#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lnot_wifi_identity.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define WIFI_NAMESPACE "lnot_config"
#define WIFI_KEY "wifi"
#define ADMIN_KEY "admin"
#define WIFI_CONNECT_TIMEOUT_US (30LL * 1000LL * 1000LL)
#define PASSWORD_SALT_SIZE 16
#define PASSWORD_HASH_SIZE 32
#define PASSWORD_ITERATIONS 100000
#define HTTP_BODY_LIMIT 512
#define SESSION_TIMEOUT_US (30LL * 60LL * 1000LL * 1000LL)
#define LOGIN_RETRY_DELAY_US (1000LL * 1000LL)

static const char *const TAG = "lnot_border_router";
static const char *const WEB_PAGE =
    "<!doctype html><html><meta name=\"viewport\" content=\"width=device-width\">"
    "<title>LocoNet Border Router</title><h1>LocoNet Border Router</h1>"
    "<main id=\"app\"></main><script>"
    "const root=document.querySelector('#app');"
    "async function req(url,method='GET',body){let r=await fetch(url,{method,"
    "headers:body?{'Content-Type':'application/json'}:{},"
    "body:body?JSON.stringify(body):undefined});if(!r.ok)throw Error(await r.text());"
    "return r.status===204?null:r.json()}"
    "async function init(){let s=await req('/api/state');"
    "if(s.setup){root.innerHTML='<h2>Set administrator password</h2><form id=\"setup\">"
    "<input type=\"password\" minlength=\"12\" required placeholder=\"At least 12 characters\">"
    "<button>Set password</button></form>';document.querySelector('#setup').onsubmit=async e=>{"
    "e.preventDefault();await req('/api/setup','POST',{password:e.target[0].value});init()};return}"
    "if(!s.loggedIn){root.innerHTML='<h2>Sign in</h2><form id=\"login\">"
    "<input type=\"password\" required placeholder=\"Administrator password\">"
    "<button>Sign in</button></form>';document.querySelector('#login').onsubmit=async e=>{"
    "e.preventDefault();await req('/api/login','POST',{password:e.target[0].value});init()};return}"
    "let w=await req('/api/wifi');root.innerHTML='<p id=\"mode\"></p><p id=\"address\"></p>"
    "<p id=\"current\"></p>"
    "<form id=\"wifi\"><input name=\"ssid\" maxlength=\"32\" required placeholder=\"WiFi SSID\">"
    "<input name=\"password\" type=\"password\" maxlength=\"64\" placeholder=\"WiFi password\">"
    "<button>Save WiFi</button></form>"
    "<button id=\"clear\">Clear WiFi credentials</button> "
    "<button id=\"logout\">Log out</button>';document.querySelector('#wifi').onsubmit=async e=>{"
    "e.preventDefault();await req('/api/wifi','POST',{ssid:e.target.ssid.value,"
    "password:e.target.password.value});alert('Saved. Trying the new network.');init()};"
    "document.querySelector('#mode').textContent='Mode: '+s.mode;"
    "document.querySelector('#address').textContent='Address: '+s.ip;"
    "document.querySelector('#current').textContent='WiFi credentials: '+"
    "(w.configured?'configured':'not configured');"
    "document.querySelector('#clear').onclick=async()=>{await req('/api/wifi','DELETE');init()};"
    "document.querySelector('#logout').onclick=async()=>{await req('/api/logout','POST',{});init()}}"
    "init().catch(e=>{root.textContent=e.message})</script></html>";

typedef struct {
    char ssid[33];
    char password[65];
} wifi_credentials_t;

typedef struct {
    uint8_t salt[PASSWORD_SALT_SIZE];
    uint8_t hash[PASSWORD_HASH_SIZE];
} password_record_t;

static wifi_credentials_t s_wifi_credentials;
static bool s_have_wifi_credentials;
static bool s_admin_password_set;
static bool s_station_connecting;
static bool s_station_connected;
static int64_t s_connect_started_us;
static httpd_handle_t s_http_server;
static esp_netif_t *s_station_netif;
static esp_timer_handle_t s_connect_timeout_timer;
static char s_ap_ssid[24];
static char s_session_token[65];
static int64_t s_session_last_use_us;
static int64_t s_next_login_allowed_us;

static esp_err_t start_wifi_client_attempt(void);

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t length)
{
    volatile uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0;
}

static esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Could not initialize configuration storage");
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t storage_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    size_t size = sizeof(s_wifi_credentials);
    err = nvs_get_blob(handle, WIFI_KEY, &s_wifi_credentials, &size);
    if (err == ESP_OK && size == sizeof(s_wifi_credentials) &&
        s_wifi_credentials.ssid[0] != '\0' &&
        memchr(s_wifi_credentials.ssid, '\0', sizeof(s_wifi_credentials.ssid)) != NULL &&
        memchr(s_wifi_credentials.password, '\0', sizeof(s_wifi_credentials.password)) != NULL) {
        s_have_wifi_credentials = true;
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_SIZE;
    }
    size = sizeof(password_record_t);
    password_record_t admin_record;
    esp_err_t admin_err = nvs_get_blob(handle, ADMIN_KEY, &admin_record, &size);
    if (admin_err == ESP_OK && size == sizeof(admin_record)) {
        s_admin_password_set = true;
    } else if (admin_err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        memset(&admin_record, 0, sizeof(admin_record));
        return ESP_ERR_INVALID_SIZE;
    }
    memset(&admin_record, 0, sizeof(admin_record));
    nvs_close(handle);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);
    if (ssid_len == 0 || ssid_len > 32 || password_len > 64 ||
        (password_len != 0 && password_len < 8)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password_len == 64) {
        for (size_t i = 0; i < password_len; ++i) {
            if (!isxdigit((unsigned char)password[i])) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    wifi_credentials_t credentials = {0};
    memcpy(credentials.ssid, ssid, ssid_len);
    memcpy(credentials.password, password, password_len);

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle), TAG, "Storage unavailable");
    esp_err_t err = nvs_set_blob(handle, WIFI_KEY, &credentials, sizeof(credentials));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        s_wifi_credentials = credentials;
        s_have_wifi_credentials = true;
    }
    memset(&credentials, 0, sizeof(credentials));
    return err;
}

static esp_err_t clear_wifi_credentials(void)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle), TAG, "Storage unavailable");
    esp_err_t err = nvs_erase_key(handle, WIFI_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        memset(&s_wifi_credentials, 0, sizeof(s_wifi_credentials));
        s_have_wifi_credentials = false;
        s_station_connecting = false;
        s_station_connected = false;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "Could not switch to AP mode");
    }
    return err;
}

static esp_err_t derive_password_hash(
    const char *password, const uint8_t salt[PASSWORD_SALT_SIZE],
    uint8_t hash[PASSWORD_HASH_SIZE])
{
    int result = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256, (const unsigned char *)password, strlen(password), salt,
        PASSWORD_SALT_SIZE, PASSWORD_ITERATIONS, PASSWORD_HASH_SIZE, hash);
    return result == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t save_admin_password(const char *password)
{
    if (strlen(password) < 12 || strlen(password) > 128) {
        return ESP_ERR_INVALID_ARG;
    }
    password_record_t record;
    esp_fill_random(record.salt, sizeof(record.salt));
    ESP_RETURN_ON_ERROR(derive_password_hash(password, record.salt, record.hash), TAG, "Password hashing failed");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle), TAG, "Storage unavailable");
    esp_err_t err = nvs_set_blob(handle, ADMIN_KEY, &record, sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    memset(&record, 0, sizeof(record));
    if (err == ESP_OK) {
        s_admin_password_set = true;
    }
    return err;
}

static bool verify_admin_password(const char *password)
{
    password_record_t record;
    nvs_handle_t handle;
    if (nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(record);
    esp_err_t err = nvs_get_blob(handle, ADMIN_KEY, &record, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(record)) {
        return false;
    }
    uint8_t candidate[PASSWORD_HASH_SIZE];
    bool valid = derive_password_hash(password, record.salt, candidate) == ESP_OK &&
        constant_time_equal(candidate, record.hash, sizeof(candidate));
    memset(&record, 0, sizeof(record));
    memset(candidate, 0, sizeof(candidate));
    return valid;
}

static void set_session_cookie(httpd_req_t *request)
{
    uint8_t random[32];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        snprintf(&s_session_token[i * 2], 3, "%02x", random[i]);
    }
    s_session_last_use_us = esp_timer_get_time();
    char cookie[128];
    snprintf(cookie, sizeof(cookie), "lnot_session=%s; HttpOnly; SameSite=Strict; Path=/",
             s_session_token);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    memset(random, 0, sizeof(random));
}

static bool is_authenticated(httpd_req_t *request)
{
    if (s_session_token[0] == '\0' ||
        esp_timer_get_time() - s_session_last_use_us > SESSION_TIMEOUT_US) {
        s_session_token[0] = '\0';
        return false;
    }
    size_t length = httpd_req_get_hdr_value_len(request, "Cookie");
    if (length == 0 || length >= 256) {
        return false;
    }
    char cookie[256];
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return false;
    }
    const char *value = strstr(cookie, "lnot_session=");
    if (value == NULL) {
        return false;
    }
    value += strlen("lnot_session=");
    size_t token_len = strcspn(value, "; ");
    bool valid = token_len == strlen(s_session_token) &&
        mbedtls_ct_memcmp(value, s_session_token, token_len) == 0;
    if (valid) {
        s_session_last_use_us = esp_timer_get_time();
    }
    return valid;
}

static esp_err_t send_error(httpd_req_t *request, const char *status, const char *message)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, message);
}

static esp_err_t read_json(httpd_req_t *request, cJSON **json)
{
    if (request->content_len == 0 || request->content_len > HTTP_BODY_LIMIT) {
        return ESP_ERR_INVALID_SIZE;
    }
    char body[HTTP_BODY_LIMIT + 1];
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)count;
    }
    body[received] = '\0';
    *json = cJSON_Parse(body);
    memset(body, 0, received);
    return *json == NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}

static const char *json_string(cJSON *json, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static esp_err_t api_state_handler(httpd_req_t *request)
{
    cJSON *state = cJSON_CreateObject();
    if (state == NULL) {
        return ESP_FAIL;
    }
    bool authenticated = is_authenticated(request);
    cJSON_AddBoolToObject(state, "setup", !s_admin_password_set);
    cJSON_AddBoolToObject(state, "loggedIn", authenticated);
    if (authenticated) {
        const char *mode = s_station_connected ? "client" :
            s_station_connecting ? "connecting" : "access-point";
        cJSON_AddStringToObject(state, "mode", mode);
        char address[16] = "192.168.4.1";
        if (s_station_connected) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(s_station_netif, &ip_info) == ESP_OK) {
                snprintf(address, sizeof(address), IPSTR, IP2STR(&ip_info.ip));
            }
        }
        cJSON_AddStringToObject(state, "ip", address);
    }
    char *encoded = cJSON_PrintUnformatted(state);
    cJSON_Delete(state);
    if (encoded == NULL) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, encoded);
    free(encoded);
    return err;
}

static esp_err_t page_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (s_admin_password_set && !is_authenticated(request)) {
        httpd_resp_set_status(request, "401 Unauthorized");
        return httpd_resp_sendstr(request, WEB_PAGE);
    }
    return httpd_resp_sendstr(request, WEB_PAGE);
}

static esp_err_t setup_handler(httpd_req_t *request)
{
    if (s_admin_password_set) {
        return send_error(request, "403 Forbidden", "{\"error\":\"setup disabled\"}");
    }
    cJSON *json = NULL;
    if (read_json(request, &json) != ESP_OK) {
        return send_error(request, "400 Bad Request", "{\"error\":\"invalid request\"}");
    }
    const char *password = json_string(json, "password");
    esp_err_t err = password == NULL ? ESP_ERR_INVALID_ARG : save_admin_password(password);
    cJSON *password_item = cJSON_GetObjectItemCaseSensitive(json, "password");
    if (cJSON_IsString(password_item)) {
        memset(password_item->valuestring, 0, strlen(password_item->valuestring));
    }
    cJSON_Delete(json);
    if (err != ESP_OK) {
        return send_error(request, "400 Bad Request", "{\"error\":\"invalid password\"}");
    }
    set_session_cookie(request);
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t login_handler(httpd_req_t *request)
{
    if (esp_timer_get_time() < s_next_login_allowed_us) {
        return send_error(request, "429 Too Many Requests", "{\"error\":\"try again later\"}");
    }
    cJSON *json = NULL;
    if (!s_admin_password_set || read_json(request, &json) != ESP_OK) {
        return send_error(request, "400 Bad Request", "{\"error\":\"invalid request\"}");
    }
    const char *password = json_string(json, "password");
    bool valid = password != NULL && verify_admin_password(password);
    cJSON *password_item = cJSON_GetObjectItemCaseSensitive(json, "password");
    if (cJSON_IsString(password_item)) {
        memset(password_item->valuestring, 0, strlen(password_item->valuestring));
    }
    cJSON_Delete(json);
    if (!valid) {
        s_next_login_allowed_us = esp_timer_get_time() + LOGIN_RETRY_DELAY_US;
        return send_error(request, "401 Unauthorized", "{\"error\":\"authentication failed\"}");
    }
    s_next_login_allowed_us = 0;
    set_session_cookie(request);
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t logout_handler(httpd_req_t *request)
{
    if (!is_authenticated(request)) {
        return send_error(request, "401 Unauthorized", "{\"error\":\"authentication required\"}");
    }
    s_session_token[0] = '\0';
    httpd_resp_set_hdr(request, "Set-Cookie", "lnot_session=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict");
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t wifi_handler(httpd_req_t *request)
{
    if (!is_authenticated(request)) {
        return send_error(request, "401 Unauthorized", "{\"error\":\"authentication required\"}");
    }
    if (request->method == HTTP_GET) {
        cJSON *state = cJSON_CreateObject();
        if (state == NULL) {
            return ESP_FAIL;
        }
        cJSON_AddBoolToObject(state, "configured", s_have_wifi_credentials);
        char *encoded = cJSON_PrintUnformatted(state);
        cJSON_Delete(state);
        if (encoded == NULL) {
            return ESP_FAIL;
        }
        httpd_resp_set_type(request, "application/json");
        esp_err_t err = httpd_resp_sendstr(request, encoded);
        free(encoded);
        return err;
    }
    if (request->method == HTTP_DELETE) {
        esp_err_t err = clear_wifi_credentials();
        if (err != ESP_OK) {
            return send_error(request, "500 Internal Server Error", "{\"error\":\"could not clear WiFi settings\"}");
        }
        httpd_resp_set_status(request, "204 No Content");
        return httpd_resp_send(request, NULL, 0);
    }
    cJSON *json = NULL;
    if (read_json(request, &json) != ESP_OK) {
        return send_error(request, "400 Bad Request", "{\"error\":\"invalid request\"}");
    }
    const char *ssid = json_string(json, "ssid");
    const char *password = json_string(json, "password");
    esp_err_t err = (ssid == NULL || password == NULL) ?
        ESP_ERR_INVALID_ARG : save_wifi_credentials(ssid, password);
    cJSON *password_item = cJSON_GetObjectItemCaseSensitive(json, "password");
    if (cJSON_IsString(password_item)) {
        memset(password_item->valuestring, 0, strlen(password_item->valuestring));
    }
    cJSON_Delete(json);
    if (err != ESP_OK) {
        return send_error(request, "400 Bad Request", "{\"error\":\"invalid WiFi settings\"}");
    }
    err = start_wifi_client_attempt();
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "{\"error\":\"could not start WiFi\"}");
    }
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t authenticated_handler(httpd_req_t *request)
{
    if (!is_authenticated(request)) {
        return send_error(request, "401 Unauthorized", "{\"error\":\"authentication required\"}");
    }
    httpd_resp_set_status(request, "404 Not Found");
    return httpd_resp_sendstr(request, "Not found");
}

static void register_uri(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t route = {.uri = uri, .method = method, .handler = handler, .user_ctx = NULL};
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http_server, &route));
}

static void make_ap_ssid(void)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY));
    if (!lnot_wifi_ap_ssid_from_mac(mac, s_ap_ssid, sizeof(s_ap_ssid))) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_SIZE);
    }
}

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_station_connected = true;
        s_station_connecting = false;
        esp_timer_stop(s_connect_timeout_timer);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED &&
               s_station_connecting) {
        if (esp_timer_get_time() - s_connect_started_us < WIFI_CONNECT_TIMEOUT_US) {
            esp_wifi_connect();
        } else {
            s_station_connecting = false;
            s_station_connected = false;
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED &&
               s_station_connected) {
        s_station_connected = false;
        s_station_connecting = true;
        s_connect_started_us = esp_timer_get_time();
        if (esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK &&
            esp_timer_start_once(s_connect_timeout_timer, WIFI_CONNECT_TIMEOUT_US) == ESP_OK) {
            esp_wifi_connect();
        } else {
            s_station_connecting = false;
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
    }
}

static void connect_timeout_callback(void *argument)
{
    (void)argument;
    if (s_station_connecting &&
        esp_timer_get_time() - s_connect_started_us >= WIFI_CONNECT_TIMEOUT_US) {
        s_station_connecting = false;
        s_station_connected = false;
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Could not restore access point mode (%s)", esp_err_to_name(err));
        }
    }
}

static esp_err_t start_wifi_client_attempt(void)
{
    if (!s_have_wifi_credentials) {
        return ESP_ERR_INVALID_STATE;
    }
    s_station_connecting = false;
    s_station_connected = false;
    (void)esp_timer_stop(s_connect_timeout_timer);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "Could not enable WiFi client");
    (void)esp_wifi_disconnect();

    wifi_config_t station = {0};
    memcpy(station.sta.ssid, s_wifi_credentials.ssid, strlen(s_wifi_credentials.ssid));
    memcpy(station.sta.password, s_wifi_credentials.password, strlen(s_wifi_credentials.password));
    station.sta.threshold.authmode = s_wifi_credentials.password[0] == '\0' ?
        WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &station);
    memset(&station, 0, sizeof(station));
    if (err != ESP_OK) {
        return err;
    }
    s_station_connecting = true;
    s_connect_started_us = esp_timer_get_time();
    err = esp_timer_start_once(s_connect_timeout_timer, WIFI_CONNECT_TIMEOUT_US);
    if (err != ESP_OK) {
        s_station_connecting = false;
        (void)esp_wifi_set_mode(WIFI_MODE_AP);
        return err;
    }
    return esp_wifi_connect();
}

static esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 8192;
    ESP_RETURN_ON_ERROR(httpd_start(&s_http_server, &config), TAG, "Could not start WebUI server");
    register_uri("/", HTTP_GET, page_handler);
    register_uri("/api/state", HTTP_GET, api_state_handler);
    register_uri("/api/setup", HTTP_POST, setup_handler);
    register_uri("/api/login", HTTP_POST, login_handler);
    register_uri("/api/logout", HTTP_POST, logout_handler);
    register_uri("/api/wifi", HTTP_GET, wifi_handler);
    register_uri("/api/wifi", HTTP_POST, wifi_handler);
    register_uri("/api/wifi", HTTP_DELETE, wifi_handler);
    register_uri("/api/", HTTP_GET, authenticated_handler);
    return ESP_OK;
}

static void print_status(void)
{
    const char *mode = s_station_connected ? "WiFi client" :
        s_station_connecting ? "AP + WiFi client connection attempt" : "Access point";
    char address[16] = "192.168.4.1";
    if (s_station_connected) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_station_netif, &ip_info) == ESP_OK) {
            snprintf(address, sizeof(address), IPSTR, IP2STR(&ip_info.ip));
        }
    }
    printf("Mode: %s; WebUI: http://%s/; AP SSID: %s; WiFi credentials: %s; "
           "administrator password: %s\n", mode, address, s_ap_ssid,
           s_have_wifi_credentials ? "configured" : "not configured",
           s_admin_password_set ? "configured" : "not configured");
}

static void serial_console_task(void *argument)
{
    (void)argument;
    char line[256];
    printf("Serial commands: status | wifi set <SSID><TAB><password> | wifi clear | "
           "admin set <password> | factory-reset\n");
    while (fgets(line, sizeof(line), stdin) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "status") == 0) {
            print_status();
        } else if (strcmp(line, "wifi clear") == 0) {
            if (clear_wifi_credentials() == ESP_OK) {
                puts("WiFi credentials cleared; access point enabled.");
            } else {
                puts("Could not clear WiFi credentials.");
            }
        } else if (strncmp(line, "wifi set ", 9) == 0) {
            char *separator = strchr(line + 9, '\t');
            if (separator == NULL) {
                puts("Use: wifi set <SSID><TAB><password> (empty password for an open WiFi).");
            } else {
                *separator++ = '\0';
                if (save_wifi_credentials(line + 9, separator) == ESP_OK &&
                    start_wifi_client_attempt() == ESP_OK) {
                    puts("WiFi settings saved; trying the configured network.");
                } else {
                    puts("Invalid WiFi settings or unable to start connection.");
                }
            }
        } else if (strncmp(line, "admin set ", 10) == 0) {
            if (save_admin_password(line + 10) == ESP_OK) {
                s_session_token[0] = '\0';
                puts("Administrator password saved.");
            } else {
                puts("Password must contain 12 to 128 characters.");
            }
        } else if (strcmp(line, "factory-reset") == 0) {
            puts("Erasing configuration and restarting.");
            ESP_ERROR_CHECK(nvs_flash_erase());
            esp_restart();
        } else if (line[0] != '\0') {
            puts("Unknown command. Type status, wifi set, wifi clear, admin set, or factory-reset.");
        }
        memset(line, 0, sizeof(line));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(storage_load());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (esp_netif_create_default_wifi_ap() == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    s_station_netif = esp_netif_create_default_wifi_sta();
    if (s_station_netif == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    const esp_timer_create_args_t timeout_args = {
        .callback = connect_timeout_callback,
        .name = "wifi_connect_timeout",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timeout_args, &s_connect_timeout_timer));

    make_ap_ssid();
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(start_web_server());

    if (s_have_wifi_credentials) {
        ESP_ERROR_CHECK(start_wifi_client_attempt());
    }
    xTaskCreate(serial_console_task, "serial_console", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Border Router ready; AP SSID: %s", s_ap_ssid);
}

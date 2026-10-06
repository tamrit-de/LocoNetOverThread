#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lnot_wifi_identity.h"
#include "lwip/ip4_addr.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define WIFI_NAMESPACE "lnot_config"
#define WIFI_KEY "wifi"
#define ADMIN_KEY "admin"
#define WIFI_CONNECT_TIMEOUT_US (30LL * 1000LL * 1000LL)
#define FACTORY_RESET_BUTTON_GPIO GPIO_NUM_9
#define FACTORY_RESET_HOLD_US (10LL * 1000LL * 1000LL)
#define FACTORY_RESET_BUTTON_POLL_MS 100
#define PASSWORD_SALT_SIZE 16
#define PASSWORD_HASH_SIZE 32
#define PASSWORD_ITERATIONS 100000
#define PASSWORD_YIELD_INTERVAL 1024
#define HTTP_BODY_LIMIT 512
#define SESSION_TIMEOUT_US (30LL * 60LL * 1000LL * 1000LL)
#define LOGIN_RETRY_DELAY_US (1000LL * 1000LL)
#define AP_IP_ADDRESS "192.168.70.1"

static const char *const TAG = "lnot_border_router";
static const char *const WEB_PAGE =
    "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta name=\"theme-color\" content=\"#0b1220\"><title>LocoNet Border Router</title>"
    "<style>"
    ":root{color-scheme:dark;font-family:system-ui,-apple-system,BlinkMacSystemFont,"
    "'Segoe UI',sans-serif;background:#0b1220;color:#e5e7eb}"
    "*{box-sizing:border-box}body{margin:0;min-width:320px}"
    ".shell{width:min(100%,960px);margin:auto;padding:24px 20px 48px}"
    ".brand{display:flex;align-items:center;gap:14px;margin:8px 0 32px}"
    ".brand-mark{display:grid;place-items:center;width:44px;height:44px;border-radius:12px;"
    "background:linear-gradient(135deg,#38bdf8,#2563eb);color:#fff;font-weight:800;font-size:19px}"
    ".brand h1{margin:0;font-size:clamp(1.25rem,4vw,1.7rem);letter-spacing:-.02em}"
    ".brand p{margin:2px 0 0;color:#94a3b8;font-size:.9rem}"
    ".card{background:#111c31;border:1px solid #263651;border-radius:16px;padding:24px;"
    "box-shadow:0 18px 45px #02061755}.auth{max-width:440px;margin:9vh auto 0}"
    "h2{margin:0 0 8px;font-size:1.35rem}h3{margin:0;font-size:1rem}"
    ".muted{margin:0;color:#94a3b8;line-height:1.55}.stack{display:grid;gap:16px;margin-top:24px}"
    "label{display:grid;gap:7px;font-size:.9rem;font-weight:600}"
    "input{width:100%;padding:12px 13px;border:1px solid #40516d;border-radius:9px;"
    "background:#0b1220;color:#f8fafc;font:inherit}input:focus{outline:2px solid #38bdf8;"
    "outline-offset:2px;border-color:transparent}"
    "button{border:0;border-radius:9px;padding:12px 16px;background:#38bdf8;color:#06213a;"
    "font:700 .92rem inherit;cursor:pointer}button:hover{background:#7dd3fc}"
    "button:disabled{opacity:.55;cursor:wait}.secondary{background:#21314b;color:#dbeafe}"
    ".secondary:hover{background:#2f4467}.danger{background:transparent;border:1px solid #6b3a4c;"
    "color:#fda4af}.danger:hover{background:#3c1d2e}.row{display:flex;gap:12px;flex-wrap:wrap;"
    "align-items:center}.row button{flex:1 1 160px}.dashboard{display:grid;gap:20px}"
    ".overview{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}"
    ".metric{padding:16px;border-radius:12px;background:#0b1220;border:1px solid #263651}"
    ".metric span{display:block;color:#94a3b8;font-size:.8rem}.metric strong{display:block;"
    "margin-top:7px;font-size:1rem;overflow-wrap:anywhere}.indicator{color:#5eead4}"
    ".section-title{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:20px}"
    ".notice{margin-top:16px;padding:11px 13px;border-radius:9px;line-height:1.4}"
    ".notice.error{background:#3c1d2e;color:#fecdd3}.notice.success{background:#12372f;color:#99f6e4}"
    ".hidden{display:none}@media(max-width:620px){.shell{padding:16px 14px 36px}.brand{margin-bottom:24px}"
    ".card{padding:20px}.overview{grid-template-columns:1fr}.auth{margin-top:4vh}}"
    "</style></head><body><div class=\"shell\"><header class=\"brand\">"
    "<div class=\"brand-mark\">LN</div><div><h1>LocoNet Border Router</h1>"
    "<p>Netzwerk und Gerätezustand verwalten <span id=\"firmware-version\"></span></p></div></header><main id=\"app\"></main></div>"
    "<script>"
    "const root=document.querySelector('#app');"
    "function message(error){try{const json=JSON.parse(error.message);return json.error||'Anfrage fehlgeschlagen'}"
    "catch(_){return error.message||'Anfrage fehlgeschlagen'}}"
    "async function request(url,method='GET',body){const response=await fetch(url,{method,"
    "headers:body?{'Content-Type':'application/json'}:{},body:body?JSON.stringify(body):undefined});"
    "if(!response.ok)throw Error(await response.text());return response.status===204?null:response.json()}"
    "function showNotice(text,type='error'){const notice=root.querySelector('[data-notice]');"
    "notice.textContent=text;notice.className='notice '+type}"
    "function setBusy(form,busy){const button=form.querySelector('button');button.disabled=busy;"
    "button.dataset.label||(button.dataset.label=button.textContent);button.textContent=busy?'Bitte warten…':button.dataset.label}"
    "function renderAuth(setup){const title=setup?'Administratorpasswort festlegen':'Anmelden';"
    "const description=setup?'Schützen Sie die Konfiguration mit einem Passwort.':'Melden Sie sich an, um die Konfiguration zu verwalten.';"
    "root.innerHTML='<section class=\"card auth\"><h2>'+title+'</h2><p class=\"muted\">'+description+'</p>"
    "<form class=\"stack\" id=\"auth\"><label>Passwort<input name=\"password\" type=\"password\" '"
    "+(setup?'minlength=\"12\" placeholder=\"Mindestens 12 Zeichen\"':'placeholder=\"Administratorpasswort\"')+' required></label>"
    "<button type=\"submit\">'+(setup?'Passwort speichern':'Anmelden')+'</button></form>"
    "<p class=\"notice hidden\" data-notice role=\"alert\"></p></section>';"
    "const form=document.querySelector('#auth');form.onsubmit=async event=>{event.preventDefault();"
    "setBusy(form,true);try{await request(setup?'/api/setup':'/api/login','POST',{password:form.elements.password.value});"
    "await refresh()}catch(error){showNotice(message(error))}finally{setBusy(form,false)}}}"
    "function modeLabel(mode){return mode==='access-point + client'?'Verbunden':mode==='connecting'?"
    "'Verbindung wird hergestellt':'Einrichtung aktiv'}"
    "function renderDashboard(state,wifi){root.innerHTML='<section class=\"dashboard\">"
    "<div class=\"overview\"><article class=\"metric\"><span>Status</span><strong class=\"indicator\" id=\"mode\"></strong></article>"
    "<article class=\"metric\"><span>WebUI-Adresse</span><strong id=\"address\"></strong></article>"
    "<article class=\"metric\"><span>WLAN-Konfiguration</span><strong id=\"configured\"></strong></article></div>"
    "<section class=\"card\"><div class=\"section-title\"><div><h2>WLAN konfigurieren</h2>"
    "<p class=\"muted\">Zugangsdaten speichern und die Verbindung starten.</p></div></div>"
    "<form class=\"stack\" id=\"wifi\"><label>Netzwerkname (SSID)<input name=\"ssid\" maxlength=\"32\" "
    "autocomplete=\"off\" required placeholder=\"Mein WLAN\"></label><label>WLAN-Passwort"
    "<input name=\"password\" type=\"password\" maxlength=\"64\" autocomplete=\"new-password\" "
    "placeholder=\"Für ein offenes Netzwerk leer lassen\"></label><button type=\"submit\">WLAN speichern</button></form>"
    "<p class=\"notice hidden\" data-notice role=\"status\"></p></section><section class=\"card\">"
    "<h3>Sitzung und gespeicherte Daten</h3><p class=\"muted\" style=\"margin-top:8px\">"
    "Sie können die WLAN-Konfiguration entfernen oder sich sicher abmelden.</p><div class=\"row\" style=\"margin-top:18px\">"
    "<button class=\"danger\" id=\"clear\" type=\"button\">WLAN-Daten löschen</button>"
    "<button class=\"secondary\" id=\"logout\" type=\"button\">Abmelden</button></div></section></section>';"
    "root.querySelector('#mode').textContent=modeLabel(state.mode);root.querySelector('#address').textContent=state.ip;"
    "root.querySelector('#configured').textContent=wifi.configured?'Gespeichert':'Nicht gespeichert';"
    "const form=root.querySelector('#wifi');form.onsubmit=async event=>{event.preventDefault();setBusy(form,true);"
    "try{await request('/api/wifi','POST',{ssid:form.elements.ssid.value,password:form.elements.password.value});"
    "await refresh();showNotice('WLAN-Daten gespeichert. Die Verbindung wird hergestellt.','success')}catch(error){showNotice(message(error))}"
    "finally{setBusy(form,false)}};root.querySelector('#clear').onclick=async()=>{if(!confirm('WLAN-Daten wirklich löschen?'))return;"
    "try{await request('/api/wifi','DELETE');await refresh()}catch(error){showNotice(message(error))}};"
    "root.querySelector('#logout').onclick=async()=>{try{await request('/api/logout','POST',{});window.location.replace('/')}"
    "catch(error){showNotice(message(error))}}}"
    "async function refresh(){const state=await request('/api/state');document.querySelector('#firmware-version').textContent='Firmware-Version: '+state.version;if(state.setup)return renderAuth(true);"
    "if(!state.loggedIn)return renderAuth(false);renderDashboard(state,await request('/api/wifi'))}"
    "refresh().catch(error=>{root.innerHTML='<section class=\"card auth\"><h2>WebUI nicht verfügbar</h2>"
    "<p class=\"notice error\">'+message(error)+'</p><button onclick=\"location.reload()\">Erneut versuchen</button></section>'})"
    "</script></body></html>";

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
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_station_netif;
static esp_timer_handle_t s_connect_timeout_timer;
static char s_device_name[24];
static char s_session_token[65];
static char s_session_cookie[128];
static int64_t s_session_last_use_us;
static int64_t s_next_login_allowed_us;
static SemaphoreHandle_t s_state_mutex;

static esp_err_t start_wifi_client_attempt(void);

static void state_lock(void)
{
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
}

static void state_unlock(void)
{
    xSemaphoreGiveRecursive(s_state_mutex);
}

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
        mbedtls_platform_zeroize(&admin_record, sizeof(admin_record));
        return ESP_ERR_INVALID_SIZE;
    }
    mbedtls_platform_zeroize(&admin_record, sizeof(admin_record));
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

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    wifi_credentials_t credentials = {0};
    memcpy(credentials.ssid, ssid, ssid_len);
    memcpy(credentials.password, password, password_len);

    err = nvs_set_blob(handle, WIFI_KEY, &credentials, sizeof(credentials));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        s_wifi_credentials = credentials;
        s_have_wifi_credentials = true;
    }
    mbedtls_platform_zeroize(&credentials, sizeof(credentials));
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
        mbedtls_platform_zeroize(&s_wifi_credentials, sizeof(s_wifi_credentials));
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
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL) {
        return ESP_FAIL;
    }

    size_t password_length = strlen(password);
    uint8_t initial_input[PASSWORD_SALT_SIZE + 4];
    uint8_t previous[PASSWORD_HASH_SIZE];
    uint8_t next[PASSWORD_HASH_SIZE];
    uint8_t accumulated[PASSWORD_HASH_SIZE];
    memcpy(initial_input, salt, PASSWORD_SALT_SIZE);
    initial_input[PASSWORD_SALT_SIZE] = 0;
    initial_input[PASSWORD_SALT_SIZE + 1] = 0;
    initial_input[PASSWORD_SALT_SIZE + 2] = 0;
    initial_input[PASSWORD_SALT_SIZE + 3] = 1;

    int result = mbedtls_md_hmac(
        md_info, (const unsigned char *)password, password_length, initial_input,
        sizeof(initial_input), previous);
    if (result == 0) {
        memcpy(accumulated, previous, sizeof(accumulated));
        for (size_t iteration = 1; iteration < PASSWORD_ITERATIONS; ++iteration) {
            result = mbedtls_md_hmac(
                md_info, (const unsigned char *)password, password_length, previous,
                sizeof(previous), next);
            if (result != 0) {
                break;
            }
            for (size_t byte = 0; byte < sizeof(accumulated); ++byte) {
                accumulated[byte] ^= next[byte];
            }
            memcpy(previous, next, sizeof(previous));
            if (iteration % PASSWORD_YIELD_INTERVAL == 0) {
                vTaskDelay(1);
            }
        }
    }
    if (result == 0) {
        memcpy(hash, accumulated, sizeof(accumulated));
    }
    mbedtls_platform_zeroize(initial_input, sizeof(initial_input));
    mbedtls_platform_zeroize(previous, sizeof(previous));
    mbedtls_platform_zeroize(next, sizeof(next));
    mbedtls_platform_zeroize(accumulated, sizeof(accumulated));
    return result == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t save_admin_password(const char *password)
{
    if (strlen(password) < 12 || strlen(password) > 128) {
        return ESP_ERR_INVALID_ARG;
    }
    password_record_t record;
    esp_fill_random(record.salt, sizeof(record.salt));
    esp_err_t err = derive_password_hash(password, record.salt, record.hash);
    if (err != ESP_OK) {
        goto cleanup;
    }
    nvs_handle_t handle;
    err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        goto cleanup;
    }
    err = nvs_set_blob(handle, ADMIN_KEY, &record, sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
cleanup:
    mbedtls_platform_zeroize(&record, sizeof(record));
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
    mbedtls_platform_zeroize(&record, sizeof(record));
    mbedtls_platform_zeroize(candidate, sizeof(candidate));
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
    snprintf(s_session_cookie, sizeof(s_session_cookie),
             "lnot_session=%s; HttpOnly; SameSite=Strict; Path=/",
             s_session_token);
    httpd_resp_set_hdr(request, "Set-Cookie", s_session_cookie);
    mbedtls_platform_zeroize(random, sizeof(random));
}

static bool is_authenticated(httpd_req_t *request)
{
    if (s_session_token[0] == '\0' ||
        esp_timer_get_time() - s_session_last_use_us > SESSION_TIMEOUT_US) {
        s_session_token[0] = '\0';
        mbedtls_platform_zeroize(s_session_cookie, sizeof(s_session_cookie));
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
        constant_time_equal((const uint8_t *)value, (const uint8_t *)s_session_token, token_len);
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

static esp_err_t configure_ap_network(void)
{
    esp_netif_ip_info_t ip_info = {0};
    IP4_ADDR(&ip_info.ip, 192, 168, 70, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 70, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_stop(s_ap_netif), TAG,
                        "Could not stop AP DHCP server");
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_ap_netif, &ip_info), TAG,
                        "Could not configure AP IP address");
    return esp_netif_dhcps_start(s_ap_netif);
}

static bool request_uses_ap_address(httpd_req_t *request)
{
    size_t host_length = httpd_req_get_hdr_value_len(request, "Host");
    char host[sizeof(AP_IP_ADDRESS) + 7];
    if (host_length == 0 || host_length >= sizeof(host) ||
        httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) != ESP_OK) {
        return false;
    }
    size_t ap_address_length = strlen(AP_IP_ADDRESS);
    return strcmp(host, AP_IP_ADDRESS) == 0 ||
        (strncmp(host, AP_IP_ADDRESS, ap_address_length) == 0 &&
         host[ap_address_length] == ':');
}

static esp_err_t redirect_ap_request_to_station(httpd_req_t *request)
{
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_station_netif, &ip_info) != ESP_OK) {
        return ESP_FAIL;
    }
    char station_address[16];
    snprintf(station_address, sizeof(station_address), IPSTR, IP2STR(&ip_info.ip));
    char location[32];
    snprintf(location, sizeof(location), "http://%s/", station_address);
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", location);
    return httpd_resp_send(request, NULL, 0);
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
    mbedtls_platform_zeroize(body, received);
    return *json == NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}

static const char *json_string(cJSON *json, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static esp_err_t api_state_handler_impl(httpd_req_t *request)
{
    cJSON *state = cJSON_CreateObject();
    if (state == NULL) {
        return ESP_FAIL;
    }
    bool authenticated = is_authenticated(request);
    cJSON_AddBoolToObject(state, "setup", !s_admin_password_set);
    cJSON_AddBoolToObject(state, "loggedIn", authenticated);
    cJSON_AddStringToObject(state, "version", esp_app_get_description()->version);
    if (authenticated) {
        const char *mode = s_station_connected ? "access-point + client" :
            s_station_connecting ? "connecting" : "access-point";
        cJSON_AddStringToObject(state, "mode", mode);
        char address[16] = AP_IP_ADDRESS;
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

static esp_err_t page_handler_impl(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (s_station_connected && request_uses_ap_address(request)) {
        return redirect_ap_request_to_station(request);
    }
    if (s_admin_password_set && !is_authenticated(request)) {
        httpd_resp_set_status(request, "401 Unauthorized");
        return httpd_resp_sendstr(request, WEB_PAGE);
    }
    return httpd_resp_sendstr(request, WEB_PAGE);
}

static esp_err_t setup_handler_impl(httpd_req_t *request)
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
        mbedtls_platform_zeroize(password_item->valuestring, strlen(password_item->valuestring));
    }
    cJSON_Delete(json);
    if (err != ESP_OK) {
        return err == ESP_ERR_INVALID_ARG ?
            send_error(request, "400 Bad Request", "{\"error\":\"invalid password\"}") :
            send_error(request, "500 Internal Server Error", "{\"error\":\"could not save administrator password\"}");
    }
    set_session_cookie(request);
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t login_handler_impl(httpd_req_t *request)
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
        mbedtls_platform_zeroize(password_item->valuestring, strlen(password_item->valuestring));
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

static esp_err_t logout_handler_impl(httpd_req_t *request)
{
    if (!is_authenticated(request)) {
        return send_error(request, "401 Unauthorized", "{\"error\":\"authentication required\"}");
    }
    s_session_token[0] = '\0';
    mbedtls_platform_zeroize(s_session_cookie, sizeof(s_session_cookie));
    httpd_resp_set_hdr(request, "Set-Cookie", "lnot_session=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict");
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t wifi_handler_impl(httpd_req_t *request)
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
        mbedtls_platform_zeroize(password_item->valuestring, strlen(password_item->valuestring));
    }
    cJSON_Delete(json);
    if (err != ESP_OK) {
        return err == ESP_ERR_INVALID_ARG ?
            send_error(request, "400 Bad Request", "{\"error\":\"invalid WiFi settings\"}") :
            send_error(request, "500 Internal Server Error", "{\"error\":\"could not save WiFi settings\"}");
    }
    err = start_wifi_client_attempt();
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error", "{\"error\":\"could not start WiFi\"}");
    }
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t authenticated_handler_impl(httpd_req_t *request)
{
    if (!is_authenticated(request)) {
        return send_error(request, "401 Unauthorized", "{\"error\":\"authentication required\"}");
    }
    httpd_resp_set_status(request, "404 Not Found");
    return httpd_resp_sendstr(request, "Not found");
}

#define DEFINE_LOCKED_HANDLER(name) \
    static esp_err_t name(httpd_req_t *request) \
    { \
        state_lock(); \
        esp_err_t err = name##_impl(request); \
        state_unlock(); \
        return err; \
    }

DEFINE_LOCKED_HANDLER(api_state_handler)
DEFINE_LOCKED_HANDLER(page_handler)
DEFINE_LOCKED_HANDLER(setup_handler)
DEFINE_LOCKED_HANDLER(login_handler)
DEFINE_LOCKED_HANDLER(logout_handler)
DEFINE_LOCKED_HANDLER(wifi_handler)
DEFINE_LOCKED_HANDLER(authenticated_handler)

static void register_uri(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t route = {.uri = uri, .method = method, .handler = handler, .user_ctx = NULL};
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http_server, &route));
}

static void make_device_name(void)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY));
    if (!lnot_wifi_device_name_from_mac(mac, s_device_name, sizeof(s_device_name))) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_SIZE);
    }
}

static void factory_reset_button_task(void *argument)
{
    (void)argument;
    bool pressed = false;
    int64_t pressed_at_us = 0;

    for (;;) {
        bool is_pressed = gpio_get_level(FACTORY_RESET_BUTTON_GPIO) == 0;
        if (is_pressed && !pressed) {
            pressed = true;
            pressed_at_us = esp_timer_get_time();
        } else if (is_pressed && esp_timer_get_time() - pressed_at_us >= FACTORY_RESET_HOLD_US) {
            ESP_LOGW(TAG, "BOOT button held for 10 seconds; erasing configuration");
            esp_err_t err = nvs_flash_erase();
            if (err == ESP_OK) {
                esp_restart();
            }
            ESP_LOGE(TAG, "Could not erase configuration (%s)", esp_err_to_name(err));
            pressed = false;
        } else if (!is_pressed) {
            pressed = false;
        }
        vTaskDelay(pdMS_TO_TICKS(FACTORY_RESET_BUTTON_POLL_MS));
    }
}

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;
    state_lock();
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_station_connected = true;
        s_station_connecting = false;
        esp_timer_stop(s_connect_timeout_timer);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
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
    state_unlock();
}

static void connect_timeout_callback(void *argument)
{
    (void)argument;
    if (xSemaphoreTakeRecursive(s_state_mutex, 0) != pdTRUE) {
        esp_err_t retry_err = esp_timer_start_once(s_connect_timeout_timer, 1000);
        if (retry_err != ESP_OK) {
            ESP_LOGW(TAG, "Could not retry WiFi timeout handling (%s)", esp_err_to_name(retry_err));
        }
        return;
    }
    if (s_station_connecting &&
        esp_timer_get_time() - s_connect_started_us >= WIFI_CONNECT_TIMEOUT_US) {
        s_station_connecting = false;
        s_station_connected = false;
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Could not restore access point mode (%s)", esp_err_to_name(err));
        }
    }
    state_unlock();
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
    mbedtls_platform_zeroize(&station, sizeof(station));
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
    const char *mode = s_station_connected ? "AP + WiFi client" :
        s_station_connecting ? "AP + WiFi client connection attempt" : "Access point";
    char address[16] = AP_IP_ADDRESS;
    if (s_station_connected) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_station_netif, &ip_info) == ESP_OK) {
            snprintf(address, sizeof(address), IPSTR, IP2STR(&ip_info.ip));
        }
    }
    printf("Firmware: %s; Mode: %s; WebUI: http://%s/; AP SSID: %s; WiFi credentials: %s; "
           "administrator password: %s\n", esp_app_get_description()->version, mode, address,
           s_device_name,
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
        state_lock();
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
                mbedtls_platform_zeroize(s_session_cookie, sizeof(s_session_cookie));
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
        mbedtls_platform_zeroize(line, sizeof(line));
        state_unlock();
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    s_state_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_state_mutex == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(storage_load());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    ESP_ERROR_CHECK(configure_ap_network());
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

    make_device_name();
    ESP_ERROR_CHECK(esp_netif_set_hostname(s_station_netif, s_device_name));
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_device_name, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_device_name);
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(start_web_server());
    const gpio_config_t factory_reset_button_config = {
        .pin_bit_mask = 1ULL << FACTORY_RESET_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&factory_reset_button_config));

    state_lock();
    if (s_have_wifi_credentials) {
        ESP_ERROR_CHECK(start_wifi_client_attempt());
    }
    state_unlock();
    xTaskCreate(serial_console_task, "serial_console", 4096, NULL, 5, NULL);
    xTaskCreate(factory_reset_button_task, "factory_reset_button", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "Border Router ready; device name and AP SSID: %s", s_device_name);
}

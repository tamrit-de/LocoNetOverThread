#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LNOT_DEVICE_ROLE_CLIENT,
    LNOT_DEVICE_ROLE_BORDER_ROUTER,
} lnot_device_role_t;

const char *lnot_device_role_name(lnot_device_role_t role);

#ifdef __cplusplus
}
#endif

#include "lnot_device_role.h"

const char *lnot_device_role_name(lnot_device_role_t role)
{
    switch (role) {
    case LNOT_DEVICE_ROLE_CLIENT:
        return "client";
    case LNOT_DEVICE_ROLE_BORDER_ROUTER:
        return "border-router";
    default:
        return "unknown";
    }
}

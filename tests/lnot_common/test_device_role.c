#include <assert.h>
#include <string.h>

#include "lnot_device_role.h"

int main(void)
{
    assert(strcmp(lnot_device_role_name(LNOT_DEVICE_ROLE_CLIENT), "client") == 0);
    assert(strcmp(lnot_device_role_name(LNOT_DEVICE_ROLE_BORDER_ROUTER), "border-router") == 0);
    assert(strcmp(lnot_device_role_name((lnot_device_role_t)99), "unknown") == 0);

    return 0;
}

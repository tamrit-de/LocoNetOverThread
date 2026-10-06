set(LNOT_FIRMWARE_VERSION "$ENV{LNOT_FIRMWARE_VERSION}")

if(LNOT_FIRMWARE_VERSION STREQUAL "")
    set(PROJECT_VER "v0.0.0-alpha.0")
elseif(NOT LNOT_FIRMWARE_VERSION MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+(-(alpha|beta)\\.[0-9]+)?$")
    message(FATAL_ERROR
        "LNOT_FIRMWARE_VERSION must use vMAJOR.MINOR.PATCH, optionally followed by -alpha.N or -beta.N.")
else()
    set(PROJECT_VER "${LNOT_FIRMWARE_VERSION}")
endif()

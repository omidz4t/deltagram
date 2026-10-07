# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

add_library(external_tlottie_native STATIC IMPORTED GLOBAL)
add_library(external_tlottie INTERFACE IMPORTED GLOBAL)
add_library(desktop-app::external_tlottie ALIAS external_tlottie)

if (DESKTOP_APP_USE_PACKAGED OR LINUX)
    find_library(DESKTOP_APP_TLOTTIE_LIBRARY tlottie REQUIRED)
    find_path(DESKTOP_APP_TLOTTIE_INCLUDE_DIR tlottie.h PATH_SUFFIXES tlottie REQUIRED)
    set(tlottie_include "${DESKTOP_APP_TLOTTIE_INCLUDE_DIR}")
    set_target_properties(external_tlottie_native PROPERTIES
        IMPORTED_LOCATION "${DESKTOP_APP_TLOTTIE_LIBRARY}"
    )
elseif (WIN32)
    set(tlottie_loc ${libs_loc}/tlottie/out)
    set(tlottie_include ${tlottie_loc}/include)
    set_target_properties(external_tlottie_native PROPERTIES
        IMPORTED_LOCATION "${tlottie_loc}/lib/tlottie.lib"
        IMPORTED_LOCATION_DEBUG "${tlottie_loc}/lib/tlottie.lib"
        IMPORTED_LOCATION_RELEASE "${tlottie_loc}/lib/tlottie.lib"
    )
elseif (APPLE)
    set(tlottie_include ${libs_loc}/local/include/tlottie)
    set_target_properties(external_tlottie_native PROPERTIES
        IMPORTED_LOCATION ${libs_loc}/local/lib/libtlottie.a
    )
endif()

target_include_directories(external_tlottie SYSTEM
INTERFACE
    ${tlottie_include}
)

target_link_libraries(external_tlottie
INTERFACE
    external_tlottie_native
)

# What the Rust standard library inside the archive needs resolved. cargo
# reports the list through --print native-static-libs while the stage builds.
if (WIN32)
    target_link_libraries(external_tlottie
    INTERFACE
        advapi32
        bcrypt
        dbghelp
        kernel32
        ntdll
        userenv
        ws2_32
    )
endif()

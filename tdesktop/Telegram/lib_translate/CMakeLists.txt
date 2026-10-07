# This file is part of Desktop App Toolkit,
# a set of libraries for developing nice desktop applications.
#
# For license and copyright information please follow this link:
# https://github.com/desktop-app/legal/blob/master/LEGAL

if (APPLE AND NOT DESKTOP_APP_DISABLE_SWIFT6)
    if (CMAKE_GENERATOR MATCHES "Ninja" AND DESKTOP_APP_MAC_ARCH)
        set(CMAKE_OSX_ARCHITECTURES "${DESKTOP_APP_MAC_ARCH}")
    endif()
    enable_language(Swift)

    set(CMAKE_Swift_LANGUAGE_VERSION 6)
    set(CMAKE_Swift_COMPILATION_MODE wholemodule)

    get_filename_component(src_loc . REALPATH)

    add_library(lib_translate STATIC
        translate_provider_mac_swift.swift
    )

    if (CMAKE_GENERATOR MATCHES "Ninja" AND DESKTOP_APP_MAC_ARCH)
        set(swift_deployment_target "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        if (DESKTOP_APP_MAC_ARCH STREQUAL "arm64"
            AND swift_deployment_target VERSION_LESS 11.0)
            set(swift_deployment_target 11.0)
        endif()
        target_compile_options(lib_translate PRIVATE
            -target "${DESKTOP_APP_MAC_ARCH}-apple-macosx${swift_deployment_target}"
        )
    endif()

    target_include_directories(lib_translate
    PUBLIC
        ${src_loc}
    )
else()
    add_library(lib_translate INTERFACE)

    target_include_directories(lib_translate
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

add_library(desktop-app::lib_translate ALIAS lib_translate)

if (APPLE AND NOT DESKTOP_APP_DISABLE_SWIFT6)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(NATURAL_LANGUAGE_FRAMEWORK NaturalLanguage)
    find_library(TRANSLATION_FRAMEWORK Translation)

    target_link_libraries(lib_translate
    PRIVATE
        ${FOUNDATION_FRAMEWORK}
        ${NATURAL_LANGUAGE_FRAMEWORK}
        ${TRANSLATION_FRAMEWORK}
    )
endif()

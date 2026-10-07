cmake_minimum_required(VERSION 3.14.0 FATAL_ERROR)
if (POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif ()
if (POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
endif ()
if (POLICY CMP0167)
    # try to use FindBoost for now
    cmake_policy(SET CMP0167 OLD)
endif ()
project(cppgir VERSION 2.0.0)

include(FindPkgConfig)
include(GNUInstallDirs)
include(CTest)
enable_testing()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    add_compile_options (-Wall -Wextra $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>)
endif()

# clang debug stdc++
if (${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fstandalone-debug")
endif()

## OPTIONS ##
option(BUILD_DOC "build documentation" ON)
option(BUILD_TOOLS "build generator tool" ON)
option(BUILD_EXAMPLES "build examples" ON)
option(BUILD_EXAMPLES_MODULES "build module examples" OFF)
option(BUILD_EXAMPLES_FORCE "build failing module examples" OFF)
set(BUILD_EXAMPLES_OPTIONS "" CACHE STRING "example compile options")
option(BUILD_EMBED_IGNORE "embed default ignore" OFF)
option(INTERNAL_EXPECTED "use internal expected-lite" ON)
option(LINK_STDFS "link to stdc++fs" OFF)
set(BUILD_FMT AUTO CACHE STRING "format library")
set_property(CACHE BUILD_FMT PROPERTY STRINGS AUTO FMTLIB STDFORMAT)
set(GTK_MAJOR GTK3 CACHE STRING "gtk version")
set_property(CACHE GTK_MAJOR PROPERTY STRINGS GTK3 GTK4)
set(GIR_DIR ${CMAKE_INSTALL_FULL_DATADIR}/gir-1.0 CACHE STRING "extra GIR search directory")
# the following will have gir-1.0 appended (for legacy reasons)
# best left as-is to follow standard GIR search
set(GIR_DEFAULT_DIRS "/usr/local/${CMAKE_INSTALL_DATADIR}:/usr/${CMAKE_INSTALL_DATADIR}"
    CACHE STRING "fallback GIR search prefix paths (to be suffixed with gir-1.0)")

## CONTENT ##

if (BUILD_TOOLS)

find_package(Boost 1.58 REQUIRED)

# check fmtlib
pkg_check_modules(FORMAT fmt)
if (FORMAT_FOUND)
    set(FORMAT_LIBRARIES "${FORMAT_LDFLAGS}")
else ()
    # fallback for old version without pkg-config
    find_path(FORMAT_INCLUDE_DIRS fmt/format.h)
    find_library(FORMAT_LIBRARIES fmt)
endif ()
if ("${FORMAT_INCLUDE_DIRS}" STREQUAL "FORMAT_INCLUDE_DIRS-NOTFOUND" OR
    "${FORMAT_LIBRARIES}" STREQUAL "FORMAT_LIBRARIES-NOTFOUND")
    set(HAVE_FMTLIB OFF)
else()
    set(HAVE_FMTLIB ON)
endif()
message(STATUS "fmtlib available status: ${HAVE_FMTLIB}")
# check C++20 format
try_compile(HAVE_STDFORMAT ${CMAKE_BINARY_DIR}
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/cmake/cpp20_format.cpp
    CXX_STANDARD 20)
message(STATUS "std::format available status: ${HAVE_STDFORMAT}")

# check and decide on which fmt to use
set(USE_FMTLIB NONE)
if (BUILD_FMT STREQUAL "AUTO")
    if (HAVE_FMTLIB)
        set(USE_FMTLIB ON)
    elseif (HAVE_STDFORMAT)
        set(USE_FMTLIB OFF)
    endif()
elseif(BUILD_FMT STREQUAL "FMTLIB" AND HAVE_FMTLIB)
    set(USE_FMTLIB ON)
elseif(BUILD_FMT STREQUAL "STDFORMAT" AND HAVE_STDFORMAT)
    set(USE_FMTLIB OFF)
endif()
if (USE_FMTLIB STREQUAL "NONE")
    message (FATAL_ERROR "no format library found")
endif()

# check C++20 coroutine
try_compile(HAVE_CORO ${CMAKE_BINARY_DIR}
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/cmake/cpp20_coro.cpp
    CXX_STANDARD 20)
message(STATUS "coroutine support available status: ${HAVE_CORO}")

# required ignore file
set(GI_IGNORE_FILE_DIR data)
set(GI_IGNORE_FILE cppgir.ignore)
file(READ data/cppgir.ignore CPPGIR_IGNORE)
if (UNIX)
    set(GI_IGNORE_FILE_PLATFORM cppgir_unix.ignore)
    file(READ data/cppgir_unix.ignore CPPGIR_UNIX_IGNORE)
    set(CPPGIR_WIN_IGNORE "")
else ()
    set(GI_IGNORE_FILE_PLATFORM cppgir_win.ignore)
    file(READ data/cppgir_win.ignore CPPGIR_WIN_IGNORE)
    set(CPPGIR_UNIX_IGNORE "")
endif ()
set(GI_IGNORE_FILE_INSTALL_DIR ${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME})

add_executable(cppgir tools/cppgir.cpp
    tools/genbase.cpp tools/genbase.hpp
    tools/genns.cpp tools/genns.hpp
    tools/genutils.cpp tools/genutils.hpp
    tools/function.cpp tools/function.hpp
    tools/repository.cpp tools/repository.hpp
    tools/common.hpp)
target_link_libraries(cppgir Boost::boost)
if (UNIX)
    # add fixed fallback search places
    target_compile_definitions(cppgir PRIVATE
        -DGI_GIR_DIR=${GIR_DIR}
        -DGI_DATA_DIR=${CMAKE_INSTALL_FULL_DATADIR}/gir-1.0
    )
    if (GIR_DEFAULT_DIRS)
        target_compile_definitions(cppgir PRIVATE
            -DDEFAULT_GIRPATH=${GIR_DEFAULT_DIRS}
        )
    endif ()
endif ()
if (BUILD_EMBED_IGNORE)
    # generate embedded ignore data
    configure_file(tools/ignore.hpp.in tools/ignore.hpp @ONLY)
    target_include_directories(cppgir PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/tools)
    set(GENERATED_IGNORE "")
else()
    target_compile_definitions(cppgir PRIVATE
        -DDEFAULT_IGNORE_FILE=${GI_IGNORE_FILE_INSTALL_DIR}/${GI_IGNORE_FILE}:${GI_IGNORE_FILE_INSTALL_DIR}/${GI_IGNORE_FILE_PLATFORM})
    set(GENERATED_IGNORE --ignore ${GI_IGNORE_FILE_DIR}/${GI_IGNORE_FILE}:${GI_IGNORE_FILE_DIR}/${GI_IGNORE_FILE_PLATFORM})
endif()
if (USE_FMTLIB)
    target_link_libraries(cppgir ${FORMAT_LIBRARIES})
    set_property(TARGET cppgir PROPERTY CXX_STANDARD 17)
else()
    set_property(TARGET cppgir PROPERTY CXX_STANDARD 20)
endif()
if (LINK_STDFS)
    # some older gcc might sometimes (?) need this, even in c++17 mode
    # see issue #80
    target_link_libraries(cppgir stdc++fs)
endif ()
add_executable(CppGir::cppgir ALIAS cppgir)

endif ()  # BUILD_TOOLS

add_library(gi INTERFACE)
target_include_directories(gi INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/override>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/override>"
)
add_library(CppGir::gi ALIAS gi)

if (INTERNAL_EXPECTED)
    set(EXPECTED_LITE_INCLUDE "expected-lite/include")
    if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/${EXPECTED_LITE_INCLUDE}/nonstd/expected.hpp)
        target_include_directories(gi INTERFACE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/${EXPECTED_LITE_INCLUDE}>"
        )
    else ()
        message (FATAL_ERROR "missing submodule expected-lite")
    endif ()
else ()
    find_package(expected-lite)
    if (expected-lite_FOUND)
        target_link_libraries(gi INTERFACE nonstd::expected-lite)
    else ()
        target_compile_features(gi INTERFACE cxx_std_23)
        message (WARNING "thus cppgir headers will require C++23")
    endif ()
endif ()


if (BUILD_TOOLS)

if (BUILD_EXAMPLES OR BUILD_TESTING)
    pkg_check_modules(GOBJECT gobject-2.0)
endif ()
if (BUILD_EXAMPLES)
    pkg_check_modules(GIO gio-2.0 gio-unix-2.0)
    pkg_check_modules(GST gstreamer-1.0)
    if (GTK_MAJOR STREQUAL "GTK3")
        pkg_check_modules(GTK gtk+-3.0)
        set(GTK_GIR Gtk-3.0)
    endif ()
    if (GTK_MAJOR STREQUAL "GTK4")
        # only plain gio is a dependency, but GIR requires other gio parts
        pkg_check_modules(GTK gtk4 gtk4-unix-print gio-unix-2.0)
        set(GTK_GIR Gtk-4.0)
    endif ()
endif ()

## TEST ##

if (BUILD_TESTING AND GOBJECT_FOUND)
    add_library(ltest
        test/test_object.c test/test_object.h test/test_boxed.c test/test_boxed.h)
    target_include_directories(ltest PUBLIC "gi" "override")
    target_link_libraries(ltest gi ${GOBJECT_LDFLAGS})
    target_compile_options(ltest PUBLIC ${GOBJECT_CFLAGS})

    add_executable(gi-test test/main.cpp)
    target_link_libraries(gi-test PRIVATE ltest)
    add_test(NAME gi-test COMMAND gi-test)

    # a C++17 version of the above
    add_executable(gi-test-17 test/main.cpp)
    target_link_libraries(gi-test-17 PRIVATE ltest)
    set_property(TARGET gi-test-17 PROPERTY CXX_STANDARD 17)
    add_test(NAME gi-test-17 COMMAND gi-test-17)
endif ()

## EXAMPLES ##

# generated wrappers' dir
set (GENERATED_DIR_DEFAULT "/tmp/gi")
set (EXAMPLES_LIBS "")
if (NOT GENERATED_DIR)
    set (GENERATED_DIR ${GENERATED_DIR_DEFAULT})
endif ()
# arguments to generator
if (NOT DEFINED GENERATED_ARGS_DEFAULT)
    set(GENERATED_ARGS_DEFAULT --class --class-full --call-args 0 --basic-collection)
endif ()
if (NOT DEFINED GENERATED_ARGS_EXTRA)
    set(GENERATED_ARGS_EXTRA "")
endif ()
set (GENERATED_ARGS ${GENERATED_ARGS_DEFAULT} ${GENERATED_ARGS_EXTRA})

set(EXAMPLE_TARGETS "")
set(EXAMPLE_NS "")
# if both --dl and --expected are enabled,
# all function return value become gi::result<>
# to keep examples straight and simple,
# only few of them are adjusted to handle that scenario
set(PLAIN_API ON)
if ("${GENERATED_ARGS}" MATCHES "--dl")
    set (EXAMPLES_LIBS ${CMAKE_DL_LIBS})
    if ("${GENERATED_ARGS}" MATCHES "--expected")
        set(PLAIN_API OFF)
    endif ()
endif ()

if (BUILD_EXAMPLES)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mxgot" NEEDS_LARGE_GOT)
endif ()

if (GOBJECT_FOUND)
    add_executable(example-gobject EXCLUDE_FROM_ALL examples/gobject.cpp)
    target_compile_options(example-gobject PRIVATE ${GOBJECT_CFLAGS})
    target_link_libraries(example-gobject PRIVATE ${GOBJECT_LDFLAGS})
    set_property(TARGET example-gobject PROPERTY CXX_STANDARD 14)

    message(STATUS "adding GObject example")
    set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} example-gobject)
    set(EXAMPLE_NS ${EXAMPLE_NS} GObject-2.0)
endif ()

if (GIO_FOUND)
    add_executable(example-gio EXCLUDE_FROM_ALL examples/gio.cpp)
    target_compile_options(example-gio PRIVATE ${GIO_CFLAGS})
    target_link_libraries(example-gio PRIVATE ${GIO_LDFLAGS})
    set_property(TARGET example-gio PROPERTY CXX_STANDARD 17)

    message(STATUS "adding Gio communication example")
    set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} example-gio)
endif ()

if (GIO_FOUND)
    add_executable(example-gio-dbus-client EXCLUDE_FROM_ALL examples/gio-dbus-client.cpp)
    target_compile_options(example-gio-dbus-client PRIVATE ${GIO_CFLAGS})
    target_link_libraries(example-gio-dbus-client PRIVATE ${GIO_LDFLAGS})

    message(STATUS "adding Gio dbus example")
    set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} example-gio-dbus-client)
    set(EXAMPLE_NS ${EXAMPLE_NS} Gio-2.0)
endif ()

if (GIO_FOUND AND PLAIN_API)
    find_package(Boost 1.65 COMPONENTS fiber)
    if (Boost_FOUND)
        # no import target; multiple calls do not override first call targets
        add_executable(example-gio-async EXCLUDE_FROM_ALL examples/gio-async.cpp)
        target_include_directories(example-gio-async PRIVATE ${Boost_INCLUDE_DIRS})
        target_compile_options(example-gio-async PRIVATE ${GIO_CFLAGS})
        target_link_libraries(example-gio-async PRIVATE ${GIO_LDFLAGS} ${Boost_LIBRARIES})

        message(STATUS "adding Gio async example")
        set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} example-gio-async)
    else ()
        set(GIO_ASYNC_EXAMPLE_TARGET "")
        message(STATUS "disabling Gio async example")
    endif ()
endif ()

if (GIO_FOUND AND HAVE_CORO AND PLAIN_API)
    add_executable(example-gio-async-co EXCLUDE_FROM_ALL examples/gio-async-co.cpp)
    target_compile_options(example-gio-async-co PRIVATE ${GIO_CFLAGS})
    target_link_libraries(example-gio-async-co PRIVATE ${GIO_LDFLAGS})
    set_property(TARGET example-gio-async-co PROPERTY CXX_STANDARD 20)

    message(STATUS "adding Gio async coroutine example")
    set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} example-gio-async-co)
    set(EXAMPLE_NS ${EXAMPLE_NS} Gio-2.0)
endif ()

if (GST_FOUND AND PLAIN_API)
    add_executable(example-gst EXCLUDE_FROM_ALL examples/gst.cpp)
    # add generated files
    foreach (GENSRC IN ITEMS ${GENERATED_DIR}/glib/glib.cpp
                ${GENERATED_DIR}/gst/gst.cpp ${GENERATED_DIR}/gobject/gobject.cpp)
        target_sources(example-gst PRIVATE ${GENSRC})
        set_property(SOURCE ${GENSRC} PROPERTY GENERATED true)
    endforeach ()
    target_link_libraries(example-gst PRIVATE ${GST_LDFLAGS})
    target_compile_options(example-gst PRIVATE ${GST_CFLAGS})
    # a lot of class methods are wrapped these days, so there is some overlap
    # however, no class implementation is used here, so arrange for suppression
    target_compile_definitions(example-gst PRIVATE GI_CLASS_IMPL_PRAGMA=1)
    set_property(TARGET example-gst PROPERTY CXX_STANDARD 14)
    if (NEEDS_LARGE_GOT)
        set_property(SOURCE ${GENERATED_DIR}/gst/gst.cpp APPEND PROPERTY COMPILE_OPTIONS "-mxgot")
    endif ()

    message(STATUS "adding Gst example")
    set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} example-gst)
    set(EXAMPLE_NS ${EXAMPLE_NS} Gst-1.0)

    function(add_gst_module_example TARGET_NAME)
        add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL examples/gst.cpp)
        # add generated files
        foreach (GENSRC IN ITEMS ${GENERATED_DIR}/gobject/gobject.cppm
            ${GENERATED_DIR}/gmodule/gmodule.cppm ${GENERATED_DIR}/gst/gst.cppm)
            target_sources(${TARGET_NAME} PRIVATE FILE_SET CXX_MODULES
                BASE_DIRS ${GENERATED_DIR} FILES ${GENSRC})
            set_property(SOURCE ${GENSRC} PROPERTY GENERATED true)
        endforeach ()
        target_link_libraries(${TARGET_NAME} PRIVATE ${GST_LDFLAGS})
        target_compile_options(${TARGET_NAME} PRIVATE ${GST_CFLAGS})
        target_compile_definitions(${TARGET_NAME} PRIVATE USE_GI_MODULE=1 GI_CLASS_IMPL_PRAGMA=1)
        set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 20)
        set_property(TARGET ${TARGET_NAME} PROPERTY CXX_SCAN_FOR_MODULES ON)
        if (NEEDS_LARGE_GOT)
            set_property(SOURCE ${GENERATED_DIR}/gst/gst.cppm APPEND PROPERTY COMPILE_OPTIONS "-mxgot")
        endif ()

        message(STATUS "adding Gst module example ${TARGET_NAME}")
        set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} ${TARGET_NAME} PARENT_SCOPE)
    endfunction ()

    # (separate) module variant, similar to case above
    # GCC fails to link this properly :-( (missing symbols in object files)
    if (CMAKE_VERSION GREATER_EQUAL 3.28 AND BUILD_EXAMPLES_MODULES AND
        (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR BUILD_EXAMPLES_FORCE))
        add_gst_module_example(example-gst-mod)
        add_gst_module_example(example-gst-mod-extern)
        target_compile_definitions(example-gst-mod-extern PRIVATE GI_MODULE_EXTERN=1)
    endif ()
endif ()

if (GTK_FOUND AND PLAIN_API)
    add_executable(example-gtk EXCLUDE_FROM_ALL examples/gtk.cpp examples/gtk-obj.cpp)
    target_compile_options(example-gtk PRIVATE ${GTK_CFLAGS})
    target_link_libraries(example-gtk PRIVATE ${GTK_LIBRARIES})
    target_compile_options(example-gtk PRIVATE -DEXAMPLES_DIR=${CMAKE_CURRENT_LIST_DIR}/examples)
    if (NEEDS_LARGE_GOT)
        set_property(SOURCE examples/gtk-obj.cpp APPEND PROPERTY COMPILE_OPTIONS "-mxgot")
    endif ()

    # sanity check
    if (NOT GTK_GIR)
        message(FATAL_ERROR "unknown Gtk GIR")
    endif ()
    message(STATUS "adding Gtk example ${GTK_GIR}")
    set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} example-gtk)
    set(EXAMPLE_NS ${EXAMPLE_NS} ${GTK_GIR})

    function(add_gtk_module_example TARGET_NAME)
        add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL)
        target_sources(${TARGET_NAME} PRIVATE examples/gtk.cpp
            PRIVATE FILE_SET CXX_MODULES BASE_DIRS ${GENERATED_DIR}
            FILES ${GENERATED_DIR}/gtk/gtk_rec.cppm)
        target_compile_options(${TARGET_NAME} PRIVATE ${GTK_CFLAGS})
        target_link_libraries(${TARGET_NAME} PRIVATE ${GTK_LIBRARIES})
        target_compile_options(${TARGET_NAME} PRIVATE -DUSE_GI_MODULE=1
            -DEXAMPLES_DIR=${CMAKE_CURRENT_LIST_DIR}/examples)
        set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 20)
        set_property(TARGET ${TARGET_NAME} PROPERTY CXX_SCAN_FOR_MODULES ON)
        set_property(SOURCE ${GENERATED_DIR}/gtk/gtk_rec.cppm PROPERTY GENERATED ON)
        if (NEEDS_LARGE_GOT)
            set_property(SOURCE ${GENERATED_DIR}/gtk/gtk_rec.cppm APPEND PROPERTY COMPILE_OPTIONS "-mxgot")
        endif ()

        message(STATUS "adding Gtk recursive module example ${TARGET_NAME}")
        set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} ${TARGET_NAME} PARENT_SCOPE)
    endfunction ()

    # (recursive) module variant, similar to non-module case above
    if (CMAKE_VERSION GREATER_EQUAL 3.28 AND BUILD_EXAMPLES_MODULES)
        add_gtk_module_example(example-gtk-mod)
        add_gtk_module_example(example-gtk-mod-extern)
        target_compile_definitions(example-gtk-mod-extern PRIVATE GI_MODULE_EXTERN=1)
    endif ()

endif ()

# optional Qt example
if (BUILD_EXAMPLES)
    find_package(Qt5Core 5.9)
endif ()
if (Qt5Core_FOUND AND GIO_FOUND AND PLAIN_API)
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
    add_executable(example-gio-qt-async EXCLUDE_FROM_ALL examples/gio-qt-async.cpp)
    target_compile_options(example-gio-qt-async PRIVATE ${GIO_CFLAGS})
    target_link_libraries(example-gio-qt-async PRIVATE ${GIO_LDFLAGS} Qt5::Core)
    set_target_properties(example-gio-qt-async PROPERTIES AUTOMOC ON)

    message(STATUS "adding Qt GIO async example")
    set(EXAMPLE_TARGETS ${EXAMPLE_TARGETS} example-gio-qt-async)
endif ()

add_custom_command(OUTPUT ${GENERATED_DIR}
    COMMENT "Generating wrapper code for: ${EXAMPLE_NS}"
    DEPENDS cppgir ${GI_IGNORE_FILE_DIR}/${GI_IGNORE_FILE} ${GI_IGNORE_FILE_DIR}/${GI_IGNORE_FILE_PLATFORM}
    COMMAND cppgir ${GENERATED_ARGS} ${GENERATED_IGNORE}
        --output ${GENERATED_DIR} ${EXAMPLE_NS}
    COMMAND cmake -E touch_nocreate ${GENERATED_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

# example might have been added if dependencies found
# make sure to disable as needed
if (NOT BUILD_EXAMPLES)
    set(EXAMPLE_TARGETS "")
endif ()

message(STATUS "example programs: ${EXAMPLE_TARGETS}")
add_custom_target(examples)
if (EXAMPLE_TARGETS)
    add_dependencies(examples ${EXAMPLE_TARGETS})
endif ()

add_custom_target(wrappers DEPENDS ${GENERATED_DIR})
foreach (example ${EXAMPLE_TARGETS})
    target_link_libraries(${example} PRIVATE gi ${EXAMPLES_LIBS})
    target_include_directories(${example} PRIVATE ${GENERATED_DIR})
    target_compile_options(${example} PRIVATE ${BUILD_EXAMPLES_OPTIONS})
    add_dependencies(${example} wrappers)
endforeach ()


## INSTALL ##

# manpage processor
find_program(RONN ronn DOC "ronn markdown man page processor")
if (${RONN} STREQUAL "RONN-NOTFOUND")
    message(STATUS "ronn manpage processor not found; not building manpage")
elseif (BUILD_DOC)
    message(STATUS "building manpage")
    add_custom_command(OUTPUT cppgir.1
        COMMAND ${RONN} --roff --pipe ${CMAKE_CURRENT_LIST_DIR}/docs/cppgir.md > cppgir.1
        DEPENDS docs/cppgir.md
        WORKING_DIRECTORY .)
    add_custom_target(manpages ALL DEPENDS cppgir.1)
endif()

endif ()  # BUILD_TOOLS

# headers
install(DIRECTORY gi override
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
if (INTERNAL_EXPECTED)
    install(DIRECTORY ${EXPECTED_LITE_INCLUDE}/nonstd
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/gi)
endif ()

# doc
install(FILES README.md docs/cppgir.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY examples
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    PATTERN external EXCLUDE)
if (TARGET manpages)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cppgir.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()

# pkgconfig
set(PKG_CONFIG "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.pc")
set(PKG_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
# configure pkg config file
configure_file("cmake/cppgir.pc.in" "${PKG_CONFIG}" @ONLY)
install(FILES "${PKG_CONFIG}"
    DESTINATION "${PKG_CONFIG_INSTALL_DIR}")

# cmake EXPORTS
set(CONFIG_PACKAGE_LOCATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
set(CONFIG_VERSION_NAME ${PROJECT_NAME}-config-version.cmake)
set(CONFIG_TARGETS_NAME ${PROJECT_NAME}-targets.cmake)
set(CONFIG_NAME ${PROJECT_NAME}-config.cmake)
if (INTERNAL_EXPECTED OR NOT expected-lite_FOUND)
    set(CONFIG_NAME_IN ${CONFIG_NAME})
else ()
    set(CONFIG_NAME_IN ${PROJECT_NAME}-config-deps.cmake)
endif ()
set(TARGETS_EXPORT_NAME CppGirTargets)

if (BUILD_TOOLS)

# generator
install(TARGETS cppgir
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    EXPORT "${TARGETS_EXPORT_NAME}")

# ignore file
if (NOT BUILD_EMBED_IGNORE)
    install(FILES ${GI_IGNORE_FILE_DIR}/${GI_IGNORE_FILE} ${GI_IGNORE_FILE_DIR}/${GI_IGNORE_FILE_PLATFORM}
        DESTINATION ${GI_IGNORE_FILE_INSTALL_DIR})
endif()

endif ()  # BUILD_TOOLS

# headers
install(TARGETS gi EXPORT "${TARGETS_EXPORT_NAME}")

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${CONFIG_VERSION_NAME}"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${CONFIG_VERSION_NAME}"
    DESTINATION ${CONFIG_PACKAGE_LOCATION}
)

install(FILES cmake/${CONFIG_NAME_IN} RENAME ${CONFIG_NAME}
    DESTINATION ${CONFIG_PACKAGE_LOCATION}
)

export(EXPORT ${TARGETS_EXPORT_NAME}
    FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/${CONFIG_TARGETS_NAME}"
    NAMESPACE CppGir::
)

install(EXPORT ${TARGETS_EXPORT_NAME}
  FILE ${CONFIG_TARGETS_NAME}
  NAMESPACE CppGir::
  DESTINATION ${CONFIG_PACKAGE_LOCATION}
)

# uninstall target;
# intermediate directories are not removed though
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake-uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

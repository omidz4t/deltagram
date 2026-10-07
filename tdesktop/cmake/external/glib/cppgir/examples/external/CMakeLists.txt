cmake_minimum_required(VERSION 3.16.0 FATAL_ERROR)
project(cppgir_example VERSION 2.0.0)

# following example uses the Gio-2.0 gir
# will generate code for that

# locate cppgir

# NOTE recent cmake version have more options and automagic find_package integration
# (see https://cmake.org/cmake/help/latest/guide/using-dependencies/index.html
# or https://cmake.org/cmake/help/latest/module/FetchContent.html)
# but a slightly older approach is used here
find_package(cppgir ${CMAKE_PROJECT_VERSION} QUIET)
if (cppgir_FOUND)
    # ok
    message(STATUS "using system cppgir")
elseif (CMAKE_VERSION GREATER_EQUAL 3.22)
    # older version fail processing the subdirectory below
    message(STATUS "cppgir not found, fetching instead")
    # of course, alternatively,
    # use any other method to include cppgir (e.g. git submodule)
    # which can also be done unconditionally (without trying find_package first)
    include(FetchContent)
    FetchContent_Declare(
      cppgir
      GIT_REPOSITORY https://gitlab.com/mnauw/cppgir.git
      GIT_TAG        master
      GIT_SHALLOW    TRUE
      GIT_SUBMODULES_RECURSE TRUE
      # disable auto-invoke of add_subdirectory() below
      SOURCE_SUBDIR  data
    )
    # no CMakeLists in specified subdir, so we handle it explicitly
    FetchContent_MakeAvailable(cppgir)
    set(CPPGIR_DIR "${cppgir_SOURCE_DIR}")
else ()
    # obviously, actual mileage/location may vary (e.g. using git submodule)
    set(CPPGIR_DIR "../..")
endif ()

if (CPPGIR_DIR)
    # adjust options
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_EMBED_IGNORE ON)
    add_subdirectory(${CPPGIR_DIR} cppgir EXCLUDE_FROM_ALL)
endif ()

set(EXAMPLE_NS "Gio-2.0")
# of course, can be anywhere, e.g. inside/outside the build directory
# set(GENERATED_DIR "/tmp/gi.example")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(OUTPUT ${GENERATED_DIR}
    COMMENT "Generating wrapper code for: ${EXAMPLE_NS}"
    DEPENDS CppGir::cppgir
    COMMAND CppGir::cppgir --output ${GENERATED_DIR} ${EXAMPLE_NS}
    COMMAND cmake -E touch_nocreate ${GENERATED_DIR}
)
add_custom_target(generate DEPENDS ${GENERATED_DIR})

# need the code and libs and especially GIRs
include(FindPkgConfig)
pkg_check_modules(GOBJECT REQUIRED IMPORTED_TARGET gobject-2.0)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

# (actually) needs no generated code, but it needs basic libs
add_executable(ext-gobject src/ext-gobject.cpp)
target_link_libraries(ext-gobject PRIVATE CppGir::gi PkgConfig::GOBJECT)

# (really) needs generated code
add_executable(ext-gio src/ext-gio.cpp)
target_link_libraries(ext-gio PRIVATE CppGir::gi PkgConfig::GIO)
target_include_directories(ext-gio PRIVATE ${GENERATED_DIR})
add_dependencies(ext-gio generate)

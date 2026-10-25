cmake_minimum_required(VERSION 3.16)
project(delta_shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_executable(delta_shell
    main.cpp
    delta_rpc.cpp
    delta_rpc.h
    shell.cpp
    shell.h
)

target_link_libraries(delta_shell PRIVATE Qt6::Core Qt6::Widgets)

if (NOT MSVC)
    target_compile_options(delta_shell PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

include(CTest)
if (BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(delta_rpc_fixture tests/rpc_fixture.cpp)
    target_link_libraries(delta_rpc_fixture PRIVATE Qt6::Core)
    add_executable(delta_integrity_tests
        tests/integrity_tests.cpp backup_transfer.cpp compose_batch.cpp compose_queue.cpp compose_queue_sender.cpp delta_rpc.cpp draft_store.cpp history_mutations.cpp profile_activity.cpp reply_store.cpp)
    target_include_directories(delta_integrity_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(delta_integrity_tests PRIVATE Qt6::Core Qt6::Test)
    target_compile_definitions(delta_integrity_tests PRIVATE
        RPC_FIXTURE_PATH="$<TARGET_FILE:delta_rpc_fixture>")
    add_dependencies(delta_integrity_tests delta_rpc_fixture)
    add_test(NAME delta_integrity COMMAND delta_integrity_tests)
    set_tests_properties(delta_integrity PROPERTIES TIMEOUT 90)
endif()

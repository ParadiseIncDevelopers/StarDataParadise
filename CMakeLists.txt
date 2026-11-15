cmake_minimum_required(VERSION 4.2)
project(StarDataParadise VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include(CTest)
include(cmake/NativeSdk.cmake)
find_package(Qt6 6.11.3 EXACT CONFIG REQUIRED COMPONENTS Widgets Network)

add_library(workbench_ui STATIC StarDataParadise/workbench.cpp StarDataParadise/profiles.cpp)
target_include_directories(workbench_ui PUBLIC StarDataParadise)
target_compile_definitions(workbench_ui PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(workbench_ui PRIVATE /utf-8)
target_link_libraries(workbench_ui PUBLIC
    Qt6::Widgets Qt6::Network StarDataParadise::StarDataParadiseDLLler advapi32)

qt_add_executable(StarDataParadise WIN32 StarDataParadise/StarDataParadise.cpp StarDataParadise/StarDataParadise.rc)
target_link_libraries(StarDataParadise PRIVATE workbench_ui)

get_target_property(qt_core_release Qt6::Core IMPORTED_LOCATION_RELEASE)
get_filename_component(qt_bin "${qt_core_release}" DIRECTORY)
find_program(WINDEPLOYQT NAMES windeployqt HINTS "${qt_bin}" NO_DEFAULT_PATH REQUIRED)
add_custom_command(TARGET StarDataParadise POST_BUILD
    COMMAND "${WINDEPLOYQT}" --no-translations --no-compiler-runtime "$<TARGET_FILE:StarDataParadise>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:StarDataParadise::StarDataParadiseDLLler>" "$<TARGET_FILE_DIR:StarDataParadise>"
    VERBATIM)

if(BUILD_TESTING)
    add_test(NAME disconnected_window COMMAND StarDataParadise --smoke-test)
    set_tests_properties(disconnected_window PROPERTIES TIMEOUT 15)

    set(SDP_TEST_SERVER "" CACHE FILEPATH "Server EXE for Workbench TLS integration")
    set(SDP_TEST_OPENSSL "" CACHE FILEPATH "OpenSSL tool for local test certificates")
    if(SDP_TEST_SERVER OR SDP_TEST_OPENSSL)
        if(NOT EXISTS "${SDP_TEST_SERVER}" OR NOT EXISTS "${SDP_TEST_OPENSSL}")
            message(FATAL_ERROR "Workbench TLS test needs both SDP_TEST_SERVER and SDP_TEST_OPENSSL.")
        endif()
        find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
        add_executable(workbench_connection_smoke tests/connection_smoke.cpp)
        target_link_libraries(workbench_connection_smoke PRIVATE workbench_ui)
        add_dependencies(workbench_connection_smoke StarDataParadise)
        add_test(NAME workbench_connections COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_smoke.py"
            --client "$<TARGET_FILE:workbench_connection_smoke>" --server "${SDP_TEST_SERVER}"
            --openssl "${SDP_TEST_OPENSSL}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/instances")
        set_tests_properties(workbench_connections PROPERTIES TIMEOUT 90)
    endif()
endif()

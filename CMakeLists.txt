cmake_minimum_required(VERSION 4.2)
project(StarDataParadise VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include(CTest)
include(cmake/NativeSdk.cmake)
find_package(Qt6 6.11.3 EXACT CONFIG REQUIRED COMPONENTS Widgets Network)

add_library(workbench_ui STATIC
    StarDataParadise/workbench.cpp StarDataParadise/profiles.cpp
    StarDataParadise/sql_pane.cpp StarDataParadise/sql_session.cpp
    StarDataParadise/sql_editor.cpp StarDataParadise/sql_script.cpp StarDataParadise/sql_definition.cpp
    StarDataParadise/data_grid.cpp StarDataParadise/table_design.cpp StarDataParadise/table_designer.cpp
    StarDataParadise/table_codec.cpp StarDataParadise/table_transfer.cpp
    StarDataParadise/routine_studio.cpp StarDataParadise/routine_debugger.cpp
    StarDataParadise/routine_export.cpp StarDataParadise/sdk_examples.cpp
    StarDataParadise/history_studio.cpp StarDataParadise/decision_studio.cpp
    StarDataParadise/ml_studio.cpp StarDataParadise/dataset_studio.cpp StarDataParadise/job_center.cpp
    StarDataParadise/admin_center.cpp StarDataParadise/backup_center.cpp
    StarDataParadise/security_center.cpp StarDataParadise/workbench_help.cpp
    StarDataParadise/compare_studio.cpp StarDataParadise/migration_studio.cpp
    StarDataParadise/migration_plan.cpp StarDataParadise/migration_source.cpp
    StarDataParadise/update_status.cpp)
target_include_directories(workbench_ui PUBLIC StarDataParadise)
target_compile_definitions(workbench_ui PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(workbench_ui PRIVATE /utf-8)
target_link_libraries(workbench_ui PUBLIC
    Qt6::Widgets Qt6::Network StarDataParadise::StarDataParadiseDLLler advapi32)

qt_add_executable(StarDataParadise WIN32 StarDataParadise/StarDataParadise.cpp StarDataParadise/StarDataParadise.rc)
target_link_libraries(StarDataParadise PRIVATE workbench_ui)

add_executable(StarDataParadiseMigrationSource
    StarDataParadise/migration_source_main.cpp StarDataParadise/migration_source_driver.cpp)
target_compile_definitions(StarDataParadiseMigrationSource PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(StarDataParadiseMigrationSource PRIVATE /utf-8)
target_link_libraries(StarDataParadiseMigrationSource PRIVATE Qt6::Core)
add_dependencies(StarDataParadise StarDataParadiseMigrationSource)

get_target_property(qt_core_release Qt6::Core IMPORTED_LOCATION_RELEASE)
get_filename_component(qt_bin "${qt_core_release}" DIRECTORY)
find_program(WINDEPLOYQT NAMES windeployqt HINTS "${qt_bin}" NO_DEFAULT_PATH REQUIRED)
add_custom_command(TARGET StarDataParadise POST_BUILD
    COMMAND "${WINDEPLOYQT}" --no-translations --no-compiler-runtime "$<TARGET_FILE:StarDataParadise>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:StarDataParadise::StarDataParadiseDLLler>" "$<TARGET_FILE_DIR:StarDataParadise>"
    VERBATIM)

if(BUILD_TESTING)
    # Use the pinned Qt offscreen plugin for automated GUI tests.
    set(qt_test_platforms "${qt_bin}/../plugins/platforms")
    if(NOT EXISTS "${qt_test_platforms}/qoffscreen.dll")
        message(FATAL_ERROR "The pinned Qt SDK needs qoffscreen.dll for Workbench tests.")
    endif()
    set(workbench_test_environment
        "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${qt_test_platforms}")
    add_executable(workbench_transfer_codec tests/table_transfer_codec.cpp)
    target_link_libraries(workbench_transfer_codec PRIVATE workbench_ui)
    add_dependencies(workbench_transfer_codec StarDataParadise)
    add_test(NAME workbench_transfer_codec COMMAND workbench_transfer_codec)
    set_tests_properties(workbench_transfer_codec PROPERTIES TIMEOUT 15 ENVIRONMENT "${workbench_test_environment}")

    add_test(NAME disconnected_window COMMAND StarDataParadise --smoke-test)
    set_tests_properties(disconnected_window PROPERTIES TIMEOUT 15 ENVIRONMENT "${workbench_test_environment}")
    add_executable(workbench_update_status tests/update_status.cpp)
    target_link_libraries(workbench_update_status PRIVATE workbench_ui)
    add_dependencies(workbench_update_status StarDataParadise)
    add_test(NAME workbench_update_status COMMAND workbench_update_status)
    set_tests_properties(workbench_update_status PROPERTIES TIMEOUT 15 ENVIRONMENT "${workbench_test_environment}")

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
        set_tests_properties(workbench_connections PROPERTIES TIMEOUT 90 ENVIRONMENT "${workbench_test_environment}")
        add_executable(workbench_sql_smoke tests/sql_workbench_smoke.cpp)
        target_link_libraries(workbench_sql_smoke PRIVATE workbench_ui)
        add_dependencies(workbench_sql_smoke StarDataParadise)
        add_test(NAME workbench_sql COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_sql_smoke.py"
            --client "$<TARGET_FILE:workbench_sql_smoke>" --server "${SDP_TEST_SERVER}"
            --openssl "${SDP_TEST_OPENSSL}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/sql-instances")
        set_tests_properties(workbench_sql PROPERTIES TIMEOUT 180 ENVIRONMENT "${workbench_test_environment}")

        foreach(widget IN ITEMS table_designer table_grid table_transfer history_studio decision_studio)
            set(target "workbench_${widget}_smoke")
            add_executable(${target} "tests/${widget}_smoke.cpp")
            target_link_libraries(${target} PRIVATE workbench_ui)
            add_dependencies(${target} StarDataParadise)
            add_test(NAME ${target} COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_grid_smoke.py"
                --client "$<TARGET_FILE:${target}>" --server "${SDP_TEST_SERVER}"
                --openssl "${SDP_TEST_OPENSSL}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/${widget}-instances")
            set_tests_properties(${target} PROPERTIES TIMEOUT 120 ENVIRONMENT "${workbench_test_environment}")
        endforeach()

        add_executable(workbench_routine_studio_smoke tests/routine_studio_smoke.cpp)
        target_link_libraries(workbench_routine_studio_smoke PRIVATE workbench_ui)
        add_dependencies(workbench_routine_studio_smoke StarDataParadise)
        add_test(NAME workbench_routine_studio COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_routine_smoke.py"
            --client "$<TARGET_FILE:workbench_routine_studio_smoke>" --server "${SDP_TEST_SERVER}"
            --openssl "${SDP_TEST_OPENSSL}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/routine-studio-instances")
        set_tests_properties(workbench_routine_studio PROPERTIES TIMEOUT 180 ENVIRONMENT "${workbench_test_environment}")

        add_executable(workbench_ml_studio_smoke tests/ml_studio_smoke.cpp)
        target_link_libraries(workbench_ml_studio_smoke PRIVATE workbench_ui)
        add_dependencies(workbench_ml_studio_smoke StarDataParadise)
        add_test(NAME workbench_ml_studio COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_ml_smoke.py"
            --client "$<TARGET_FILE:workbench_ml_studio_smoke>" --server "${SDP_TEST_SERVER}"
            --openssl "${SDP_TEST_OPENSSL}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/ml-studio-instances")
        set_tests_properties(workbench_ml_studio PROPERTIES TIMEOUT 300 ENVIRONMENT "${workbench_test_environment}")

        add_executable(workbench_compare_studio_smoke tests/compare_studio_smoke.cpp)
        target_link_libraries(workbench_compare_studio_smoke PRIVATE workbench_ui)
        add_dependencies(workbench_compare_studio_smoke StarDataParadise)
        add_test(NAME workbench_compare_studio COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_compare_smoke.py"
            --client "$<TARGET_FILE:workbench_compare_studio_smoke>" --server "${SDP_TEST_SERVER}"
            --openssl "${SDP_TEST_OPENSSL}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/instances")
        set_tests_properties(workbench_compare_studio PROPERTIES TIMEOUT 180 ENVIRONMENT "${workbench_test_environment}")

        set(SDP_TEST_CLI "" CACHE FILEPATH "CLI EXE for Workbench administration acceptance")
        if(SDP_TEST_CLI)
            if(NOT EXISTS "${SDP_TEST_CLI}")
                message(FATAL_ERROR "The administration test needs a built CLI executable.")
            endif()
            add_executable(workbench_admin_studio_smoke tests/admin_studio_smoke.cpp)
            target_link_libraries(workbench_admin_studio_smoke PRIVATE workbench_ui)
            add_dependencies(workbench_admin_studio_smoke StarDataParadise)
            add_test(NAME workbench_admin_studio COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_admin_smoke.py"
                --client "$<TARGET_FILE:workbench_admin_studio_smoke>" --server "${SDP_TEST_SERVER}"
                --openssl "${SDP_TEST_OPENSSL}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/admin-studio-instances"
                --cli "${SDP_TEST_CLI}")
            set_tests_properties(workbench_admin_studio PROPERTIES TIMEOUT 300 ENVIRONMENT "${workbench_test_environment}")
        endif()

        set(SDP_TEST_POSTGRESQL "" CACHE PATH "Local PostgreSQL installation for migration acceptance")
        set(SDP_TEST_MYSQL "" CACHE PATH "Local MySQL installation for migration acceptance")
        if(SDP_TEST_POSTGRESQL OR SDP_TEST_MYSQL)
            if(NOT EXISTS "${SDP_TEST_POSTGRESQL}/bin/initdb.exe" OR
                NOT EXISTS "${SDP_TEST_POSTGRESQL}/bin/postgres.exe" OR
                NOT EXISTS "${SDP_TEST_POSTGRESQL}/bin/pg_ctl.exe" OR
                NOT EXISTS "${SDP_TEST_POSTGRESQL}/bin/psql.exe" OR
                NOT EXISTS "${SDP_TEST_POSTGRESQL}/bin/libpq.dll" OR
                NOT EXISTS "${SDP_TEST_MYSQL}/bin/mysqld.exe" OR
                NOT EXISTS "${SDP_TEST_MYSQL}/bin/mysql.exe" OR
                NOT EXISTS "${SDP_TEST_MYSQL}/bin/mysqladmin.exe" OR
                NOT EXISTS "${SDP_TEST_MYSQL}/lib/libmysql.dll")
                message(FATAL_ERROR "Migration acceptance needs installed PostgreSQL and MySQL servers, clients and drivers.")
            endif()
            add_executable(workbench_migration_studio_smoke tests/migration_studio_smoke.cpp)
            target_compile_options(workbench_migration_studio_smoke PRIVATE /utf-8)
            target_link_libraries(workbench_migration_studio_smoke PRIVATE workbench_ui)
            add_dependencies(workbench_migration_studio_smoke StarDataParadise StarDataParadiseMigrationSource)
            add_test(NAME workbench_migration_studio COMMAND Python3::Interpreter -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_migration_smoke.py"
                --client "$<TARGET_FILE:workbench_migration_studio_smoke>" --server "${SDP_TEST_SERVER}"
                --openssl "${SDP_TEST_OPENSSL}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/migration-instances"
                --postgresql "${SDP_TEST_POSTGRESQL}" --mysql "${SDP_TEST_MYSQL}")
            set_tests_properties(workbench_migration_studio PROPERTIES TIMEOUT 600 ENVIRONMENT "${workbench_test_environment}")
        endif()

        set(SDP_TEST_EXPORT_TOOLKIT "" CACHE PATH "Local Stage 35 routine exporter toolkit for the Java test")
        set(SDP_TEST_JDK "" CACHE PATH "Local JDK home for the Java export test")
        set(SDP_TEST_JDBC "" CACHE FILEPATH "Pinned StarDataParadise JDBC runtime JAR")
        set(SDP_TEST_GSON "" CACHE FILEPATH "Pinned Gson runtime JAR")
        if(SDP_TEST_EXPORT_TOOLKIT OR SDP_TEST_JDK OR SDP_TEST_JDBC OR SDP_TEST_GSON)
            if(NOT EXISTS "${SDP_TEST_EXPORT_TOOLKIT}/snapshot.py" OR NOT EXISTS "${SDP_TEST_JDK}/bin/javac.exe"
                OR NOT EXISTS "${SDP_TEST_JDBC}" OR NOT EXISTS "${SDP_TEST_GSON}")
                message(FATAL_ERROR "The Java export test needs the Stage 35 toolkit, JDK, JDBC and Gson.")
            endif()
            add_executable(workbench_routine_export_smoke tests/routine_export_smoke.cpp)
            target_link_libraries(workbench_routine_export_smoke PRIVATE workbench_ui)
            add_dependencies(workbench_routine_export_smoke StarDataParadise)
            add_test(NAME workbench_routine_export COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/workbench_java_export_smoke.py"
                --client "$<TARGET_FILE:workbench_routine_export_smoke>" --server "${SDP_TEST_SERVER}"
                --openssl "${SDP_TEST_OPENSSL}" --scratch "${CMAKE_CURRENT_BINARY_DIR}/routine-export-instances"
                --toolkit "${SDP_TEST_EXPORT_TOOLKIT}" --jdk "${SDP_TEST_JDK}"
                --jdbc "${SDP_TEST_JDBC}" --gson "${SDP_TEST_GSON}")
            set_tests_properties(workbench_routine_export PROPERTIES TIMEOUT 180 ENVIRONMENT "${workbench_test_environment}")
        endif()
    endif()
endif()

cmake_minimum_required(VERSION 3.28)

project(PrismaticLauncher VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Concurrent Network)
find_package(LibArchive REQUIRED)
qt_standard_project_setup()

qt_add_library(launcher-core STATIC
    src/core/AtomicFile.cpp src/core/AtomicFile.h
    src/core/DataPaths.cpp src/core/DataPaths.h
    src/core/InstanceStore.cpp src/core/InstanceStore.h
    src/core/GameLauncher.cpp src/core/GameLauncher.h
    src/core/ModManager.cpp src/core/ModManager.h
    src/core/DependencyResolver.cpp src/core/DependencyResolver.h
    src/core/ModInventory.cpp src/core/ModInventory.h
    src/core/ModSource.cpp src/core/ModSource.h
)
target_include_directories(launcher-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
target_link_libraries(launcher-core PUBLIC Qt6::Core PRIVATE LibArchive::LibArchive Qt6::Network)
qt_add_resources(launcher-core dependency-catalog
    PREFIX "/prismatic/catalog" BASE packaging/catalog
    FILES packaging/catalog/sources.json
)

function(prismatic_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
prismatic_warnings(launcher-core)

add_subdirectory(prismatic-launcher/src/qt)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(data-paths-tests tests/core/DataPathsTests.cpp)
    target_link_libraries(data-paths-tests PRIVATE launcher-core Qt6::Test)
    prismatic_warnings(data-paths-tests)
    add_test(NAME data-paths COMMAND data-paths-tests)

    qt_add_executable(mod-inventory-tests tests/core/ModInventoryTests.cpp)
    target_link_libraries(mod-inventory-tests PRIVATE launcher-core Qt6::Test)
    prismatic_warnings(mod-inventory-tests)
    add_test(NAME mod-inventory COMMAND mod-inventory-tests)

    qt_add_executable(dependency-resolver-tests tests/core/DependencyResolverTests.cpp)
    target_link_libraries(dependency-resolver-tests PRIVATE launcher-core Qt6::Test)
    prismatic_warnings(dependency-resolver-tests)
    add_test(NAME dependency-resolver COMMAND dependency-resolver-tests)

    qt_add_executable(mod-source-tests tests/core/ModSourceTests.cpp)
    target_link_libraries(mod-source-tests PRIVATE launcher-core Qt6::Test LibArchive::LibArchive)
    prismatic_warnings(mod-source-tests)
    add_test(NAME mod-source COMMAND mod-source-tests)

    qt_add_executable(phase2-safety-tests tests/core/Phase2SafetyTests.cpp)
    target_link_libraries(phase2-safety-tests PRIVATE launcher-core Qt6::Test LibArchive::LibArchive)
    prismatic_warnings(phase2-safety-tests)
    add_test(NAME phase2-safety COMMAND phase2-safety-tests)

    qt_add_executable(theme-tests tests/ui/ThemeTests.cpp)
    target_link_libraries(theme-tests PRIVATE launcher-ui Qt6::Test)
    prismatic_warnings(theme-tests)
    add_test(NAME themes COMMAND theme-tests)
    set_tests_properties(themes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(mod-explorer-tests tests/ui/ModExplorerTests.cpp)
    target_link_libraries(mod-explorer-tests PRIVATE launcher-ui Qt6::Test)
    prismatic_warnings(mod-explorer-tests)
    add_test(NAME mod-explorer COMMAND mod-explorer-tests)
    set_tests_properties(mod-explorer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(phase2-integration-tests tests/ui/Phase2IntegrationTests.cpp)
    target_link_libraries(phase2-integration-tests PRIVATE launcher-ui Qt6::Test)
    prismatic_warnings(phase2-integration-tests)
    add_test(NAME phase2-integration COMMAND phase2-integration-tests)
    set_tests_properties(phase2-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(atomic-file-tests tests/core/AtomicFileTests.cpp)
    target_link_libraries(atomic-file-tests PRIVATE launcher-core Qt6::Test)
    prismatic_warnings(atomic-file-tests)
    add_test(NAME atomic-file COMMAND atomic-file-tests)

    qt_add_executable(launcher-ui-tests tests/ui/LauncherTests.cpp)
    target_link_libraries(launcher-ui-tests PRIVATE launcher-ui Qt6::Test LibArchive::LibArchive)
    prismatic_warnings(launcher-ui-tests)
    add_test(NAME launcher-ui COMMAND launcher-ui-tests)
    set_tests_properties(launcher-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(instance-store-tests tests/core/InstanceStoreTests.cpp)
    target_link_libraries(instance-store-tests PRIVATE launcher-core Qt6::Test)
    prismatic_warnings(instance-store-tests)
    add_test(NAME instance-store COMMAND instance-store-tests)

    qt_add_executable(game-launcher-tests tests/core/GameLauncherTests.cpp)
    target_link_libraries(game-launcher-tests PRIVATE launcher-core Qt6::Test)
    prismatic_warnings(game-launcher-tests)
    add_test(NAME game-launcher COMMAND game-launcher-tests)

    qt_add_executable(mod-manager-tests tests/core/ModManagerTests.cpp)
    target_link_libraries(mod-manager-tests PRIVATE launcher-core Qt6::Test LibArchive::LibArchive)
    prismatic_warnings(mod-manager-tests)
    add_test(NAME mod-manager COMMAND mod-manager-tests)

    qt_add_executable(launcher-reliability-tests tests/core/reliability_test.cpp)
    target_link_libraries(launcher-reliability-tests PRIVATE launcher-core Qt6::Test)
    prismatic_warnings(launcher-reliability-tests)
    add_test(NAME launcher-reliability COMMAND launcher-reliability-tests)

    file(MAKE_DIRECTORY
        "${CMAKE_BINARY_DIR}/tmp"
        "${CMAKE_BINARY_DIR}/tmp/home"
        "${CMAKE_BINARY_DIR}/tmp/data"
        "${CMAKE_BINARY_DIR}/tmp/config"
        "${CMAKE_BINARY_DIR}/tmp/cache"
    )
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND AND UNIX)
        add_test(NAME macos-packaging-policy COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${PROJECT_SOURCE_DIR}/packaging/macos" -p "test_*.py")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_program(PRISMATIC_DESKTOP_VALIDATOR desktop-file-validate)
            if(PRISMATIC_DESKTOP_VALIDATOR)
                add_test(NAME linux-packaging-policy COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
                    -s "${PROJECT_SOURCE_DIR}/packaging/linux" -p "test_*.py")
            endif()
        endif()
    endif()
    get_property(prismatic_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${prismatic_tests}
        PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 60)
    set_property(TEST ${prismatic_tests}
        APPEND PROPERTY ENVIRONMENT
        "TMPDIR=${CMAKE_BINARY_DIR}/tmp"
        "TMP=${CMAKE_BINARY_DIR}/tmp"
        "TEMP=${CMAKE_BINARY_DIR}/tmp"
        "HOME=${CMAKE_BINARY_DIR}/tmp/home"
        "USERPROFILE=${CMAKE_BINARY_DIR}/tmp/home"
        "XDG_DATA_HOME=${CMAKE_BINARY_DIR}/tmp/data"
        "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/tmp/config"
        "XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/tmp/cache")
endif()

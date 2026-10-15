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

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Concurrent)
find_package(LibArchive REQUIRED)
qt_standard_project_setup()

qt_add_library(launcher-core STATIC
    src/core/AtomicFile.cpp src/core/AtomicFile.h
    src/core/InstanceStore.cpp src/core/InstanceStore.h
    src/core/GameLauncher.cpp src/core/GameLauncher.h
    src/core/ModManager.cpp src/core/ModManager.h
)
target_include_directories(launcher-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
target_link_libraries(launcher-core PUBLIC Qt6::Core PRIVATE LibArchive::LibArchive)

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
    set_tests_properties(atomic-file launcher-ui instance-store game-launcher mod-manager launcher-reliability
        PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 60)
    set_property(TEST atomic-file launcher-ui instance-store game-launcher mod-manager launcher-reliability
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

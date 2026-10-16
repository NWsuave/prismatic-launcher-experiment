qt_add_library(launcher-ui STATIC
    LauncherConfig.cpp LauncherConfig.h
    LauncherWindow.cpp LauncherWindow.h
    InstancePages.cpp
    ModExplorerPage.cpp ModExplorerPage.h
    ThemeManager.cpp ThemeManager.h
    DependencyDialog.cpp DependencyDialog.h
)
target_include_directories(launcher-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(launcher-ui PUBLIC Qt6::Widgets launcher-core PRIVATE Qt6::Concurrent)
prismatic_warnings(launcher-ui)
qt_add_resources(launcher-ui ui-resources
    PREFIX "/ui"
    FILES style.qss config/defaults.ini
)
qt_add_resources(launcher-ui theme-resources
    PREFIX "/themes" BASE themes
    FILES themes/common.qss themes/prismatic-dark.qss themes/retro-valley.qss themes/retro-terminal.qss
)

qt_add_executable(prismatic-launch main.cpp)
target_link_libraries(prismatic-launch PRIVATE launcher-ui)
if(APPLE)
    target_link_options(prismatic-launch PRIVATE "-Wl,-headerpad_max_install_names")
endif()
prismatic_warnings(prismatic-launch)
set_target_properties(prismatic-launch PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.prismatic.launcher"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

install(TARGETS prismatic-launch
    BUNDLE DESTINATION . COMPONENT Runtime
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Runtime
)

if(APPLE)
    add_custom_target(package-macos
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/package-macos.sh" --build-dir "${CMAKE_BINARY_DIR}"
        USES_TERMINAL VERBATIM)
elseif(WIN32)
    add_custom_target(package-windows
        COMMAND pwsh -NoProfile -File "${PROJECT_SOURCE_DIR}/scripts/package-windows.ps1"
            -BuildDir "${CMAKE_BINARY_DIR}" -Config "$<CONFIG>"
        USES_TERMINAL VERBATIM)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/packaging/linux/qt-sdk-$<CONFIG>.json"
        CONTENT "{\"qmake\":\"$<TARGET_FILE:Qt6::qmake>\",\"core\":\"$<TARGET_FILE:Qt6::Core>\"}\n")
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/org.prismatic.launcher.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications" COMPONENT Runtime)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/org.prismatic.launcher.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps" COMPONENT Runtime)
    add_custom_target(package-linux
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/package-linux.sh" --build-dir "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
        USES_TERMINAL VERBATIM)
endif()

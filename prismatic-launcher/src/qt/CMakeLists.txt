qt_add_library(launcher-ui STATIC
    LauncherConfig.cpp LauncherConfig.h
    LauncherWindow.cpp LauncherWindow.h
    InstancePages.cpp
)
target_include_directories(launcher-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(launcher-ui PUBLIC Qt6::Widgets launcher-core PRIVATE Qt6::Concurrent)
prismatic_warnings(launcher-ui)
qt_add_resources(launcher-ui ui-resources
    PREFIX "/ui"
    FILES style.qss config/defaults.ini
)

qt_add_executable(prismatic-launch main.cpp)
target_link_libraries(prismatic-launch PRIVATE launcher-ui)
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
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

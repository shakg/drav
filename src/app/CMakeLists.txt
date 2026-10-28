# Editor logic, testable without a window.
add_library(drav_editor STATIC
    Editor.h Editor.cpp EditorCommands.cpp EditorProps.cpp EditorLibrary.cpp EditorComponents.cpp EditorPrecision.cpp EditorFind.cpp EditorSafety.cpp Scripting.h Scripting.cpp PropMaps.h Demo.cpp
    Tools.h Tools.cpp
    LayerModel.h LayerModel.cpp
    SvgImport.h SvgImport.cpp
)
target_include_directories(drav_editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(drav_editor PUBLIC drav_render Qt6::Gui Qt6::Qml)

qt_add_executable(drav
    main.cpp
    CanvasItem.h CanvasItem.cpp
)

set(DRAV_QML
    Main.qml Theme.qml Icon.qml Icons.js IconButton.qml NumberField.qml TextBox.qml Dropdown.qml Section.qml
    ColorArea.qml Checker.qml Swatch.qml PaintPopup.qml PaintList.qml EffectsSection.qml TextSection.qml
    ExportSection.qml DesignPanel.qml LayersPanel.qml PagesList.qml AssetsPanel.qml Toolbar.qml MainMenu.qml
    VariablePicker.qml StylePreview.qml StylePicker.qml StyleChip.qml StyleEditor.qml LocalAssets.qml VariablesWindow.qml
    VariableCell.qml ComponentPropsSection.qml FindBar.qml RenameDialog.qml VersionsWindow.qml
    InspectPanel.qml ScriptConsole.qml AppMenu.qml AppMenuItem.qml AppMenuSeparator.qml ThemedWindow.qml)
list(TRANSFORM DRAV_QML PREPEND "${PROJECT_SOURCE_DIR}/qml/")
set_source_files_properties("${PROJECT_SOURCE_DIR}/qml/Theme.qml" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(f IN LISTS DRAV_QML)
    get_filename_component(name "${f}" NAME)
    set_source_files_properties("${f}" PROPERTIES QT_RESOURCE_ALIAS "${name}")
endforeach()

qt_add_qml_module(drav
    URI Drav
    VERSION 1.0
    QML_FILES ${DRAV_QML}
)

target_link_libraries(drav PRIVATE drav_editor Qt6::Quick Qt6::QuickControls2 Qt6::OpenGL Qt6::Gui)
set_target_properties(drav PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)

# App icon, file association and desktop integration.
set(DRAV_RES "${PROJECT_SOURCE_DIR}/resources")
qt_add_resources(drav "app_icon" PREFIX "/icons" BASE "${DRAV_RES}/icons" FILES "${DRAV_RES}/icons/drav.png")
if(APPLE)
    set(icns "${DRAV_RES}/icons/drav.icns")
    set_source_files_properties("${icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(drav PRIVATE "${icns}")
    configure_file("${DRAV_RES}/macos/Info.plist.in" "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
    set_target_properties(drav PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
elseif(WIN32)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/drav.rc" "IDI_ICON1 ICON \"${DRAV_RES}/icons/drav.ico\"\n")
    target_sources(drav PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/drav.rc")
endif()

install(TARGETS drav RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
    install(FILES "${DRAV_RES}/linux/drav.desktop" DESTINATION share/applications)
    install(FILES "${DRAV_RES}/linux/drav.xml" DESTINATION share/mime/packages)
    install(FILES "${DRAV_RES}/icons/drav.png" DESTINATION share/icons/hicolor/512x512/apps)
    install(FILES "${DRAV_RES}/icons/drav.svg" DESTINATION share/icons/hicolor/scalable/apps)
endif()

# Copies Qt (libraries, plugins, QML modules) next to the app on install, so the install tree
# is self-contained: windeployqt on Windows, macdeployqt on macOS, Qt's own deployer on Linux.
# Turn off when packaging against a system Qt (distro packages). DRAV_PACKAGE runs its own deploy
# script (cmake/Packaging.cmake); a second one for the same target is a CMake generate error.
option(DRAV_DEPLOY_QT "Bundle Qt into the install tree" ON)
if(DRAV_DEPLOY_QT AND NOT DRAV_PACKAGE)
    # On Linux, libraries that must match the host (libc, GPU drivers, X11, D-Bus, fontconfig)
    # are left out even when the toolchain (e.g. conda) provides its own copies.
    set(host_libs "^/lib/" "^/lib64/" "^/usr/lib/" "^/usr/lib64/" "/ld-linux")
    foreach(name c m dl pthread rt resolv util GL EGL GLX GLdispatch OpenGL gbm drm X11 X11-xcb Xau Xdmcp xcb
            ICE SM dbus-1 fontconfig freetype expat uuid)
        list(APPEND host_libs "/lib${name}[.]so")
    endforeach()
    qt_generate_deploy_qml_app_script(TARGET drav OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR
        POST_EXCLUDE_REGEXES ${host_libs})
    install(SCRIPT "${deploy_script}")
endif()

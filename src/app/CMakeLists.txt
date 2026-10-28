# Editor logic, testable without a window.
add_library(drav_editor STATIC
    Editor.h Editor.cpp EditorCommands.cpp EditorProps.cpp EditorLibrary.cpp EditorComponents.cpp EditorPrecision.cpp EditorFind.cpp EditorSafety.cpp PropMaps.h Demo.cpp
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
    VariableCell.qml ComponentPropsSection.qml FindBar.qml RenameDialog.qml VersionsWindow.qml)
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
    NO_RESOURCE_TARGET_PATH
)

target_link_libraries(drav PRIVATE drav_editor Qt6::Quick Qt6::QuickControls2 Qt6::OpenGL Qt6::Gui)
set_target_properties(drav PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
install(TARGETS drav RUNTIME DESTINATION bin BUNDLE DESTINATION .)

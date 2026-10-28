qt_add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE drav_core Qt6::Test)
target_compile_definitions(tst_core PRIVATE DRAV_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME tst_core COMMAND tst_core)

qt_add_executable(tst_render tst_render.cpp)
target_link_libraries(tst_render PRIVATE drav_render Qt6::Test)
add_test(NAME tst_render COMMAND tst_render)

qt_add_executable(tst_editor tst_editor.cpp)
target_link_libraries(tst_editor PRIVATE drav_editor Qt6::Test)
target_compile_definitions(tst_editor PRIVATE DRAV_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME tst_editor COMMAND tst_editor -platform offscreen)

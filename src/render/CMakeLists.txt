add_library(drav_render STATIC
    Renderer.h Renderer.cpp
    Export.h Export.cpp
    Code.h Code.cpp
)
target_link_libraries(drav_render PUBLIC drav_core)

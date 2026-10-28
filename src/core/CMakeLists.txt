add_library(drav_core STATIC
    Types.h
    Node.h Node.cpp
    Document.h Document.cpp
    Layout.h Layout.cpp
    Geometry.h Geometry.cpp
    Text.h Text.cpp
    Serializer.h Serializer.cpp
    Library.h Library.cpp
    Variables.h Variables.cpp
    Libraries.h Libraries.cpp
    VectorNetwork.cpp
)
qt_add_resources(drav_core "fonts"
    PREFIX "/fonts"
    BASE "${PROJECT_SOURCE_DIR}/resources/fonts"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/fonts/InterVariable.ttf"
        "${PROJECT_SOURCE_DIR}/resources/fonts/InterVariable-Italic.ttf"
)
target_include_directories(drav_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(drav_core PUBLIC Qt6::Core Skia::Skia)

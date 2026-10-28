add_library(drav_core STATIC
    Types.h
    Node.h Node.cpp
    Document.h Document.cpp
    Layout.h Layout.cpp
    Geometry.h Geometry.cpp
    Text.h Text.cpp
    Serializer.h Serializer.cpp
    Library.h Library.cpp
    Color.h Color.cpp
    Variables.h Variables.cpp
    Libraries.h Libraries.cpp
    VectorNetwork.cpp
    Zip.h Zip.cpp
    FigImport.h FigImport.cpp
)
qt_add_resources(drav_core "fonts"
    PREFIX "/fonts"
    BASE "${PROJECT_SOURCE_DIR}/resources/fonts"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/fonts/InterVariable.ttf"
        "${PROJECT_SOURCE_DIR}/resources/fonts/InterVariable-Italic.ttf"
)
target_include_directories(drav_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
# .fig import: deflate and zstd decompression.
pkg_check_modules(FIG_DEPS REQUIRED IMPORTED_TARGET zlib libzstd)
target_link_libraries(drav_core PUBLIC Qt6::Core Skia::Skia PRIVATE PkgConfig::FIG_DEPS)

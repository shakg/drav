# Packaging

`scripts/package.sh [skia dir] [build dir]` configures a release build, runs the tests and calls
CPack. Under the hood:

```bash
cmake --install build --prefix out   # self-contained install tree
```

```bash
cd build && cpack                    # package for this platform
```

Installing runs Qt's deploy step (`qt_generate_deploy_qml_app_script`), which copies the Qt
libraries, plugins and QML modules the app uses next to it:

| Platform | Deploy tool | Package | Integration |
| --- | --- | --- | --- |
| Linux | Qt's generic deployer, `$ORIGIN` rpath | `.tar.gz` (`bin/`, `lib/`, `share/`) | `.desktop` file, `application/x-drav` MIME type, hicolor icons |
| macOS | `macdeployqt` | `.dmg` with `drav.app` | `Info.plist` with `.drav` document type, `.icns` icon, Finder open events |
| Windows | `windeployqt` | `.zip` | Embedded `.ico` icon |

On Linux, libraries that must match the host (glibc, GPU/GL drivers, X11/xcb core, D-Bus,
fontconfig, freetype) are not bundled. Configure with `-DDRAV_DEPLOY_QT=OFF` to package against a
system Qt (distro packages, Flatpak).

Not handled here: code signing and notarization (macOS), installer signing (Windows), and
non-Qt runtime libraries on Windows (zlib, zstd, libwebp from the package manager — copy their
DLLs next to `drav.exe`, or link them statically, e.g. with vcpkg's `x64-windows-static` triplet).

Icons: `resources/icons/drav.svg` is the source; `scripts/make-icons.py` (Pillow) regenerates the
PNG, ICO and ICNS.

# Self-contained .deb: drav and the Qt it was built with go under /opt/drav, so the package does not
# depend on the distro's Qt (Ubuntu 24.04 ships 6.4). Everything else (fontconfig, ICU, X11, ...) comes
# from the distro; dpkg-shlibdeps fills in those dependencies.
#
#   cmake -S . -B build -DDRAV_PACKAGE=ON -DCMAKE_INSTALL_PREFIX=/opt/drav ...
#   cmake --build build && cpack --config build/CPackConfig.cmake -B build/package
if(NOT DRAV_PACKAGE)
    return()
endif()
if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/opt/drav")
    message(FATAL_ERROR "DRAV_PACKAGE needs -DCMAKE_INSTALL_PREFIX=/opt/drav")
endif()

# Plugins for embedded/kiosk setups; a desktop app never loads them and they pull in libinput, tslib, gbm.
set(DRAV_PRUNED_PLUGINS
    platforms/libqeglfs.so platforms/libqlinuxfb.so platforms/libqminimalegl.so platforms/libqvnc.so
    platforms/libqvkkhrdisplay.so egldeviceintegrations generic)

# Qt deploys only default plugins; add input methods (dead keys, IME) and themes (native file dialogs).
set(DRAV_EXTRA_PLUGINS)
foreach(p QComposePlatformInputContextPlugin QIbusPlatformInputContextPlugin QXdgDesktopPortalThemePlugin QGtk3ThemePlugin)
    if(TARGET Qt6::${p})
        list(APPEND DRAV_EXTRA_PLUGINS Qt6::${p})
    endif()
endforeach()
qt_import_plugins(drav INCLUDE ${DRAV_EXTRA_PLUGINS})

qt_generate_deploy_qml_app_script(TARGET drav OUTPUT_SCRIPT drav_deploy_script NO_TRANSLATIONS)
# Fixed layout regardless of how the Qt install is laid out; the deploy script writes a matching qt.conf.
install(CODE "set(QT_DEPLOY_PLUGINS_DIR plugins)\nset(QT_DEPLOY_QML_DIR qml)")
install(SCRIPT "${drav_deploy_script}")
foreach(p IN LISTS DRAV_PRUNED_PLUGINS)
    install(CODE "file(REMOVE_RECURSE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/plugins/${p}\")")
endforeach()

file(CREATE_LINK /opt/drav/bin/drav "${CMAKE_BINARY_DIR}/drav-symlink" SYMBOLIC)
install(FILES "${CMAKE_BINARY_DIR}/drav-symlink" DESTINATION /usr/bin RENAME drav)
install(FILES "${PROJECT_SOURCE_DIR}/resources/drav.desktop" DESTINATION /usr/share/applications)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME drav)
set(CPACK_PACKAGE_VENDOR drav)
set(CPACK_PACKAGE_CONTACT "İshak Gönül <1sh4kg0nu1@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline vector design tool")
set(CPACK_PACKAGE_DESCRIPTION "Desktop vector design tool with a Qt Quick UI and a Skia GPU canvas. No network, no telemetry, no accounts.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/shakg/drav")
set(CPACK_PACKAGING_INSTALL_PREFIX /opt/drav)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_SECTION graphics)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS /opt/drav/lib)
include(CPack)

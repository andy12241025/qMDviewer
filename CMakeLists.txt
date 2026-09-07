cmake_minimum_required(VERSION 3.16)

project(qMDviewer
    VERSION 1.0.0
    DESCRIPTION "Lightweight cross-platform Markdown viewer"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Build against whichever major version is installed.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(qt_version "${Qt${QT_VERSION_MAJOR}_VERSION}")
if(NOT qt_version)
    set(qt_version "${QT_VERSION}")
endif()
if(qt_version AND qt_version VERSION_LESS 5.14)
    message(FATAL_ERROR
        "Qt ${qt_version} found, but QTextDocument::setMarkdown() needs Qt 5.14 or newer.")
endif()

# Only needed for File > Print; PDF export goes through QPdfWriter in QtGui.
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS PrintSupport)

if(WIN32)
    enable_language(RC)
endif()

add_executable(qmdviewer WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/markdownview.cpp
    src/markdownview.h
    src/codehighlighter.cpp
    src/codehighlighter.h
    src/pdfoutline.cpp
    src/pdfoutline.h
    src/markdowneditor.cpp
    src/markdowneditor.h
    src/vimhandler.cpp
    src/vimhandler.h
    resources/qmdviewer.qrc
    $<$<BOOL:${WIN32}>:resources/qmdviewer.rc>
)

target_link_libraries(qmdviewer PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(Qt${QT_VERSION_MAJOR}PrintSupport_FOUND)
    target_link_libraries(qmdviewer PRIVATE Qt${QT_VERSION_MAJOR}::PrintSupport)
    target_compile_definitions(qmdviewer PRIVATE QMDV_HAVE_PRINTSUPPORT)
else()
    message(STATUS "Qt PrintSupport not found - printing disabled (PDF export still works)")
endif()

target_compile_definitions(qmdviewer PRIVATE QMDV_VERSION="${PROJECT_VERSION}")

set_target_properties(qmdviewer PROPERTIES
    MACOSX_BUNDLE_ICON_FILE "qmdviewer.icns"
    MACOSX_BUNDLE_BUNDLE_NAME "qMDviewer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.qmdviewer.app"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

include(GNUInstallDirs)
install(TARGETS qmdviewer
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(UNIX AND NOT APPLE)
    install(FILES packaging/qmdviewer.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    foreach(icon_size 16 24 32 48 64 128 256)
        install(FILES resources/qmdviewer-${icon_size}.png
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${icon_size}x${icon_size}/apps"
            RENAME qmdviewer.png)
    endforeach()
endif()

# --- packaging --------------------------------------------------------------
# cmake --build build && cpack -G DEB --config build/CPackConfig.cmake
set(CPACK_PACKAGE_NAME "qmdviewer")
set(CPACK_PACKAGE_VENDOR "qMDviewer")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/andy12241025/qMDviewer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight cross-platform Markdown viewer")
# One string: separate arguments would become a CMake list and land in the
# control file with semicolons in it.
set(CPACK_PACKAGE_DESCRIPTION
    "A small Markdown viewer and editor built on Qt's own Markdown renderer, so it\nneeds no web engine and no third-party Markdown library. It reloads when the\nfile changes on disk, shows a heading outline, colours fenced code, exports PDF\nwith a real document outline, and has an optional source editor with a vim mode.")
set(CPACK_PACKAGE_CONTACT "andy12241025 <andy12241025@users.noreply.github.com>")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_PACKAGE_SECTION "text")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# Qt only *recommends* the platform plugin, and without it the application
# cannot open a window at all, so depend on it outright.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-gtk-platformtheme")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)

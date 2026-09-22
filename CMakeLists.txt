cmake_minimum_required(VERSION 3.16)

# The VERSION file is the single source of truth for the app's base version number -- CI reads
# it too (for the "number-commit" scheme on regular builds), so it can't just live as a literal
# here where only CMake would see it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GDLAUNCHER_BASE_VERSION LIMIT_COUNT 1)
string(STRIP "${GDLAUNCHER_BASE_VERSION}" GDLAUNCHER_BASE_VERSION)

project(GDLauncher VERSION ${GDLAUNCHER_BASE_VERSION} LANGUAGES CXX C)

# The version actually baked into the app (QCoreApplication::applicationVersion(), and what
# CPack names packages after). Defaults to the plain VERSION-file number for a local dev build;
# CI overrides it with -DGDLAUNCHER_APP_VERSION=... -- "<version>-<short-sha>" for a regular
# per-commit build, or just the tag's version number for a tagged release.
if(NOT GDLAUNCHER_APP_VERSION)
    set(GDLAUNCHER_APP_VERSION "${GDLAUNCHER_BASE_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# When ON, compiles in verbose diagnostic logging (detected local/remote versions,
# download/extraction progress, instance operations, launch steps, ...) that streams live
# into the log console. Off by default so a normal build has zero extra log overhead.
# Also defaults ON for a Debug configuration, since that's what most people mean by "a debug
# build" -- override either way with -DGDLAUNCHER_DEBUG_LOGGING=ON/OFF.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(GDLAUNCHER_DEBUG_LOGGING "Enable verbose live debug logging" ON)
else()
    option(GDLAUNCHER_DEBUG_LOGGING "Enable verbose live debug logging" OFF)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Concurrent)
find_package(ZLIB REQUIRED)

# --- Vendored miniz (zip read/write) ---
add_library(miniz STATIC
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
)
target_include_directories(miniz PUBLIC third_party/miniz)

set(APP_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/Settings.cpp
    src/DebugLog.cpp
    src/InstanceManager.cpp
    src/VersionManager.cpp
    src/GameLauncher.cpp
    src/ConsoleWindow.cpp
    src/UpdateChecker.cpp
    src/LogPipeServer.cpp
    src/gd/GDParser.cpp
    src/gd/GDCrypto.cpp
    src/gd/SaveEditor.cpp
    src/ui/InstanceDialog.cpp
    src/ui/SettingsDialog.cpp
    src/ui/DownloadsDialog.cpp
    src/ui/SaveEditorDialog.cpp
    src/ui/Theme.cpp
    src/ui/Animations.cpp
    src/ui/WelcomeDialog.cpp
    src/ui/ChangelogDialog.cpp
    src/ui/XmlHighlighter.cpp
)

set(APP_HEADERS
    src/MainWindow.h
    src/Settings.h
    src/DebugLog.h
    src/InstanceManager.h
    src/VersionManager.h
    src/GameLauncher.h
    src/ConsoleWindow.h
    src/UpdateChecker.h
    src/LogPipeServer.h
    src/gd/GDParser.h
    src/gd/GDCrypto.h
    src/gd/SaveEditor.h
    src/ui/InstanceDialog.h
    src/ui/SettingsDialog.h
    src/ui/DownloadsDialog.h
    src/ui/SaveEditorDialog.h
    src/ui/Theme.h
    src/ui/Animations.h
    src/ui/WelcomeDialog.h
    src/ui/ChangelogDialog.h
    src/ui/XmlHighlighter.h
)

if(WIN32)
    list(APPEND APP_SOURCES resources/app.rc)
endif()

qt_add_executable(gdlauncher ${APP_SOURCES} ${APP_HEADERS} resources/resources.qrc)

target_link_libraries(gdlauncher PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::Concurrent ZLIB::ZLIB miniz)
target_include_directories(gdlauncher PRIVATE src)
target_compile_definitions(gdlauncher PRIVATE GDLAUNCHER_APP_VERSION="${GDLAUNCHER_APP_VERSION}")
message(STATUS "GDLauncher: app version = ${GDLAUNCHER_APP_VERSION}")

if(GDLAUNCHER_DEBUG_LOGGING)
    target_compile_definitions(gdlauncher PRIVATE GDLAUNCHER_DEBUG_LOGGING)
    message(STATUS "GDLauncher: verbose debug logging ENABLED")
endif()

if(WIN32)
    if(GDLAUNCHER_DEBUG_LOGGING)
        # Keep the console subsystem for a debug-logging build: WIN32_EXECUTABLE detaches the
        # process from stdout/stderr entirely, so qDebug() output from GD_DEBUG_LOG would go
        # nowhere even if you launched the exe from a terminal expecting to see it.
        message(STATUS "GDLauncher: keeping console subsystem so debug logging is visible from a terminal")
    else()
        set_target_properties(gdlauncher PROPERTIES WIN32_EXECUTABLE ON)
    endif()
endif()

install(TARGETS gdlauncher RUNTIME DESTINATION bin)

if(UNIX AND NOT APPLE)
    install(FILES resources/gdlauncher.desktop DESTINATION share/applications)
    install(FILES resources/icon.png DESTINATION share/icons/hicolor/256x256/apps RENAME gdlauncher.png)
endif()

# --- CPack: .deb / .rpm on Linux, NSIS (.exe) / WiX (.msi) on Windows ---
# The zip/tar.gz release artifacts are self-contained (bundled Qt libs via windeployqt, or built
# against system Qt on Linux); these packages instead declare Qt as a real dependency so a
# system's package manager (or the installer, on Windows) resolves it, which is what users
# installing via apt/dnf/an installer actually expect.
set(CPACK_PACKAGE_NAME "gdlauncher")
set(CPACK_PACKAGE_VENDOR "pcpapc172")
set(CPACK_PACKAGE_VERSION ${GDLAUNCHER_APP_VERSION})
# CPack defaults this to "<name> <version>" (e.g. "gdlauncher 1.0.0") unless told otherwise,
# which would put every release in its own Program Files folder instead of upgrading in place --
# NSIS's uninstall-before-install and WiX's upgrade GUID both only recognize a prior install
# when it's at the SAME path. Keep it fixed across versions so updates actually update.
set(CPACK_PACKAGE_INSTALL_DIRECTORY "GDLauncher")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Game Instance Launcher for Geometry Dash")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/pcpapc172/gdlaunchercpp")
set(CPACK_PACKAGE_CONTACT "pcpapc172")
# CPack's WiX generator specifically requires the license file to have a recognized extension
# (.txt or .rtf) -- our repo-root LICENSE file (no extension, standard for GitHub license
# detection) makes WiX fail with "unsupported WiX License file extension ''" even though NSIS
# and DEB/RPM tolerate it fine. Point CPack at a .txt copy instead of renaming the real one.
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/resources/LICENSE.txt")

# Debian/Ubuntu (.deb)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "pcpapc172")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6core6, libqt6widgets6, libqt6network6, libqt6concurrent6, zlib1g, wine")
set(CPACK_DEBIAN_FILE_NAME "gdlauncher-${GDLAUNCHER_APP_VERSION}-amd64.deb")

# Fedora/RHEL (.rpm)
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase, zlib, wine")
set(CPACK_RPM_FILE_NAME "gdlauncher-${GDLAUNCHER_APP_VERSION}-1.x86_64.rpm")

# Windows NSIS installer (.exe)
set(CPACK_NSIS_PACKAGE_NAME "GDLauncher")
set(CPACK_NSIS_DISPLAY_NAME "GDLauncher")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\gdlauncher.exe")
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/icon.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/icon.ico")
set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\GDLauncher.lnk' '$INSTDIR\\\\bin\\\\gdlauncher.exe'")
set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\GDLauncher.lnk'")

# Windows MSI (WiX)
set(CPACK_WIX_UPGRADE_GUID "6E6F5A4F-6D53-4E8E-9E30-6D6C0D7B7C63")
set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/resources/icon.ico")

if(NOT CPACK_GENERATOR)
    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
    elseif(APPLE)
        set(CPACK_GENERATOR "TGZ")
    else()
        set(CPACK_GENERATOR "TGZ")
    endif()
endif()

include(CPack)

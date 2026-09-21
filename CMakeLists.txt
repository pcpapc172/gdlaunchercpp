cmake_minimum_required(VERSION 3.16)
project(GDLauncher VERSION 2.1.5 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
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
)

set(APP_HEADERS
    src/MainWindow.h
    src/Settings.h
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
)

qt_add_executable(gdlauncher ${APP_SOURCES} ${APP_HEADERS} resources/resources.qrc)

target_link_libraries(gdlauncher PRIVATE Qt6::Core Qt6::Widgets Qt6::Network ZLIB::ZLIB miniz)
target_include_directories(gdlauncher PRIVATE src)

if(WIN32)
    set_target_properties(gdlauncher PROPERTIES WIN32_EXECUTABLE ON)
endif()

install(TARGETS gdlauncher RUNTIME DESTINATION bin)

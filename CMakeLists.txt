cmake_minimum_required(VERSION 3.16)
project(AristocratVideoConverter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt 5.15 — последняя ветка Qt, которая работает на Windows 7.
find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Svg Network)

set(SOURCES
    src/main.cpp
    src/core/Tools.cpp
    src/core/MediaInfo.cpp
    src/core/Preset.cpp
    src/core/Task.cpp
    src/core/FfmpegCommand.cpp
    src/core/ConversionEngine.cpp
    src/core/ConversionWorker.cpp
    src/core/Settings.cpp
    src/core/I18n.cpp
    src/core/Theme.cpp
    src/core/LocalServer.cpp
    src/core/Dvd.cpp
    src/core/Chapters.cpp
    src/core/Release.cpp
    src/core/Gender.cpp
    src/ui/MainWindow.cpp
    src/ui/ConvertPage.cpp
    src/ui/TaskCard.cpp
    src/ui/DropZone.cpp
    src/ui/FormatPopup.cpp
    src/ui/PresetEditDialog.cpp
    src/ui/OutputSizeDialog.cpp
    src/ui/EditDialog.cpp
    src/ui/TrimDialog.cpp
    src/ui/FramePreview.cpp
    src/ui/PlaceholderPage.cpp
    src/ui/ToolboxPage.cpp
    src/ui/Widgets.cpp
    src/ui/Frameless.cpp
    src/ui/DownloadPage.cpp
    src/ui/SettingsDialog.cpp
    src/ui/ChaptersDialog.cpp
    src/ui/CloudUpload.cpp
)

set(RESOURCES resources/resources.qrc browser-extension/extension.qrc)

if(WIN32)
    set(APP_RC "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    file(WRITE "${APP_RC}" "IDI_ICON1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/resources/logo/app.ico\"\n")
    list(APPEND RESOURCES "${APP_RC}")
endif()

# Встроенный браузер (перехват видеопотоков) — нужен Qt WebEngine
option(WITH_WEBENGINE "Встроенный браузер на Qt WebEngine" ON)
if(WITH_WEBENGINE)
    find_package(Qt5 QUIET COMPONENTS WebEngineWidgets)
endif()
if(Qt5WebEngineWidgets_FOUND)
    list(APPEND SOURCES src/ui/BrowserWindow.cpp src/ui/PageGrabber.cpp src/ui/SceneTools.cpp src/ui/ReleaseManager.cpp src/ui/ActressPhotos.cpp src/ui/ServerPage.cpp src/ui/ServerScan.cpp src/ui/CoverArt.cpp src/ui/ReleaseScheduler.cpp)
    message(STATUS "Встроенный браузер: включён")
else()
    message(STATUS "Встроенный браузер: Qt WebEngine не найден, браузер отключён")
endif()

add_executable(aristocrat WIN32 ${SOURCES} ${RESOURCES})
if(Qt5WebEngineWidgets_FOUND)
    target_link_libraries(aristocrat PRIVATE Qt5::WebEngineWidgets)
    target_compile_definitions(aristocrat PRIVATE HAVE_WEBENGINE)
endif()
if(MSVC)
    target_compile_options(aristocrat PRIVATE /utf-8 /W3)
endif()
target_include_directories(aristocrat PRIVATE src)
target_link_libraries(aristocrat PRIVATE Qt5::Widgets Qt5::Svg Qt5::Network)
if(WIN32)
    target_link_libraries(aristocrat PRIVATE winmm)   # сигнал «готово» планировщика (PlaySound)
endif()
target_compile_definitions(aristocrat PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    QT_DEPRECATED_WARNINGS)

if(MINGW)
    # Статическая линковка рантайма MinGW — не нужен отдельный установщик библиотек.
    target_link_options(aristocrat PRIVATE -static-libgcc -static-libstdc++)
endif()

set_target_properties(aristocrat PROPERTIES OUTPUT_NAME "AristocratVideoConverter")

install(TARGETS aristocrat RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/aristocrat-video-converter.desktop DESTINATION share/applications)
    install(FILES resources/logo/aristocrat.png DESTINATION share/icons/hicolor/256x256/apps RENAME aristocrat-video-converter.png)
endif()

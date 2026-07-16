cmake_minimum_required(VERSION 3.21)
project(image-viewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt: AUTOMOC handles Q_OBJECT headers automatically.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)
find_package(LibArchive REQUIRED)
find_package(libheif CONFIG REQUIRED)
find_package(libavif CONFIG REQUIRED)
find_package(libraw CONFIG REQUIRED)

# Testable core (no GUI): sources (folder/archive), decoding, cache/prefetch,
# and the Browser playlist model. Linked by both the app and the tests.
add_library(viewer-core STATIC
    src/source/ImageSource.cpp
    src/source/FolderSource.cpp
    src/source/ArchiveSource.cpp
    src/decode/ImageDecoder.cpp
    src/decode/FtypBox.cpp
    src/decode/HeifDecoder.cpp
    src/decode/AvifDecoder.cpp
    src/decode/RawDecoder.cpp
    src/cache/ImageCache.cpp
    src/cache/Prefetcher.cpp
    src/browse/Browser.cpp
)
target_include_directories(viewer-core PUBLIC src)
# vcpkg 依赖放在 Qt 之前:Homebrew Qt 会把 /opt/homebrew/include 注入包含路径,
# 若排在 vcpkg 之前,系统里同名库(如 ImageMagick 依赖的 libheif)的头文件会
# 抢先命中,造成「Homebrew 头文件 + vcpkg 静态库」的 ABI 错配。
target_link_libraries(viewer-core PUBLIC
    LibArchive::LibArchive
    heif
    avif
    libraw::raw_r
    Qt6::Core
    Qt6::Gui
)

# GUI application.
add_executable(image-viewer
    src/main.cpp
    src/MainWindow.cpp
    src/ImageView.cpp
)
target_link_libraries(image-viewer PRIVATE viewer-core Qt6::Widgets)

if(APPLE)
    set_target_properties(image-viewer PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "ImageViewer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.image-viewer"
    )
endif()

# Tests (Qt Test). Disable with -DBUILD_TESTING=OFF.
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

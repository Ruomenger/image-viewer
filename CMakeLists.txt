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

# Testable core (no GUI): the image-source abstraction and its
# folder / archive implementations. Linked by both the app and the tests.
add_library(viewer-core STATIC
    src/source/ImageSource.cpp
    src/source/FolderSource.cpp
    src/source/ArchiveSource.cpp
    src/cache/ImageCache.cpp
)
target_include_directories(viewer-core PUBLIC src)
target_link_libraries(viewer-core PUBLIC
    Qt6::Core
    Qt6::Gui
    LibArchive::LibArchive
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

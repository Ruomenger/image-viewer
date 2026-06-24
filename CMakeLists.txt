cmake_minimum_required(VERSION 3.21)
project(image-viewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt: AUTOMOC handles Q_OBJECT headers automatically.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt comes from a prebuilt install (Homebrew / aqt). Point CMAKE_PREFIX_PATH at it
# (see README / CMakePresets.json — usually $env{QT_ROOT}).
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Everything else comes from vcpkg (manifest mode, see vcpkg.json).
find_package(LibArchive REQUIRED)

add_executable(image-viewer
    src/main.cpp
    src/MainWindow.cpp
    src/ImageView.cpp
    src/source/ImageSource.cpp
    src/source/FolderSource.cpp
    src/source/ArchiveSource.cpp
)

target_include_directories(image-viewer PRIVATE src)
target_link_libraries(image-viewer PRIVATE
    Qt6::Widgets
    LibArchive::LibArchive
)

if(APPLE)
    set_target_properties(image-viewer PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "ImageViewer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.image-viewer"
    )
endif()

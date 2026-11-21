cmake_minimum_required(VERSION 3.20)
project(pgt-studio VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(pgt-studio
    src/main.cpp
    src/MainWindow.cpp
    src/OutputPanel.cpp
    src/editor/CodeEditor.cpp
    src/editor/PgtHighlighter.cpp
    src/project/NewProjectDialog.cpp
    src/settings/Settings.cpp
    src/settings/SettingsDialog.cpp
)
target_include_directories(pgt-studio PRIVATE src)
target_compile_definitions(pgt-studio PRIVATE PGT_STUDIO_VERSION="${PROJECT_VERSION}")
target_compile_options(pgt-studio PRIVATE -Wall -Wextra)
target_link_libraries(pgt-studio PRIVATE Qt6::Widgets)

install(TARGETS pgt-studio RUNTIME DESTINATION bin)

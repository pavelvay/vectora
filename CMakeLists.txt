cmake_minimum_required(VERSION 3.21)

project(Vectora VERSION 0.1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport)

qt_standard_project_setup()

qt_add_executable(vectora
    src/main.cpp
    src/Theme.cpp
    src/Theme.h
    src/Icons.cpp
    src/Icons.h
    src/Tools.h
    src/CanvasView.cpp
    src/CanvasView.h
    src/Ruler.cpp
    src/Ruler.h
    src/ToolPalette.cpp
    src/ToolPalette.h
    src/ColorPalette.cpp
    src/ColorPalette.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/Document.cpp
    src/Document.h
    src/VectorItem.cpp
    src/VectorItem.h
    src/SelfTest.cpp
    src/SelfTest.h
    src/DockPanels.cpp
    src/DockPanels.h
    src/WelcomeWidget.cpp
    src/WelcomeWidget.h
)

target_link_libraries(vectora PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::PrintSupport
)

set_target_properties(vectora PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Vectora"
    MACOSX_BUNDLE_GUI_IDENTIFIER "app.vectora.editor"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

include(GNUInstallDirs)
install(TARGETS vectora
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(UNIX AND NOT APPLE)
    install(FILES packaging/vectora.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES resources/vectora.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME vectora.svg
    )
endif()

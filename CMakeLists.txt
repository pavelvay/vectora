cmake_minimum_required(VERSION 3.21)

project(Vectora VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport Network)
# Qt PDF powers native PDF/AI import when the module is available. Builds
# without it fall back to Ghostscript at runtime.
find_package(Qt6 6.4 QUIET COMPONENTS Pdf)

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
    src/Effects.cpp
    src/Effects.h
    src/AssetLibrary.cpp
    src/AssetLibrary.h
    src/AssetPanel.cpp
    src/AssetPanel.h
    src/Patterns.cpp
    src/Patterns.h
    src/PatternStore.cpp
    src/PatternStore.h
    src/PatternDialog.cpp
    src/PatternDialog.h
    src/CodeGenerator.cpp
    src/CodeGenerator.h
    src/CodeDialog.cpp
    src/CodeDialog.h
    src/UpdateChecker.cpp
    src/UpdateChecker.h
    src/DocumentTools.cpp
    src/DocumentTools.h
    src/DocumentDialogs.cpp
    src/DocumentDialogs.h
    src/TemplateStore.cpp
    src/TemplateStore.h
    src/VectorItem.cpp
    src/VectorItem.h
    src/SelfTest.cpp
    src/SelfTest.h
    src/DockPanels.cpp
    src/DockPanels.h
    src/SvgImporter.cpp
    src/SvgImporter.h
    src/FormatIO.cpp
    src/FormatIO.h
    src/VectorCraftIO.cpp
    src/VectorCraftIO.h
    src/CdrIO.cpp
    src/CdrIO.h
    src/CmxIO.cpp
    src/Cmx32Writer.cpp
    src/CmxIO.h
    src/PageDialog.cpp
    src/PageDialog.h
    src/PageBar.cpp
    src/PageBar.h
    src/WelcomeWidget.cpp
    src/WelcomeWidget.h
    resources/resources.qrc
)

target_link_libraries(vectora PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::PrintSupport
)

if(TARGET Qt6::Pdf)
    target_link_libraries(vectora PRIVATE Qt6::Pdf)
    target_compile_definitions(vectora PRIVATE VECTORA_HAVE_QT_PDF=1)
endif()

# libcdr reads CorelDRAW documents. It is optional: without it the app falls
# back to cdr2xhtml, Inkscape or LibreOffice at runtime.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBCDR QUIET libcdr-0.1 librevenge-generators-0.0
        librevenge-stream-0.0)
    # pkg-config results are cached, so a dependency that was present during an
    # earlier configure (for example under /tmp) can leave dead paths behind.
    # Drop such a cached result and look the packages up again.
    if(LIBCDR_FOUND)
        set(_libcdr_stale FALSE)
        foreach(_libcdr_dir ${LIBCDR_INCLUDE_DIRS} ${LIBCDR_LIBRARY_DIRS})
            if(_libcdr_dir AND NOT EXISTS "${_libcdr_dir}")
                set(_libcdr_stale TRUE)
            endif()
        endforeach()
        if(_libcdr_stale)
            message(STATUS "libcdr: ignoring stale cached paths, searching again")
            get_cmake_property(_libcdr_vars CACHE_VARIABLES)
            foreach(_libcdr_var ${_libcdr_vars})
                if(_libcdr_var MATCHES "^LIBCDR_")
                    unset(${_libcdr_var} CACHE)
                endif()
            endforeach()
            pkg_check_modules(LIBCDR QUIET libcdr-0.1 librevenge-generators-0.0
                librevenge-stream-0.0)
        endif()
    endif()
    if(LIBCDR_FOUND)
        target_include_directories(vectora PRIVATE ${LIBCDR_INCLUDE_DIRS})
        target_link_directories(vectora PRIVATE ${LIBCDR_LIBRARY_DIRS})
        target_link_libraries(vectora PRIVATE ${LIBCDR_LIBRARIES})
        target_compile_definitions(vectora PRIVATE VECTORA_HAVE_LIBCDR=1)
        set_target_properties(vectora PROPERTIES
            BUILD_RPATH "\$ORIGIN;${LIBCDR_LIBRARY_DIRS}")
    endif()
endif()

if(WIN32)
    # Forward slashes: rc.exe treats backslashes in quoted paths as escapes.
    set(VECTORA_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/vectora.ico")
    configure_file(resources/vectora.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/vectora.rc @ONLY)
    target_sources(vectora PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/vectora.rc)
endif()

target_link_libraries(vectora PRIVATE Qt6::Network)

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
    install(FILES resources/vectora-256.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME vectora.png
    )
    install(FILES resources/vectora-512.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
        RENAME vectora.png
    )
endif()

set(CPACK_PACKAGE_NAME "vectora")
set(CPACK_PACKAGE_VENDOR "Vectora")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vectora vector graphics editor")
set(CPACK_PACKAGE_CONTACT "pavelvay")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_NAME "vectora")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "pavelvay")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(_vectora_deb_depends "libqt6widgets6, libqt6svg6, libc6 (>= 2.35)")
    if(LIBCDR_FOUND)
        string(APPEND _vectora_deb_depends
            ", libcdr-0.1-1, librevenge-0.0-0, librevenge-generators-0.0-0, librevenge-stream-0.0-0")
    endif()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${_vectora_deb_depends}")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    else()
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    endif()
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_PACKAGE_FILE_NAME "vectora-${PROJECT_VERSION}-amd64")
endif()
include(CPack)

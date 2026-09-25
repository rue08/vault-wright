cmake_minimum_required(VERSION 3.16)

project(VaultWright VERSION 0.1 LANGUAGES CXX)

add_compile_definitions(PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network WebEngineWidgets WebChannel)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network WebEngineWidgets WebChannel)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
)

# App icon, per platform. The .icns/.ico are pre-generated (from Icons/app-icon.svg,
# the editable source) rather than built at configure time, since the tools to do that
# (rsvg-convert, iconutil) aren't guaranteed to be on every dev/build machine.
if(APPLE)
    set(APP_ICON_MACOS "${CMAKE_SOURCE_DIR}/Icons/AppIcon.icns")
    set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND PROJECT_SOURCES ${APP_ICON_MACOS})
endif()
if(WIN32)
    list(APPEND PROJECT_SOURCES windows/app.rc)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(VaultWright
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        resources.qrc
        monacoeditor.h monacoeditor.cpp
        theme.h theme.cpp
        terminal.h terminal.cpp
        loginwindow.h loginwindow.cpp loginwindow.ui
        authenticator.h authenticator.cpp
        storage.h storage.cpp
        config.h


    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET VaultWright APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(VaultWright SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(VaultWright
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(VaultWright PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebEngineWidgets
    Qt${QT_VERSION_MAJOR}::WebChannel)
target_link_libraries(VaultWright PRIVATE Qt${QT_VERSION_MAJOR}::Core)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.VaultWright)
endif()
set_target_properties(VaultWright PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
if(APPLE)
    set_target_properties(VaultWright PROPERTIES MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
endif()

include(GNUInstallDirs)
install(TARGETS VaultWright
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Bundle a portable MinGW-w64 toolchain next to VaultWright.exe so Run works
# on Windows with no separate compiler install. See windows/README.md for
# where to obtain one -- this step is a no-op (with a warning) until a
# toolchain has actually been placed there.
if(WIN32)
    set(MINGW_BUNDLE_DIR "${CMAKE_SOURCE_DIR}/windows/mingw64")
    if(EXISTS "${MINGW_BUNDLE_DIR}")
        add_custom_command(TARGET VaultWright POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${MINGW_BUNDLE_DIR}" "$<TARGET_FILE_DIR:VaultWright>/mingw64"
            COMMENT "Copying bundled MinGW-w64 toolchain next to VaultWright.exe"
        )
        install(DIRECTORY "${MINGW_BUNDLE_DIR}/" DESTINATION "${CMAKE_INSTALL_BINDIR}/mingw64")
    else()
        message(STATUS "windows/mingw64/ not found -- VaultWright.exe will fall back to g++ on PATH. "
                        "See windows/README.md to bundle a toolchain before packaging a release.")
    endif()
endif()

# Copy the embedded Monaco Editor -- the tracked bridge page (monaco-host/,
# see MonacoEditor::MonacoEditor()) plus the vendored Monaco build -- next to
# the built binary as monaco/. The vendored build (third_party/monaco/vs/)
# is gitignored (too large for git, same reasoning as windows/mingw64/); run
# third_party/fetch-monaco.sh once per machine to populate it. Missing it is
# a hard error, unlike the optional MinGW bundle above, since there's no
# fallback editor path once MonacoEditor is what every workspace tab uses.
set(MONACO_VENDOR_DIR "${CMAKE_SOURCE_DIR}/third_party/monaco/vs")
if(NOT EXISTS "${MONACO_VENDOR_DIR}")
    message(FATAL_ERROR "third_party/monaco/vs/ not found -- run third_party/fetch-monaco.sh first.")
endif()
add_custom_command(TARGET VaultWright POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/monaco-host" "$<TARGET_FILE_DIR:VaultWright>/monaco"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MONACO_VENDOR_DIR}" "$<TARGET_FILE_DIR:VaultWright>/monaco/vs"
    COMMENT "Copying Monaco Editor (bridge page + vendored build) next to VaultWright"
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/monaco-host/" DESTINATION "${CMAKE_INSTALL_BINDIR}/monaco")
install(DIRECTORY "${MONACO_VENDOR_DIR}/" DESTINATION "${CMAKE_INSTALL_BINDIR}/monaco/vs")

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(VaultWright)
endif()

cmake_minimum_required(VERSION 3.16)
project(CalDisplay VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 QUIET COMPONENTS Core Gui Qml Quick Network Xml)
if (Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick Network Xml)
    set(QT_PACKAGE Qt5)
endif()

add_executable(CalDisplay
    src/main.cpp
    src/accountmanager.cpp
    src/accountmanager.h
    src/eventmodel.cpp
    src/eventmodel.h
    src/feedmanager.cpp
    src/feedmanager.h
)

target_link_libraries(CalDisplay PRIVATE
    ${QT_PACKAGE}::Core
    ${QT_PACKAGE}::Gui
    ${QT_PACKAGE}::Network
    ${QT_PACKAGE}::Qml
    ${QT_PACKAGE}::Quick
    ${QT_PACKAGE}::Xml
)

# Keep QML external for easy iteration on low-power systems.
if (WIN32)
    target_compile_definitions(CalDisplay PRIVATE APP_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml")
else()
    target_compile_definitions(CalDisplay PRIVATE APP_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml")
endif()

# Install targets
install(TARGETS CalDisplay DESTINATION bin)
install(DIRECTORY qml DESTINATION share/CalDisplay)
install(FILES deploy/caldisplay-app.desktop DESTINATION share/applications RENAME caldisplay.desktop)
install(FILES assets/caldisplay.svg DESTINATION share/icons/hicolor/scalable/apps)

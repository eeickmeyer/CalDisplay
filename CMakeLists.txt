# SPDX-License-Identifier: GPL-3.0-or-later
# CalDisplay - A calendar application for displaying events from shared ICS feeds
# Copyright (C) 2026 Erich Eickmeyer
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

cmake_minimum_required(VERSION 3.16)
project(CalDisplay VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 QUIET COMPONENTS Core Gui Qml Quick Network Xml Widgets)
if (Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Qml Quick Network Xml Widgets)
    set(QT_PACKAGE Qt5)
endif()

add_executable(CalDisplay
    src/main.cpp
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
    ${QT_PACKAGE}::Widgets
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
install(FILES deploy/caldisplay-windowed.desktop DESTINATION share/applications)
install(FILES assets/caldisplay.svg DESTINATION share/icons/hicolor/scalable/apps)

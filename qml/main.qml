// SPDX-License-Identifier: GPL-3.0-or-later
// CalDisplay - A calendar application for displaying events from shared ICS feeds
// Copyright (C) 2026 Erich Eickmeyer
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: root
    visible: true
    visibility: windowedMode ? Window.Windowed : Window.FullScreen
    title: windowedMode ? "CalDisplay (Configuration)" : "CalDisplay"
    color: "#0f1f2f"
    
    property bool setupOpen: windowedMode
    property string currentTime: root.formatClockTime(new Date())
    readonly property string ncPrimary: "#0082c9"
    readonly property string ncPrimaryStrong: "#0073b1"
    readonly property string ncAccent: "#2daee0"
    readonly property string ncBgTop: "#1b3a54"
    readonly property string ncBgBottom: "#102335"
    readonly property string ncPanel: "#162b3e"
    readonly property string ncPanelAlt: "#122538"
    readonly property string ncBorder: "#2e5878"
    readonly property string ncText: "#f3f8fc"
    readonly property string ncMutedText: "#c6d8e7"
    readonly property string ncSubtleText: "#8eaac2"
    readonly property var weekdayNames: feedManager.sundayFirst
        ? ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
        : ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    readonly property bool localePrefers12Hour: {
        var pattern = Qt.locale().timeFormat(Locale.ShortFormat)
        return pattern.indexOf("AP") !== -1 || (pattern.indexOf("h") !== -1 && pattern.indexOf("H") === -1)
    }
    readonly property bool use12HourClock: {
        if (feedManager.timeFormatPreference === 1) return true
        if (feedManager.timeFormatPreference === 2) return false
        return localePrefers12Hour
    }
    readonly property string eventTimePattern: use12HourClock ? "h:mm AP" : "HH:mm"
    readonly property string clockTimePattern: use12HourClock ? "h:mm:ss AP" : "HH:mm:ss"

    // ── View cycling ──────────────────────────────────────────────
    property int  currentView:   0      // 0 = Day, 1 = Week, 2 = Month, 3 = Two Months
    property int  viewCycleSecs: 20
    property real cycleProgress: 0.0
    property var  allEvents:     []
    readonly property var viewNames: ["Day", "Week", "Month", "Two Months"]
    readonly property var calendarLegend: {
        var seen = {}; var result = []
        for (var i = 0; i < allEvents.length; i++) {
            var ev = allEvents[i]
            if (!seen[ev.calendar]) { seen[ev.calendar] = true; result.push({name: ev.calendar, color: ev.color}) }
        }
        return result
    }

    function refreshEventData() {
        allEvents = eventModel.getEvents()
    }

    function startCycle() {
        cycleProgress = 0
        progressAnim.restart()
    }

    function isSameDate(a, b) {
        return a.getFullYear() === b.getFullYear() &&
               a.getMonth()    === b.getMonth()    &&
               a.getDate()     === b.getDate()
    }

    function dayStartMs(dayDate) {
        var d = new Date(dayDate)
        d.setHours(0, 0, 0, 0)
        return d.getTime()
    }

    function dayEndMs(dayDate) {
        var d = new Date(dayDate)
        d.setHours(24, 0, 0, 0)
        return d.getTime()
    }

    function eventsForDay(dayDate) {
        var start = dayStartMs(dayDate)
        var end = dayEndMs(dayDate)
        var filtered = root.allEvents.filter(function(ev) {
            return ev.endMs > start && ev.startMs < end
        })
        filtered.sort(function(a, b) {
            return a.startMs - b.startMs
        })
        return filtered
    }

    function isAllDayEvent(ev) {
        var start = new Date(ev.startMs)
        var end = new Date(ev.endMs)
        var startsAtMidnight = start.getHours() === 0 && start.getMinutes() === 0 &&
            start.getSeconds() === 0 && start.getMilliseconds() === 0
        var endsAtMidnight = end.getHours() === 0 && end.getMinutes() === 0 &&
            end.getSeconds() === 0 && end.getMilliseconds() === 0
        var durationMs = ev.endMs - ev.startMs
        return startsAtMidnight && endsAtMidnight && durationMs >= (24 * 60 * 60 * 1000)
    }

    function allDayEventsForDay(dayDate) {
        return eventsForDay(dayDate).filter(function(ev) {
            return root.isAllDayEvent(ev)
        })
    }

    function timedEventsForDay(dayDate) {
        return eventsForDay(dayDate).filter(function(ev) {
            return !root.isAllDayEvent(ev)
        })
    }

    function monthChipText(ev) {
        if (root.isAllDayEvent(ev)) {
            return "All day  " + ev.title
        }
        return root.formatEventTime(new Date(ev.startMs)) + "  " + ev.title
    }

    function isTentativeEvent(ev) {
        return ev.status && ev.status === "TENTATIVE"
    }

    function timelineLayoutForDay(dayDate, events) {
        var start = dayStartMs(dayDate)
        var end = dayEndMs(dayDate)
        var layout = []
        for (var i = 0; i < events.length; i++) {
            var evStart = Math.max(events[i].startMs, start)
            var evEnd = Math.min(events[i].endMs, end)
            if (evEnd <= evStart) {
                evEnd = evStart + (15 * 60 * 1000)
            }
            layout.push({
                event: events[i],
                startMs: evStart,
                endMs: evEnd,
                slot: 0,
                columns: 1
            })
        }

        var active = []
        var clusterIndices = []
        var maxConcurrent = 1
        var nextSlot = 0

        function finalizeCluster() {
            if (clusterIndices.length === 0) {
                return
            }
            var cols = Math.max(1, maxConcurrent)
            for (var c = 0; c < clusterIndices.length; c++) {
                layout[clusterIndices[c]].columns = cols
            }
            clusterIndices = []
            maxConcurrent = 1
            nextSlot = 0
        }

        function smallestFreeSlot() {
            var used = {}
            for (var a = 0; a < active.length; a++) {
                used[active[a].slot] = true
            }
            for (var s = 0; s < nextSlot; s++) {
                if (!used[s]) {
                    return s
                }
            }
            return nextSlot
        }

        for (var j = 0; j < layout.length; j++) {
            var currentStart = layout[j].startMs
            for (var k = active.length - 1; k >= 0; k--) {
                if (active[k].endMs <= currentStart) {
                    active.splice(k, 1)
                }
            }

            if (active.length === 0 && clusterIndices.length > 0) {
                finalizeCluster()
            }

            var slot = smallestFreeSlot()
            layout[j].slot = slot
            if (slot === nextSlot) {
                nextSlot++
            }

            active.push({slot: slot, endMs: layout[j].endMs})
            clusterIndices.push(j)
            maxConcurrent = Math.max(maxConcurrent, active.length)
        }

        finalizeCluster()
        return layout
    }

    function formatEventTime(dateValue) {
        return Qt.formatTime(dateValue, root.eventTimePattern)
    }

    function formatClockTime(dateValue) {
        return Qt.formatTime(dateValue, root.clockTimePattern)
    }

    function formatTimelineHour(hourIndex) {
        var d = new Date()
        d.setHours(hourIndex, 0, 0, 0)
        return formatEventTime(d)
    }

    function scrollDayTimelineToNow() {
        if (typeof dayTimelineFlick === "undefined") {
            return
        }
        if (typeof dayViewPanel === "undefined") {
            return
        }
        if (root.currentView !== 0 || !root.isSameDate(dayViewPanel.dayDate, new Date())) {
            return
        }

        var now = new Date()
        var minutes = (now.getHours() * 60) + now.getMinutes()
        var y = minutes * (dayViewPanel.hourHeight / 60.0)
        // Keep a larger amount of past time visible above "now".
        var target = y - (dayTimelineFlick.height * 0.65)
        var maxY = Math.max(0, dayViewPanel.timelineHeight - dayTimelineFlick.height)
        dayTimelineFlick.contentY = Math.max(0, Math.min(target, maxY))
    }

    function scrollWeekTimelineToNow() {
        if (typeof weekTimelineFlick === "undefined") {
            return
        }
        if (root.currentView !== 1) {
            return
        }

        var now = new Date()
        var minutes = (now.getHours() * 60) + now.getMinutes()
        var snappedHour = Math.round(minutes / 60.0)
        var y = snappedHour * weekTimelineContent.hourHeight
        var target = y - (weekTimelineFlick.height * 0.45)
        var maxY = Math.max(0, weekTimelineContent.height - weekTimelineFlick.height)
        weekTimelineFlick.contentY = Math.max(0, Math.min(target, maxY))
    }

    function startDayEventAutoScroll() {
        if (typeof dayEventList === "undefined") {
            return
        }
        if (root.currentView !== 0 || root.setupOpen) {
            return
        }

        var maxY = Math.max(0, dayEventList.contentHeight - dayEventList.height)
        dayEventScrollAnim.stop()
        if (maxY <= 0) {
            dayEventList.contentY = 0
            return
        }

        dayEventList.contentY = 0
        dayEventScrollAnim.to = maxY
        dayEventScrollAnim.duration = root.viewCycleSecs * 1000
        dayEventScrollAnim.restart()
    }

    // Mon–Sun of the current week
    function weekDays() {
        var d = new Date()
        d.setHours(0, 0, 0, 0)
        var dow = d.getDay()
        d.setDate(d.getDate() - (feedManager.sundayFirst ? dow : (dow === 0 ? 6 : dow - 1)))
        var out = []
        for (var i = 0; i < 7; i++) { out.push(new Date(d)); d.setDate(d.getDate() + 1) }
        return out
    }

    function monthInfo(monthOffset) {
        var today = new Date()
        var first = new Date(today.getFullYear(), today.getMonth() + monthOffset, 1)
        var daysInMonth = new Date(first.getFullYear(), first.getMonth() + 1, 0).getDate()
        var firstDay = first.getDay()
        var leadingBlanks = feedManager.sundayFirst ? firstDay : (firstDay === 0 ? 6 : firstDay - 1)
        var cells = []

        for (var i = 0; i < leadingBlanks; i++) {
            cells.push({ inMonth: false, date: null })
        }

        for (var day = 1; day <= daysInMonth; day++) {
            cells.push({ inMonth: true, date: new Date(first.getFullYear(), first.getMonth(), day) })
        }

        while (cells.length % 7 !== 0) {
            cells.push({ inMonth: false, date: null })
        }

        return {
            first: first,
            cells: cells
        }
    }

    Connections {
        target: eventModel
        function onModelReset() { root.refreshEventData() }
    }

    Component.onCompleted: {
        refreshEventData()
        if (!windowedMode) startCycle()
    }

    ListModel { id: feedListModel }

    function populateFeedList() {
        feedListModel.clear()
        var lines = feedManager.feedUrls.split('\n')
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i].trim()
            if (!line) continue
            var pipe = line.indexOf('|')
            if (pipe > 0)
                feedListModel.append({name: line.substring(0, pipe).trim(), url: line.substring(pipe + 1).trim()})
            else
                feedListModel.append({name: '', url: line})
        }
    }

    function serializeFeedList() {
        var lines = []
        for (var i = 0; i < feedListModel.count; i++) {
            var item = feedListModel.get(i)
            var name = (item.name || '').trim()
            var url = (item.url || '').trim()
            if (!url) continue
            lines.push(name ? (name + '|' + url) : url)
        }
        return lines.join('\n')
    }

    function feedNameFromLocation(location) {
        var value = (location || "").trim()
        if (!value)
            return ""

        if (value.indexOf("file://") === 0)
            value = value.substring(7)

        value = value.replace(/\\/g, "/")
        var slash = value.lastIndexOf("/")
        var name = slash >= 0 ? value.substring(slash + 1) : value
        try {
            name = decodeURIComponent(name)
        } catch (err) {
        }
        if (name.toLowerCase().endsWith(".ics"))
            name = name.substring(0, name.length - 4)
        return name
    }

    onSetupOpenChanged: { if (setupOpen) populateFeedList(); else { viewCycleTimer.restart(); startCycle() } }
    onCurrentViewChanged: {
        if (currentView === 0) {
            Qt.callLater(scrollDayTimelineToNow)
            Qt.callLater(startDayEventAutoScroll)
        } else if (currentView === 1) {
            Qt.callLater(scrollWeekTimelineToNow)
        }
    }

    // ── Timers ────────────────────────────────────────────────────
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: root.currentTime = root.formatClockTime(new Date())
    }

    Timer {
        id: viewCycleTimer
        interval: root.viewCycleSecs * 1000
        running: !root.setupOpen; repeat: true
        onTriggered: { root.currentView = (root.currentView + 1) % root.viewNames.length; root.startCycle() }
    }

    NumberAnimation {
        id: dayEventScrollAnim
        target: dayEventList
        property: "contentY"
        from: 0
        to: 0
        duration: root.viewCycleSecs * 1000
        easing.type: Easing.InOutQuad
    }

    NumberAnimation {
        id: progressAnim
        target: root; property: "cycleProgress"
        from: 0.0; to: 1.0
        duration: root.viewCycleSecs * 1000
    }

    // ── Background ────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.ncBgTop }
            GradientStop { position: 1.0; color: root.ncBgBottom }
        }
    }

    // ── Main layout ───────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Text {
                text: feedManager.displayName
                color: root.ncText; font.pixelSize: 32; font.bold: true
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.currentTime
                color: root.ncAccent
                font.pixelSize: 42
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Text {
                text: Qt.formatDateTime(new Date(), "ddd yyyy-MM-dd")
                color: root.ncMutedText; font.pixelSize: 26
            }
            Button {
                visible: windowedMode
                text: "Settings"
                onClicked: root.setupOpen = !root.setupOpen
            }
        }

        // View tabs + progress bar
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.viewNames
                    delegate: Rectangle {
                        height: 34; implicitWidth: tabLabel.implicitWidth + 28; radius: 6
                        color: root.currentView === index ? root.ncPrimaryStrong : root.ncPanelAlt
                        border.width: root.currentView === index ? 1 : 0
                        border.color: root.ncAccent
                        Text {
                            id: tabLabel; anchors.centerIn: parent; text: modelData
                            color: root.currentView === index ? root.ncText : root.ncSubtleText
                            font.pixelSize: 17; font.bold: root.currentView === index
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { root.currentView = index; viewCycleTimer.restart(); root.startCycle() }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true; height: 3; color: root.ncPanelAlt; radius: 1
                Rectangle {
                    width: parent.width * root.cycleProgress; height: parent.height
                    radius: 1; color: root.ncAccent; opacity: 0.8
                }
            }
        }

        // Color key legend
        Flow {
            Layout.fillWidth: true
            spacing: 12
            visible: root.calendarLegend.length > 0
            Repeater {
                model: root.calendarLegend
                delegate: RowLayout {
                    spacing: 5
                    Rectangle { width: 12; height: 12; radius: 6; color: modelData.color }
                    Text { text: modelData.name; color: root.ncMutedText; font.pixelSize: 14 }
                }
            }
        }

        // ── StackLayout: three views ──────────────────────────────
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true

        StackLayout {
            anchors.fill: parent
            currentIndex: root.currentView

            // ── Day view ──────────────────────────────────────────
            Rectangle {
                id: dayViewPanel
                color: root.ncPanel; radius: 16
                border.width: 1; border.color: root.ncBorder; clip: true

                property var dayDate: new Date()
                property var dayAllDayEvents: root.allDayEventsForDay(dayDate)
                property var dayTimedEvents: root.timedEventsForDay(dayDate)
                property var dayTimelineEvents: root.timelineLayoutForDay(dayDate, dayTimedEvents)
                property int hourHeight: 70
                property int timelineHeight: 24 * hourHeight
                property bool isToday: root.isSameDate(dayDate, new Date())
                property bool firstLayoutSettled: false
                onDayTimedEventsChanged: Qt.callLater(root.scrollDayTimelineToNow)
                onWidthChanged: {
                    if (!firstLayoutSettled && width > 0 && height > 0) {
                        firstLayoutSettled = true
                        Qt.callLater(root.scrollDayTimelineToNow)
                    }
                }

                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    Text {
                        text: Qt.formatDate(dayViewPanel.dayDate, "dddd, MMMM d")
                        color: root.ncMutedText; font.pixelSize: 22; font.bold: true
                    }

                    RowLayout {
                        id: daySplitRow
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 12
                        property real eventPaneWidth: {
                            var maxFromRatio = daySplitRow.width * 0.38
                            var maxAllowingTimelineMin = daySplitRow.width - 420 - spacing
                            return Math.max(320, Math.min(maxFromRatio, maxAllowingTimelineMin))
                        }

                        Rectangle {
                            id: dayEventPane
                            Layout.fillHeight: true
                            Layout.minimumWidth: daySplitRow.eventPaneWidth
                            Layout.maximumWidth: daySplitRow.eventPaneWidth
                            Layout.preferredWidth: daySplitRow.eventPaneWidth
                            radius: 12
                            color: root.ncPanelAlt
                            border.width: 1
                            border.color: root.ncBorder
                            clip: true

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                Text {
                                    text: "Events"
                                    color: root.ncMutedText
                                    font.pixelSize: 18
                                    font.bold: true
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    visible: dayViewPanel.dayAllDayEvents.length > 0

                                    Text {
                                        text: "All-day"
                                        color: root.ncSubtleText
                                        font.pixelSize: 13
                                        font.bold: true
                                    }

                                    Repeater {
                                        model: dayViewPanel.dayAllDayEvents
                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            implicitHeight: 28
                                            radius: 6
                                            color: "#1a344a"
                                            border.width: 1
                                            border.color: root.ncBorder

                                            Row {
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                spacing: 6
                                                Rectangle { width: 5; height: parent.height; color: modelData.color; radius: 3 }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: modelData.title
                                                    color: root.ncText
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    width: parent.width - 10
                                                }
                                            }
                                        }
                                    }
                                }

                                ListView {
                                    id: dayEventList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 8
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds
                                    model: dayViewPanel.dayTimedEvents
                                    onContentHeightChanged: Qt.callLater(root.startDayEventAutoScroll)
                                    onHeightChanged: Qt.callLater(root.startDayEventAutoScroll)

                                    delegate: Rectangle {
                                        width: ListView.view.width
                                        height: 74
                                        radius: 10
                                        color: "#1a344a"
                                        border.width: root.isTentativeEvent(modelData) ? 2 : 1
                                        border.color: root.isTentativeEvent(modelData) ? "#fbbf24" : root.ncBorder
                                        opacity: root.isTentativeEvent(modelData) ? 0.65 : 1.0

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 10

                                            Rectangle {
                                                width: 7
                                                Layout.fillHeight: true
                                                radius: 4
                                                color: modelData.color
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1

                                                Text {
                                                    text: root.formatEventTime(new Date(modelData.startMs)) + " - " +
                                                        root.formatEventTime(new Date(modelData.endMs))
                                                    color: "#9fd7f1"
                                                    font.pixelSize: 14
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }

                                                Text {
                                                    text: (root.isTentativeEvent(modelData) ? "! " : "") + modelData.title
                                                    color: root.ncText
                                                    font.pixelSize: 18
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }

                                                Text {
                                                    text: modelData.calendar + (root.isTentativeEvent(modelData) ? " (Tentative)" : "")
                                                    color: root.ncMutedText
                                                    font.pixelSize: 12
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }
                                            }
                                        }
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        visible: dayViewPanel.dayTimedEvents.length === 0 && dayViewPanel.dayAllDayEvents.length === 0
                                        text: "No events today"
                                        color: root.ncSubtleText
                                        font.pixelSize: 24
                                    }
                                }
                            }
                        }

                        Rectangle {
                            id: dayTimelinePane
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 420
                            radius: 12
                            color: root.ncPanelAlt
                            border.width: 1
                            border.color: root.ncBorder
                            clip: true

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                Text {
                                    text: "Timeline"
                                    color: root.ncMutedText
                                    font.pixelSize: 18
                                    font.bold: true
                                }

                                Flickable {
                                    id: dayTimelineFlick
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    contentWidth: dayTimelineContent.width
                                    contentHeight: dayViewPanel.timelineHeight
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                    Component.onCompleted: Qt.callLater(root.scrollDayTimelineToNow)

                                    Item {
                                        id: dayTimelineContent
                                        width: dayTimelineFlick.width
                                        height: dayViewPanel.timelineHeight

                                        Row {
                                            anchors.fill: parent
                                            spacing: 10

                                            Item {
                                                width: 52
                                                height: parent.height

                                                Repeater {
                                                    model: 24
                                                    delegate: Text {
                                                        x: 0
                                                        y: index * dayViewPanel.hourHeight - 8
                                                        text: root.formatTimelineHour(index)
                                                        color: root.ncSubtleText
                                                        font.pixelSize: 13
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                id: timelineGrid
                                                width: Math.max(parent.width - 62, 120)
                                                height: parent.height
                                                color: "#0d1e2e"
                                                radius: 8
                                                border.width: 1
                                                border.color: root.ncBorder
                                                clip: true

                                                Repeater {
                                                    model: 25
                                                    delegate: Rectangle {
                                                        x: 0
                                                        y: index * dayViewPanel.hourHeight
                                                        width: timelineGrid.width
                                                        height: 1
                                                        color: index % 6 === 0 ? root.ncBorder : "#1a3146"
                                                    }
                                                }

                                                Repeater {
                                                    model: dayViewPanel.dayTimelineEvents
                                                    delegate: Rectangle {
                                                        property real dayStart: root.dayStartMs(dayViewPanel.dayDate)
                                                        property real minutesFromStart: (modelData.startMs - dayStart) / 60000.0
                                                        property real durationMinutes: (modelData.endMs - modelData.startMs) / 60000.0
                                                        property real availableWidth: timelineGrid.width - 12
                                                        property real slotWidth: availableWidth / Math.max(1, modelData.columns)

                                                        x: 6 + (modelData.slot * slotWidth)
                                                        y: minutesFromStart * (dayViewPanel.hourHeight / 60.0)
                                                        width: Math.max(slotWidth - 3, 24)
                                                        height: Math.max(durationMinutes * (dayViewPanel.hourHeight / 60.0), 24)
                                                        radius: 6
                                                        color: modelData.event.color
                                                        opacity: root.isTentativeEvent(modelData.event) ? 0.45 : 0.78
                                                        clip: true
                                                        z: 20 + modelData.slot
                                                        border.width: root.isTentativeEvent(modelData.event) ? 1 : 0
                                                        border.color: root.isTentativeEvent(modelData.event) ? "#fbbf24" : "transparent"

                                                        Column {
                                                            anchors.fill: parent
                                                            anchors.margins: 5
                                                            spacing: 1

                                                            Text {
                                                                text: (root.isTentativeEvent(modelData.event) ? "! " : "") + modelData.event.title
                                                                color: root.ncText
                                                                font.pixelSize: 14
                                                                font.bold: true
                                                                elide: Text.ElideRight
                                                                width: parent.width
                                                            }

                                                            Text {
                                                                text: root.formatEventTime(new Date(modelData.event.startMs)) + " - " +
                                                                      root.formatEventTime(new Date(modelData.event.endMs))
                                                                color: root.ncMutedText
                                                                font.pixelSize: 12
                                                            }
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    visible: dayViewPanel.isToday
                                                    x: 0
                                                    y: {
                                                        var _tick = root.currentTime
                                                        var now = new Date()
                                                        var minutes = (now.getHours() * 60) + now.getMinutes() + (now.getSeconds() / 60.0)
                                                        return Math.max(0, Math.min(minutes * (dayViewPanel.hourHeight / 60.0), timelineGrid.height - 2))
                                                    }
                                                    width: timelineGrid.width
                                                    height: 2
                                                    color: "#ff6b6b"
                                                    z: 50
                                                }

                                                Rectangle {
                                                    visible: dayViewPanel.isToday
                                                    x: 3
                                                    y: {
                                                        var _tick = root.currentTime
                                                        var now = new Date()
                                                        var minutes = (now.getHours() * 60) + now.getMinutes() + (now.getSeconds() / 60.0)
                                                        return Math.max(0, Math.min((minutes * (dayViewPanel.hourHeight / 60.0)) - 5, timelineGrid.height - 10))
                                                    }
                                                    width: 10
                                                    height: 10
                                                    radius: 5
                                                    color: "#ff6b6b"
                                                    z: 51
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Week view ─────────────────────────────────────────
            Rectangle {
                id: weekViewPanel
                color: root.ncPanel; radius: 16
                border.width: 1; border.color: root.ncBorder; clip: true
                property int timelineHourHeight: 56
                property real hourGutterWidth: 56
                property real weekDaySpacing: 6
                property real weekContentWidth: Math.max(0, width - 20)
                property real weekDayColumnWidth: Math.max(1, (weekContentWidth - hourGutterWidth - (6 * weekDaySpacing)) / 7)

                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 8
                    Text {
                        property var days: root.weekDays()
                        text: "Week of " + Qt.formatDate(days[0], "MMM d") +
                              " – " + Qt.formatDate(days[6], "MMM d")
                        color: root.ncMutedText; font.pixelSize: 18; font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: root.weekDays()
                            delegate: Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: Qt.formatDate(modelData, "ddd")
                                color: root.ncSubtleText
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28

                        Row {
                            anchors.fill: parent
                            spacing: weekViewPanel.weekDaySpacing

                            Item {
                                width: weekViewPanel.hourGutterWidth
                                height: parent.height
                            }

                            Repeater {
                                model: root.weekDays()
                                delegate: Rectangle {
                                    width: weekViewPanel.weekDayColumnWidth
                                    height: parent.height
                                    radius: 8
                                    color: root.ncPanelAlt
                                    border.width: 1
                                    border.color: root.isSameDate(modelData, new Date()) ? root.ncAccent : root.ncBorder

                                    property var colDate: modelData
                                    property var allDayEvents: root.allDayEventsForDay(colDate)
                                    property var firstAllDayEvent: allDayEvents.length > 0 ? allDayEvents[0] : null

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: 4
                                        visible: allDayEvents.length > 0
                                        radius: 4
                                        color: "#1a344a"

                                        Row {
                                            anchors.fill: parent
                                            spacing: 0
                                            Rectangle {
                                                width: 4
                                                height: parent.height
                                                color: firstAllDayEvent ? firstAllDayEvent.color : "transparent"
                                            }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                leftPadding: 3
                                                text: firstAllDayEvent
                                                    ? (firstAllDayEvent.title + (allDayEvents.length > 1 ? ("  +" + (allDayEvents.length - 1)) : ""))
                                                    : ""
                                                font.pixelSize: 10
                                                color: root.ncMutedText
                                                elide: Text.ElideRight
                                                width: parent.width - 5
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Flickable {
                        id: weekTimelineFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: weekTimelineContent.width
                        contentHeight: weekTimelineContent.height
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        Component.onCompleted: Qt.callLater(root.scrollWeekTimelineToNow)

                        Item {
                            id: weekTimelineContent
                            width: weekViewPanel.weekContentWidth
                            height: 24 * weekViewPanel.timelineHourHeight
                            property int hourHeight: weekViewPanel.timelineHourHeight
                            property real hourGutterWidth: weekViewPanel.hourGutterWidth

                            Row {
                                anchors.fill: parent
                                spacing: 6

                                Item {
                                    width: weekTimelineContent.hourGutterWidth
                                    height: parent.height

                                    Repeater {
                                        model: 24
                                        delegate: Text {
                                            x: 0
                                            y: index * weekTimelineContent.hourHeight - 8
                                            text: root.formatTimelineHour(index)
                                            color: root.ncSubtleText
                                            font.pixelSize: 12
                                        }
                                    }
                                }

                                Repeater {
                                    model: root.weekDays()
                                    delegate: Rectangle {
                                        width: weekViewPanel.weekDayColumnWidth
                                        height: parent.height
                                        radius: 10
                                        color: root.ncPanelAlt
                                        border.width: 1
                                        border.color: root.isSameDate(modelData, new Date()) ? root.ncAccent : root.ncBorder
                                        clip: true

                                        property var colDate: modelData
                                        property bool isToday: root.isSameDate(colDate, new Date())
                                        property var timedEvents: root.timedEventsForDay(colDate)
                                        property var dayTimelineEvents: root.timelineLayoutForDay(colDate, timedEvents)

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 4

                                            Item {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                implicitHeight: weekTimelineContent.height - 30

                                                Repeater {
                                                    model: 25
                                                    delegate: Rectangle {
                                                        x: 0
                                                        y: index * weekTimelineContent.hourHeight
                                                        width: parent.width
                                                        height: 1
                                                        color: index % 6 === 0 ? root.ncBorder : "#1a3146"
                                                    }
                                                }

                                                Repeater {
                                                    model: dayTimelineEvents
                                                    delegate: Rectangle {
                                                        property real dayStart: root.dayStartMs(colDate)
                                                        property real minutesFromStart: (modelData.startMs - dayStart) / 60000.0
                                                        property real durationMinutes: (modelData.endMs - modelData.startMs) / 60000.0
                                                        property real availableWidth: parent.width - 10
                                                        property real slotWidth: availableWidth / Math.max(1, modelData.columns)

                                                        x: 5 + (modelData.slot * slotWidth)
                                                        y: minutesFromStart * (weekTimelineContent.hourHeight / 60.0)
                                                        width: Math.max(slotWidth - 3, 18)
                                                        height: Math.max(durationMinutes * (weekTimelineContent.hourHeight / 60.0), 22)
                                                        radius: 5
                                                        color: modelData.event.color
                                                        opacity: root.isTentativeEvent(modelData.event) ? 0.45 : 0.78
                                                        clip: true
                                                        z: 20 + modelData.slot
                                                        border.width: root.isTentativeEvent(modelData.event) ? 1 : 0
                                                        border.color: root.isTentativeEvent(modelData.event) ? "#fbbf24" : "transparent"

                                                        Column {
                                                            anchors.fill: parent
                                                            anchors.margins: 4
                                                            spacing: 1
                                                            Text {
                                                                text: (root.isTentativeEvent(modelData.event) ? "! " : "") + modelData.event.title
                                                                color: root.ncText
                                                                font.pixelSize: 12
                                                                font.bold: true
                                                                elide: Text.ElideRight
                                                                width: parent.width
                                                            }
                                                            Text {
                                                                text: root.formatEventTime(new Date(modelData.event.startMs))
                                                                color: root.ncMutedText
                                                                font.pixelSize: 10
                                                            }
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    visible: isToday
                                                    x: 0
                                                    y: {
                                                        var now = new Date()
                                                        var minutes = (now.getHours() * 60) + now.getMinutes() + (now.getSeconds() / 60.0)
                                                        return Math.max(0, Math.min(minutes * (weekTimelineContent.hourHeight / 60.0), (weekTimelineContent.hourHeight * 24) - 2))
                                                    }
                                                    width: parent.width
                                                    height: 2
                                                    color: "#ff6b6b"
                                                    z: 50
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Month view ────────────────────────────────────────
            Rectangle {
                id: monthViewPanel
                color: root.ncPanel; radius: 16
                border.width: 1; border.color: root.ncBorder; clip: true

                property var currentMonth: root.monthInfo(0)

                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: Qt.formatDate(monthViewPanel.currentMonth.first, "MMMM yyyy")
                        color: root.ncText; font.pixelSize: 24; font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 4
                        Repeater {
                            model: root.weekdayNames
                            delegate: Text {
                                Layout.fillWidth: true; text: modelData
                                color: root.ncSubtleText; font.pixelSize: 13; font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        columns: 7; rowSpacing: 4; columnSpacing: 4
                        Repeater {
                            model: monthViewPanel.currentMonth.cells
                            delegate: Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true; radius: 6; clip: true
                                property bool inMonth: modelData.inMonth
                                property var cellDate: modelData.date
                                property bool isToday: inMonth && root.isSameDate(cellDate, new Date())
                                property var dayEvents: inMonth ? root.eventsForDay(cellDate) : []
                                color: isToday ? root.ncPrimaryStrong : (inMonth ? root.ncPanelAlt : "#0f1f2f")
                                border.width: isToday ? 1 : 0; border.color: root.ncAccent
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 3; spacing: 2
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: inMonth ? cellDate.getDate() : ""
                                        color: isToday ? root.ncText : (inMonth ? root.ncText : root.ncSubtleText)
                                        font.pixelSize: 14; font.bold: isToday
                                    }
                                    Repeater {
                                        model: dayEvents.slice(0, 3)
                                        delegate: Rectangle {
                                            Layout.fillWidth: true; height: 16; radius: 2; clip: true
                                            color: "#1a344a"
                                            opacity: root.isTentativeEvent(modelData) ? 0.65 : 1.0
                                            border.width: root.isTentativeEvent(modelData) ? 1 : 0
                                            border.color: root.isTentativeEvent(modelData) ? "#fbbf24" : "transparent"
                                            Row {
                                                anchors.fill: parent; spacing: 0
                                                Rectangle { width: 4; height: parent.height; color: modelData.color }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    leftPadding: 3
                                                    text: (root.isTentativeEvent(modelData) ? "! " : "") + root.monthChipText(modelData)
                                                    font.pixelSize: 11; color: root.ncMutedText
                                                    elide: Text.ElideRight; width: parent.width - 5
                                                }
                                            }
                                        }
                                    }
                                    Text {
                                        visible: dayEvents.length > 3
                                        text: "+" + (dayEvents.length - 3) + " more"
                                        color: root.ncSubtleText; font.pixelSize: 10
                                        Layout.alignment: Qt.AlignLeft
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                            }
                        }
                    }
                }
            }

            // ── Two-month view ───────────────────────────────────
            Rectangle {
                id: twoMonthViewPanel
                color: root.ncPanel; radius: 16
                border.width: 1; border.color: root.ncBorder; clip: true

                property var leftMonth: root.monthInfo(0)
                property var rightMonth: root.monthInfo(1)

                RowLayout {
                    anchors.fill: parent; anchors.margins: 12
                    spacing: 10

                    Repeater {
                        model: [twoMonthViewPanel.leftMonth, twoMonthViewPanel.rightMonth]
                        delegate: Rectangle {
                            id: monthCard
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 12
                            color: root.ncPanelAlt
                            border.width: 1
                            border.color: root.ncBorder

                            property var monthData: modelData

                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 10; spacing: 6
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: Qt.formatDate(monthCard.monthData.first, "MMMM yyyy")
                                    color: root.ncText; font.pixelSize: 22; font.bold: true
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 4
                                    Repeater {
                                        model: root.weekdayNames
                                        delegate: Text {
                                            Layout.fillWidth: true; text: modelData
                                            color: root.ncSubtleText; font.pixelSize: 12; font.bold: true
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }
                                GridLayout {
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    columns: 7; rowSpacing: 4; columnSpacing: 4
                                    Repeater {
                                        model: monthData.cells
                                        delegate: Rectangle {
                                            Layout.fillWidth: true; Layout.fillHeight: true; radius: 6; clip: true
                                            property bool inMonth: modelData.inMonth
                                            property var cellDate: modelData.date
                                            property bool isToday: inMonth && root.isSameDate(cellDate, new Date())
                                            property var dayEvents: inMonth ? root.eventsForDay(cellDate) : []
                                            color: isToday ? root.ncPrimaryStrong : (inMonth ? root.ncPanel : "#0f1f2f")
                                            border.width: isToday ? 1 : 0; border.color: root.ncAccent

                                            ColumnLayout {
                                                anchors.fill: parent; anchors.margins: 3; spacing: 2
                                                Text {
                                                    Layout.alignment: Qt.AlignHCenter
                                                    text: inMonth ? cellDate.getDate() : ""
                                                    color: isToday ? root.ncText : (inMonth ? root.ncText : root.ncSubtleText)
                                                    font.pixelSize: 13; font.bold: isToday
                                                }
                                                Repeater {
                                                    model: dayEvents.slice(0, 2)
                                                    delegate: Rectangle {
                                                        Layout.fillWidth: true; height: 16; radius: 2; clip: true
                                                        color: "#1a344a"
                                                        opacity: root.isTentativeEvent(modelData) ? 0.65 : 1.0
                                                        border.width: root.isTentativeEvent(modelData) ? 1 : 0
                                                        border.color: root.isTentativeEvent(modelData) ? "#fbbf24" : "transparent"
                                                        Row {
                                                            anchors.fill: parent; spacing: 0
                                                            Rectangle { width: 4; height: parent.height; color: modelData.color }
                                                            Text {
                                                                anchors.verticalCenter: parent.verticalCenter
                                                                leftPadding: 3
                                                                text: (root.isTentativeEvent(modelData) ? "! " : "") + root.monthChipText(modelData)
                                                                font.pixelSize: 10; color: root.ncMutedText
                                                                elide: Text.ElideRight; width: parent.width - 5
                                                            }
                                                        }
                                                    }
                                                }
                                                Text {
                                                    visible: dayEvents.length > 2
                                                    text: "+" + (dayEvents.length - 2) + " more"
                                                    color: root.ncSubtleText; font.pixelSize: 10
                                                }
                                                Item { Layout.fillHeight: true }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        } // Item wrapper for StackLayout

        // Footer
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                visible: windowedMode
                text: feedManager.busy ? "Refreshing..." : "Refresh Now"
                enabled: !feedManager.busy; onClicked: feedManager.refreshFeeds()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        z: 9999
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        cursorShape: windowedMode ? Qt.ArrowCursor : Qt.BlankCursor
    }

    // ── Setup panel overlay ───────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.setupOpen ? 0.45 : 0.0
        visible: root.setupOpen
        z: 50
        MouseArea { anchors.fill: parent; onClicked: root.setupOpen = false }
    }

    Rectangle {
        id: setupPanel
        width: Math.min(540, root.width * 0.85)
        anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
        z: 60; color: root.ncPanelAlt; border.color: root.ncBorder; border.width: 1
        transform: Translate {
            x: root.setupOpen ? 0 : setupPanel.width
            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.InOutQuad } }
        }

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 18; spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Calendar Sources"; color: root.ncText; font.pixelSize: 28; font.bold: true }
                Item { Layout.fillWidth: true }
                Button { text: "Close"; onClicked: root.setupOpen = false }
            }

            Label { text: "Use shared ICS/webcal links or local .ics files."; color: root.ncMutedText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: "Calendars (Name | URL)"; color: root.ncText }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(feedListModel.count * 54 + 10, 220)
                clip: true
                ListView {
                    id: feedListView
                    model: feedListModel
                    spacing: 8
                    delegate: RowLayout {
                        width: feedListView.width
                        spacing: 6
                        TextField {
                            text: model.name; placeholderText: "Name"
                            implicitWidth: 130
                            onTextEdited: feedListModel.setProperty(index, "name", text)
                        }
                        TextField {
                            text: model.url; placeholderText: "webcal://, https://, /path/file.ics, or file:///..."
                            Layout.fillWidth: true
                            onTextEdited: feedListModel.setProperty(index, "url", text)
                        }
                        Button {
                            text: "Browse"
                            onClicked: {
                                var selectedPath = feedManager.pickLocalIcsFile()
                                if (!selectedPath)
                                    return

                                feedListModel.setProperty(index, "url", selectedPath)
                                var currentItem = feedListModel.get(index)
                                var currentName = (currentItem.name || "").trim()
                                if (!currentName) {
                                    var name = root.feedNameFromLocation(selectedPath)
                                    feedListModel.setProperty(index, "name", name)
                                }
                            }
                        }
                        Button {
                            text: "✕"; implicitWidth: 36
                            onClicked: feedListModel.remove(index)
                        }
                    }
                }
            }

            Button {
                text: "+ Add Calendar"; Layout.alignment: Qt.AlignLeft
                onClicked: feedListModel.append({name: "", url: ""})
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Button {
                    text: "Save"; enabled: !feedManager.busy
                    onClicked: { feedManager.feedUrls = root.serializeFeedList(); feedManager.saveSettings() }
                }
                Button {
                    text: feedManager.busy ? "Refreshing..." : "Refresh Feeds"; enabled: !feedManager.busy
                    onClicked: { feedManager.feedUrls = root.serializeFeedList(); feedManager.saveSettings(); feedManager.refreshFeeds() }
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Label { text: "Automatic refresh"; color: root.ncText }
                Switch {
                    id: autoRefreshSwitch; checked: feedManager.autoRefreshEnabled; enabled: !feedManager.busy
                    onToggled: { feedManager.autoRefreshEnabled = checked; feedManager.saveSettings() }
                }
                Label { text: "Every"; color: root.ncText }
                SpinBox {
                    id: refreshIntervalSpin; from: 1; to: 180; value: feedManager.refreshIntervalMinutes; editable: true
                    enabled: !feedManager.busy && autoRefreshSwitch.checked
                    onValueModified: { feedManager.refreshIntervalMinutes = value; feedManager.saveSettings() }
                }
                Label { text: "min"; color: root.ncText }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Label { text: "Display name"; color: root.ncText }
                TextField {
                    id: displayNameField
                    Layout.fillWidth: true
                    text: feedManager.displayName
                    placeholderText: "Family Calendar"
                    enabled: !feedManager.busy
                    onEditingFinished: {
                        feedManager.displayName = text
                        feedManager.saveSettings()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Label { text: "Time format"; color: root.ncText }
                Switch {
                    id: timeFormatSwitch
                    checked: !root.use12HourClock
                    enabled: !feedManager.busy
                    onClicked: {
                        feedManager.timeFormatPreference = checked ? 2 : 1
                        root.currentTime = root.formatClockTime(new Date())
                        feedManager.saveSettings()
                    }
                }
                Label { text: timeFormatSwitch.checked ? "24-hour" : "12-hour"; color: root.ncText }
                Button {
                    text: "Host default"
                    enabled: !feedManager.busy
                    onClicked: {
                        feedManager.timeFormatPreference = 0
                        root.currentTime = root.formatClockTime(new Date())
                        feedManager.saveSettings()
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.use12HourClock ? "Preview: 9:05 PM" : "Preview: 21:05"
                    color: root.ncSubtleText
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Label { text: "Week starts Sunday"; color: root.ncText }
                Switch {
                    checked: feedManager.sundayFirst
                    enabled: !feedManager.busy
                    onToggled: {
                        feedManager.sundayFirst = checked
                        feedManager.saveSettings()
                    }
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Label { text: "View cycle"; color: root.ncText }
                SpinBox {
                    from: 5; to: 300; value: root.viewCycleSecs; editable: true
                    onValueModified: { root.viewCycleSecs = value; viewCycleTimer.restart(); root.startCycle() }
                }
                Label { text: "sec"; color: root.ncText }
                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true; color: root.ncPanel; radius: 8
                border.width: 1; border.color: root.ncBorder
                implicitHeight: roStatusText.implicitHeight + 16
                Text {
                    id: roStatusText; anchors.fill: parent; anchors.margins: 8
                    text: feedManager.statusMessage; color: root.ncMutedText; wrapMode: Text.WordWrap; font.pixelSize: 16
                }
            }
        }
    }
}

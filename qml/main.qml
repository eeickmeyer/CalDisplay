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
    property var currentDate: new Date()
    property double lastClockTickMs: 0
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
    readonly property int eventAllDayTextSize: 16
    readonly property int eventListTimeTextSize: 16
    readonly property int eventListTitleTextSize: 22
    readonly property int eventListMetaTextSize: 15
    readonly property int timelineEventTitleTextSize: 16
    readonly property int timelineEventMetaTextSize: 13
    readonly property int monthChipTextSize: 13
    readonly property int compactMonthChipTextSize: 12
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
    property int  currentView:   0      // stack index: 0=Day, 1=Week, 2=Month, 3=Two Months, 4=Weather
    property int  viewCycleSecs: 20
    property real cycleProgress: 0.0
    property var  allEvents:     []
    readonly property var allViewDefs: [
        { key: "day", label: "Day", index: 0 },
        { key: "week", label: "Week", index: 1 },
        { key: "month", label: "Month", index: 2 },
        { key: "twomonths", label: "Two Months", index: 3 },
        { key: "weather", label: "Weather", index: 4 }
    ]
    readonly property var visibleViewKeys: root.parseVisibleViews(feedManager.visibleViews)
    readonly property var activeViews: {
        var filtered = []
        for (var i = 0; i < root.visibleViewKeys.length; i++) {
            var def = root.viewDefForKey(root.visibleViewKeys[i])
            if (def)
                filtered.push(def)
        }
        return filtered
    }
    readonly property var calendarLegend: {
        var seen = {}
        var result = []
        for (var i = 0; i < allEvents.length; i++) {
            var ev = allEvents[i]
            if (!seen[ev.calendar]) {
                seen[ev.calendar] = true
                result.push({name: ev.calendar, color: ev.color})
            }
        }
        return result
    }

    function refreshEventData() {
        allEvents = eventModel.getEvents()
    }

    function hexToRgb(colorString) {
        var hex = (colorString || "").toString().trim()
        if (hex.charAt(0) === "#")
            hex = hex.slice(1)

        if (hex.length === 3) {
            return {
                r: parseInt(hex.charAt(0) + hex.charAt(0), 16),
                g: parseInt(hex.charAt(1) + hex.charAt(1), 16),
                b: parseInt(hex.charAt(2) + hex.charAt(2), 16)
            }
        }

        if (hex.length === 6 || hex.length === 8) {
            var offset = hex.length === 8 ? 2 : 0
            return {
                r: parseInt(hex.substr(offset, 2), 16),
                g: parseInt(hex.substr(offset + 2, 2), 16),
                b: parseInt(hex.substr(offset + 4, 2), 16)
            }
        }

        return null
    }

    function relativeLuminance(rgb) {
        function channelToLinear(v) {
            var c = v / 255.0
            return c <= 0.03928 ? (c / 12.92) : Math.pow((c + 0.055) / 1.055, 2.4)
        }

        var r = channelToLinear(rgb.r)
        var g = channelToLinear(rgb.g)
        var b = channelToLinear(rgb.b)
        return 0.2126 * r + 0.7152 * g + 0.0722 * b
    }

    function compositeRgb(fgColor, fgAlpha, bgColor) {
        var fg = root.hexToRgb(fgColor)
        var bg = root.hexToRgb(bgColor)
        if (!fg)
            return bg
        if (!bg)
            return fg

        var a = Math.max(0.0, Math.min(1.0, fgAlpha))
        return {
            r: Math.round((fg.r * a) + (bg.r * (1.0 - a))),
            g: Math.round((fg.g * a) + (bg.g * (1.0 - a))),
            b: Math.round((fg.b * a) + (bg.b * (1.0 - a)))
        }
    }

    function useBlackTextOnColor(colorString, alpha, underColor) {
        var rgb = (alpha !== undefined && underColor)
                ? root.compositeRgb(colorString, alpha, underColor)
                : root.hexToRgb(colorString)
        if (!rgb)
            return false

        var lum = root.relativeLuminance(rgb)
        var contrastWhite = (1.0 + 0.05) / (lum + 0.05)
        var contrastBlack = (lum + 0.05) / (0.0 + 0.05)
        return contrastBlack > contrastWhite
    }

    function eventTitleColor(eventColor, alpha, underColor) {
        return root.useBlackTextOnColor(eventColor, alpha, underColor) ? "#111111" : "#ffffff"
    }

    function eventMetaColor(eventColor, alpha, underColor) {
        return root.useBlackTextOnColor(eventColor, alpha, underColor) ? "#111111" : "#ffffff"
    }

    function parseVisibleViews(raw) {
        var allowed = ["day", "week", "month", "twomonths", "weather"]
        var keys = []
        var input = (raw || "").split(",")
        for (var i = 0; i < input.length; i++) {
            var k = input[i].trim().toLowerCase()
            if (!k || allowed.indexOf(k) === -1 || keys.indexOf(k) !== -1)
                continue
            keys.push(k)
        }
        if (keys.length === 0)
            keys.push("day")
        return keys
    }

    function serializeVisibleViews(keys) {
        return keys.join(",")
    }

    function viewDefForKey(key) {
        for (var i = 0; i < root.allViewDefs.length; i++) {
            if (root.allViewDefs[i].key === key)
                return root.allViewDefs[i]
        }
        return null
    }

    function viewLabelForKey(key) {
        var def = root.viewDefForKey(key)
        return def ? def.label : key
    }

    function isViewEnabled(key) {
        return root.visibleViewKeys.indexOf(key) !== -1
    }

    function firstEnabledViewIndex() {
        return root.activeViews.length > 0 ? root.activeViews[0].index : 0
    }

    function ensureCurrentViewEnabled() {
        var enabled = false
        for (var i = 0; i < root.activeViews.length; i++) {
            if (root.activeViews[i].index === root.currentView) {
                enabled = true
                break
            }
        }
        if (!enabled)
            root.currentView = root.firstEnabledViewIndex()
    }

    function nextEnabledViewIndex(fromIndex) {
        if (root.activeViews.length === 0)
            return 0

        var currentPos = -1
        for (var i = 0; i < root.activeViews.length; i++) {
            if (root.activeViews[i].index === fromIndex) {
                currentPos = i
                break
            }
        }

        if (currentPos === -1)
            return root.activeViews[0].index

        return root.activeViews[(currentPos + 1) % root.activeViews.length].index
    }

    function setViewEnabled(key, enabled) {
        var keys = root.visibleViewKeys.slice(0)
        var idx = keys.indexOf(key)

        if (enabled) {
            if (idx === -1)
                keys.push(key)
        } else {
            if (idx === -1 || keys.length <= 1)
                return
            keys.splice(idx, 1)
        }

        feedManager.visibleViews = root.serializeVisibleViews(keys)
        feedManager.saveSettings()
        root.ensureCurrentViewEnabled()
        viewCycleTimer.restart()
        root.startCycle()
    }

    function moveVisibleView(fromIndex, toIndex) {
        var keys = root.visibleViewKeys.slice(0)
        if (fromIndex < 0 || fromIndex >= keys.length)
            return
        if (toIndex < 0 || toIndex >= keys.length)
            return
        if (fromIndex === toIndex)
            return

        var moved = keys.splice(fromIndex, 1)[0]
        keys.splice(toIndex, 0, moved)

        feedManager.visibleViews = root.serializeVisibleViews(keys)
        feedManager.saveSettings()
        root.ensureCurrentViewEnabled()
        viewCycleTimer.restart()
        root.startCycle()
    }

    function startCycle() {
        cycleProgress = 0
        progressAnim.restart()
    }

    function isSameDate(a, b) {
        return a.getFullYear() === b.getFullYear() &&
               a.getMonth() === b.getMonth() &&
               a.getDate() === b.getDate()
    }

    function dayStartMs(dayDate) {
        var d = new Date(dayDate)
        d.setHours(0, 0, 0, 0)
        return d.getTime()
    }

    function formatEventTime(dateObj) {
        return Qt.formatTime(dateObj, root.eventTimePattern)
    }

    function formatClockTime(dateObj) {
        return Qt.formatTime(dateObj, root.clockTimePattern)
    }

    function formatTimelineHour(hour) {
        if (root.use12HourClock) {
            var h = hour % 12
            if (h === 0)
                h = 12
            return h + (hour < 12 ? " AM" : " PM")
        }
        return (hour < 10 ? "0" : "") + hour + ":00"
    }

    function isTentativeEvent(eventObj) {
        var status = (eventObj && eventObj.status ? eventObj.status : "").toString().toUpperCase()
        return status === "TENTATIVE"
    }

    function isAllDayEvent(eventObj) {
        var start = new Date(eventObj.startMs)
        var end = new Date(eventObj.endMs)
        var duration = eventObj.endMs - eventObj.startMs
        var wholeDays = duration > 0 && (duration % (24 * 60 * 60 * 1000) === 0)
        return start.getHours() === 0 && start.getMinutes() === 0 &&
               end.getHours() === 0 && end.getMinutes() === 0 && wholeDays
    }

    function eventsForDay(dayDate) {
        var startMs = root.dayStartMs(dayDate)
        var endMs = startMs + (24 * 60 * 60 * 1000)
        var out = []
        for (var i = 0; i < allEvents.length; i++) {
            var ev = allEvents[i]
            if (ev.endMs <= startMs || ev.startMs >= endMs)
                continue
            out.push(ev)
        }
        out.sort(function(a, b) {
            if (a.startMs !== b.startMs)
                return a.startMs - b.startMs
            return a.endMs - b.endMs
        })
        return out
    }

    function allDayEventsForDay(dayDate) {
        var dayEvents = root.eventsForDay(dayDate)
        var out = []
        for (var i = 0; i < dayEvents.length; i++) {
            if (root.isAllDayEvent(dayEvents[i]))
                out.push(dayEvents[i])
        }
        return out
    }

    function timedEventsForDay(dayDate) {
        var dayEvents = root.eventsForDay(dayDate)
        var out = []
        for (var i = 0; i < dayEvents.length; i++) {
            if (!root.isAllDayEvent(dayEvents[i]))
                out.push(dayEvents[i])
        }
        return out
    }

    function monthChipText(eventObj) {
        if (root.isAllDayEvent(eventObj))
            return eventObj.title
        return root.formatEventTime(new Date(eventObj.startMs)) + " " + eventObj.title
    }

    function timelineLayoutForDay(dayDate, timedEvents) {
        var dayStart = root.dayStartMs(dayDate)
        var dayEnd = dayStart + (24 * 60 * 60 * 1000)
        var sorted = timedEvents.slice(0)
        sorted.sort(function(a, b) {
            if (a.startMs !== b.startMs)
                return a.startMs - b.startMs
            return a.endMs - b.endMs
        })

        var active = []
        var out = []

        for (var i = 0; i < sorted.length; i++) {
            var ev = sorted[i]
            var startMs = Math.max(ev.startMs, dayStart)
            var endMs = Math.min(ev.endMs, dayEnd)
            if (endMs <= startMs)
                endMs = startMs + 60000

            var stillActive = []
            for (var j = 0; j < active.length; j++) {
                if (active[j].endMs > startMs)
                    stillActive.push(active[j])
            }
            active = stillActive

            var used = {}
            for (j = 0; j < active.length; j++)
                used[active[j].slot] = true

            var slot = 0
            while (used[slot])
                slot++

            active.push({ endMs: endMs, slot: slot })

            out.push({
                event: ev,
                startMs: startMs,
                endMs: endMs,
                slot: slot,
                columns: Math.max(1, active.length)
            })
        }

        for (i = 0; i < out.length; i++) {
            var cols = 1
            for (j = 0; j < out.length; j++) {
                var a = out[i]
                var b = out[j]
                var overlaps = !(a.endMs <= b.startMs || b.endMs <= a.startMs)
                if (overlaps)
                    cols = Math.max(cols, b.slot + 1)
            }
            out[i].columns = Math.max(out[i].columns, cols)
        }

        return out
    }

    function scrollDayTimelineToNow() {
        if (typeof dayTimelineFlick === "undefined" || typeof dayViewPanel === "undefined") {
            return
        }
        if (root.currentView !== 0 || root.setupOpen) {
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
        var d = new Date(root.currentDate)
        d.setHours(0, 0, 0, 0)
        var dow = d.getDay()
        d.setDate(d.getDate() - (feedManager.sundayFirst ? dow : (dow === 0 ? 6 : dow - 1)))
        var out = []
        for (var i = 0; i < 7; i++) { out.push(new Date(d)); d.setDate(d.getDate() + 1) }
        return out
    }

    function monthInfo(monthOffset) {
        var today = new Date(root.currentDate)
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
        root.lastClockTickMs = (new Date()).getTime()
        refreshEventData()
        root.ensureCurrentViewEnabled()
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
    onVisibleViewKeysChanged: root.ensureCurrentViewEnabled()
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
        onTriggered: {
            var now = new Date()
            var nowMs = now.getTime()

            // Run staleness checks continuously; each manager decides whether
            // enough time has elapsed before issuing a real network refresh.
            feedManager.refreshFeedsIfDue()
            weatherManager.refreshWeatherIfDue()

            // Detect a large timer gap to catch resume-from-suspend reliably.
            if (root.lastClockTickMs > 0 && (nowMs - root.lastClockTickMs) > 70000) {
                root.currentDate = now
                root.refreshEventData()
                weatherManager.refreshWeatherIfDue()
            }

            root.lastClockTickMs = nowMs
            root.currentTime = root.formatClockTime(now)
        }
    }

    Timer {
        id: midnightWatchTimer
        interval: 60000; running: true; repeat: true
        onTriggered: {
            var now = new Date()
            if (!root.isSameDate(now, root.currentDate)) {
                root.currentDate = now
                root.refreshEventData()
                weatherManager.refreshWeatherIfDue()
            }
        }
    }

    // Catch the case where the system was asleep during midnight rollover:
    // Qt timers are paused during sleep, so the midnightWatchTimer may not
    // fire at midnight.  Re-check the date as soon as the app becomes active.
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) {
                var now = new Date()
                if (!root.isSameDate(now, root.currentDate)) {
                    root.currentDate = now
                    root.refreshEventData()
                }
                weatherManager.refreshWeatherIfDue()
            }
        }
    }

    Timer {
        id: viewCycleTimer
        interval: root.viewCycleSecs * 1000
        running: !root.setupOpen && root.activeViews.length > 1; repeat: true
        onTriggered: {
            var next = root.nextEnabledViewIndex(root.currentView)
            root.currentView = next
            root.startCycle()
        }
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
                text: Qt.formatDateTime(root.currentDate, "ddd yyyy-MM-dd")
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
                    model: root.activeViews
                    delegate: Rectangle {
                        height: 34; implicitWidth: tabLabel.implicitWidth + 28; radius: 6
                        color: root.currentView === modelData.index ? root.ncPrimaryStrong : root.ncPanelAlt
                        border.width: root.currentView === modelData.index ? 1 : 0
                        border.color: root.ncAccent
                        Text {
                            id: tabLabel; anchors.centerIn: parent; text: modelData.label
                            color: root.currentView === modelData.index ? root.ncText : root.ncSubtleText
                            font.pixelSize: 17; font.bold: root.currentView === modelData.index
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { root.currentView = modelData.index; viewCycleTimer.restart(); root.startCycle() }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.activeViews.length > 1 ? 3 : 0
                visible: root.activeViews.length > 1
                height: 3; color: root.ncPanelAlt; radius: 1
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

                property var dayDate: new Date(root.currentDate)
                property var dayAllDayEvents: root.allDayEventsForDay(dayDate)
                property var dayTimedEvents: root.timedEventsForDay(dayDate)
                property var dayTimelineEvents: root.timelineLayoutForDay(dayDate, dayTimedEvents)
                property int hourHeight: 70
                property int timelineHeight: 24 * hourHeight
                property bool isToday: root.isSameDate(dayDate, root.currentDate)
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
                                            implicitHeight: 34
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
                                                    font.pixelSize: root.eventAllDayTextSize
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
                                        height: 88
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
                                                    font.pixelSize: root.eventListTimeTextSize
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }

                                                Text {
                                                    text: (root.isTentativeEvent(modelData) ? "! " : "") + modelData.title
                                                    color: root.ncText
                                                    font.pixelSize: root.eventListTitleTextSize
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }

                                                Text {
                                                    text: modelData.calendar + (root.isTentativeEvent(modelData) ? " (Tentative)" : "")
                                                    color: root.ncMutedText
                                                    font.pixelSize: root.eventListMetaTextSize
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
                                                                color: root.eventTitleColor(
                                                                    modelData.event.color,
                                                                    root.isTentativeEvent(modelData.event) ? 0.45 : 0.78,
                                                                    "#0d1e2e")
                                                                font.pixelSize: root.timelineEventTitleTextSize
                                                                font.bold: true
                                                                elide: Text.ElideRight
                                                                width: parent.width
                                                            }

                                                            Text {
                                                                text: root.formatEventTime(new Date(modelData.event.startMs)) + " - " +
                                                                      root.formatEventTime(new Date(modelData.event.endMs))
                                                                color: root.eventMetaColor(
                                                                    modelData.event.color,
                                                                    root.isTentativeEvent(modelData.event) ? 0.45 : 0.78,
                                                                    "#0d1e2e")
                                                                font.pixelSize: root.timelineEventMetaTextSize
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
                        Layout.preferredHeight: 36

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
                                    border.color: root.isSameDate(modelData, root.currentDate) ? root.ncAccent : root.ncBorder

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
                                                font.pixelSize: root.timelineEventMetaTextSize
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
                                        border.color: root.isSameDate(modelData, root.currentDate) ? root.ncAccent : root.ncBorder
                                        clip: true

                                        property var colDate: modelData
                                        property bool isToday: root.isSameDate(colDate, root.currentDate)
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
                                                                color: root.eventTitleColor(
                                                                    modelData.event.color,
                                                                    root.isTentativeEvent(modelData.event) ? 0.45 : 0.78,
                                                                    root.ncPanelAlt)
                                                                font.pixelSize: root.timelineEventTitleTextSize
                                                                font.bold: true
                                                                elide: Text.ElideRight
                                                                width: parent.width
                                                            }
                                                            Text {
                                                                text: root.formatEventTime(new Date(modelData.event.startMs))
                                                                color: root.eventMetaColor(
                                                                    modelData.event.color,
                                                                    root.isTentativeEvent(modelData.event) ? 0.45 : 0.78,
                                                                    root.ncPanelAlt)
                                                                font.pixelSize: root.timelineEventMetaTextSize
                                                            }
                                                        }
                                                    }
                                                }

                                                Rectangle {
                                                    visible: isToday
                                                    x: 0
                                                    y: {
                                                        var _tick = root.currentTime
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
                                property bool isToday: inMonth && root.isSameDate(cellDate, root.currentDate)
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
                                            Layout.fillWidth: true; height: 20; radius: 3; clip: true
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
                                                    font.pixelSize: root.monthChipTextSize; color: root.ncMutedText
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
                                            property bool isToday: inMonth && root.isSameDate(cellDate, root.currentDate)
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
                                                        Layout.fillWidth: true; height: 18; radius: 3; clip: true
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
                                                                font.pixelSize: root.compactMonthChipTextSize; color: root.ncMutedText
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

        // ── Weather view (index 4) ────────────────────────────────
        Rectangle {
            color: root.ncPanel; radius: 16
            border.width: 1; border.color: root.ncBorder; clip: true

            // Not-configured placeholder
            ColumnLayout {
                anchors.centerIn: parent
                visible: !weatherManager.configured
                spacing: 12
                Image {
                    source: ""
                    sourceSize.width: 72; sourceSize.height: 72
                    fillMode: Image.PreserveAspectFit
                    Layout.preferredWidth: 72; Layout.preferredHeight: 72
                    Layout.alignment: Qt.AlignHCenter
                }
                Text { text: "Weather not configured"; font.pixelSize: 24; color: root.ncMutedText; Layout.alignment: Qt.AlignHCenter }
                Text { text: "Enter a location in Settings"; font.pixelSize: 18; color: root.ncSubtleText; Layout.alignment: Qt.AlignHCenter }
            }

            // Two-column weather layout
            RowLayout {
                id: weatherRow
                anchors.fill: parent; anchors.margins: 14; spacing: 14
                visible: weatherManager.configured

                // ── Left column: current conditions + hourly ──────────────
                ColumnLayout {
                    Layout.preferredWidth: Math.max(280, weatherRow.width * 0.34)
                    Layout.minimumWidth: 280
                    Layout.maximumWidth: Math.max(280, weatherRow.width * 0.34)
                    Layout.fillHeight: true; spacing: 12

                    // Current conditions card
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 264
                        color: root.ncPanelAlt; radius: 12
                        border.width: 1; border.color: root.ncBorder

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 16; spacing: 6
                            Text {
                                text: weatherManager.locationName || weatherManager.locationQuery
                                color: root.ncMutedText; font.pixelSize: 18
                                elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            RowLayout {
                                spacing: 8
                                Image {
                                    source: weatherManager.currentWeather.iconPath || ""
                                    sourceSize.width: 52; sourceSize.height: 52
                                    fillMode: Image.PreserveAspectFit
                                    Layout.preferredWidth: 52; Layout.preferredHeight: 52
                                }
                                Text { text: weatherManager.currentWeather.tempStr || "--"; color: root.ncText; font.pixelSize: 56; font.bold: true }
                            }
                            Text {
                                text: weatherManager.currentWeather.description || (weatherManager.busy ? "Loading\u2026" : "No data")
                                color: root.ncMutedText; font.pixelSize: 20
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: root.ncBorder }
                            Grid {
                                columns: 2; columnSpacing: 14; rowSpacing: 3
                                Text { text: "Feels like"; color: root.ncSubtleText; font.pixelSize: 18 }
                                Text { text: weatherManager.currentWeather.feelsLikeStr || "--"; color: root.ncText; font.pixelSize: 18 }
                                Text { text: "Humidity"; color: root.ncSubtleText; font.pixelSize: 18 }
                                Text {
                                    text: weatherManager.currentWeather.humidity !== undefined ? weatherManager.currentWeather.humidity + "%" : "--"
                                    color: root.ncText; font.pixelSize: 18
                                }
                                Text { text: "Wind"; color: root.ncSubtleText; font.pixelSize: 18 }
                                Text { text: weatherManager.currentWeather.windStr || "--"; color: root.ncText; font.pixelSize: 18 }
                            }
                            Item { Layout.fillHeight: true }
                            Text {
                                text: weatherManager.statusMessage
                                color: root.ncSubtleText; font.pixelSize: 14
                                wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                    }

                    // Hourly forecast card
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        color: root.ncPanelAlt; radius: 12
                        border.width: 1; border.color: root.ncBorder; clip: true

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 2
                            Text { text: "Today \u00B7 Hourly"; color: root.ncMutedText; font.pixelSize: 16; font.bold: true; Layout.bottomMargin: 2 }
                            Repeater {
                                model: weatherManager.hourlyForecast
                                delegate: RowLayout {
                                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                                    Text { text: modelData.timeText; color: root.ncSubtleText; font.pixelSize: 16; Layout.preferredWidth: 54 }
                                    Image {
                                        source: modelData.iconPath || ""
                                        sourceSize.width: 22; sourceSize.height: 22
                                        fillMode: Image.PreserveAspectFit
                                        Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                    }
                                    Text { text: modelData.tempStr; color: root.ncText; font.pixelSize: 16; Layout.preferredWidth: 54 }
                                    Text {
                                        text: (modelData.precipType ? modelData.precipType + " " : "") + modelData.precipProb + "%"
                                        color: modelData.precipProb > 30 ? root.ncAccent : root.ncSubtleText
                                        font.pixelSize: 15
                                        visible: modelData.precipProb > 0
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }
                    }
                }

                // ── Right column: extended daily forecast ─────────────────
                Rectangle {
                    id: dailyForecastPanel
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.minimumWidth: 320
                    color: root.ncPanelAlt; radius: 12
                    border.width: 1; border.color: root.ncBorder; clip: true

                    property real availableHeight: Math.max(100, height - 60)
                    property int forecastCount: weatherManager.dailyForecast.length
                    property real itemHeight: forecastCount > 0 ? Math.max(28, availableHeight / forecastCount) : 40
                    property real baseFontSize: Math.max(16, Math.min(22, itemHeight * 0.5))
                    property real iconSize: Math.max(20, Math.min(28, itemHeight * 0.6))

                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16; spacing: 6
                        Text { text: "Extended Forecast"; color: root.ncMutedText; font.pixelSize: 20; font.bold: true; Layout.bottomMargin: 4 }
                        
                        // Weather Alerts with auto-scroll
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: weatherManager.weatherAlerts.length > 0 ? 84 : 0
                            visible: weatherManager.weatherAlerts.length > 0
                            
                            Rectangle {
                                anchors.fill: parent
                                color: "#cc3311"
                                radius: 8
                                border.width: 2
                                border.color: "#ff4422"
                                
                                SwipeView {
                                    id: alertsSwipeView
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    clip: true
                                    interactive: false
                                    
                                    Repeater {
                                        model: weatherManager.weatherAlerts
                                        delegate: RowLayout {
                                            spacing: 8

                                            Item {
                                                Layout.preferredWidth: 24
                                                Layout.preferredHeight: 24

                                                Image {
                                                    anchors.fill: parent
                                                    source: modelData.iconPath || ""
                                                    sourceSize.width: 24
                                                    sourceSize.height: 24
                                                    fillMode: Image.PreserveAspectFit
                                                    visible: !!source
                                                }

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "!"
                                                    font.pixelSize: 20
                                                    font.bold: true
                                                    color: "#ffffff"
                                                    visible: !(modelData.iconPath && modelData.iconPath.length > 0)
                                                }
                                            }
                                            
                                            Text {
                                                text: modelData.event + (modelData.headline ? "\n" + modelData.headline : "")
                                                color: "#ffffff"
                                                font.pixelSize: 15
                                                font.bold: true
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                    }
                                    
                                    Timer {
                                        interval: 5000
                                        running: alertsSwipeView.count > 1
                                        repeat: true
                                        onTriggered: {
                                            if (alertsSwipeView.currentIndex < alertsSwipeView.count - 1)
                                                alertsSwipeView.currentIndex++
                                            else
                                                alertsSwipeView.currentIndex = 0
                                        }
                                    }
                                }
                                
                                PageIndicator {
                                    visible: alertsSwipeView.count > 1
                                    count: alertsSwipeView.count
                                    currentIndex: alertsSwipeView.currentIndex
                                    anchors.bottom: parent.bottom
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottomMargin: 4
                                    
                                    delegate: Rectangle {
                                        width: 6
                                        height: 6
                                        radius: 3
                                        color: index === alertsSwipeView.currentIndex ? "#ffffff" : "#ffffff80"
                                    }
                                }
                            }
                        }
                        
                        Repeater {
                            model: weatherManager.dailyForecast
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.preferredHeight: dailyForecastPanel.itemHeight
                                color: "transparent"
                                border.width: 0
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 4; anchors.rightMargin: 4
                                    spacing: 10
                                    Text {
                                        text: modelData.dayName
                                        color: index === 0 ? root.ncText : root.ncMutedText
                                        font.pixelSize: dailyForecastPanel.baseFontSize
                                        font.bold: index === 0
                                        Layout.preferredWidth: 110
                                        Layout.minimumWidth: 110
                                        elide: Text.ElideRight
                                    }
                                    Image {
                                        source: modelData.iconPath || ""
                                        sourceSize.width: dailyForecastPanel.iconSize
                                        sourceSize.height: dailyForecastPanel.iconSize
                                        fillMode: Image.PreserveAspectFit
                                        Layout.preferredWidth: dailyForecastPanel.iconSize
                                        Layout.preferredHeight: dailyForecastPanel.iconSize
                                        Layout.minimumWidth: dailyForecastPanel.iconSize
                                    }
                                    Text {
                                        text: modelData.description
                                        color: root.ncSubtleText
                                        font.pixelSize: dailyForecastPanel.baseFontSize * 0.9
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                    }
                                    Text {
                                        text: modelData.tempMaxStr
                                        color: root.ncText
                                        font.pixelSize: dailyForecastPanel.baseFontSize
                                        font.bold: true
                                        Layout.preferredWidth: 64
                                        Layout.minimumWidth: 64
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    Text {
                                        text: modelData.tempMinStr
                                        color: root.ncSubtleText
                                        font.pixelSize: dailyForecastPanel.baseFontSize
                                        Layout.preferredWidth: 64
                                        Layout.minimumWidth: 64
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    Text {
                                        text: modelData.precipProb > 0 && modelData.precipType ? modelData.precipType : ""
                                        color: modelData.precipProb > 30 ? root.ncAccent : root.ncSubtleText
                                        font.pixelSize: dailyForecastPanel.baseFontSize * 0.85
                                        Layout.preferredWidth: 60
                                        Layout.minimumWidth: 60
                                        horizontalAlignment: Text.AlignRight
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: modelData.precipProb > 0 ? modelData.precipProb + "%" : "-"
                                        color: modelData.precipProb > 30 ? root.ncAccent : root.ncSubtleText
                                        font.pixelSize: dailyForecastPanel.baseFontSize * 0.85
                                        Layout.preferredWidth: 52
                                        Layout.minimumWidth: 52
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }
                        }
                        Text {
                            visible: weatherManager.dailyForecast.length === 0
                            text: weatherManager.busy ? "Loading forecast..." : "No extended forecast data"
                            color: root.ncSubtleText
                            font.pixelSize: 16
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
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
                Text { text: "Settings"; color: root.ncText; font.pixelSize: 28; font.bold: true }
                Item { Layout.fillWidth: true }
                Button { text: "Close"; onClicked: root.setupOpen = false }
            }

            TabBar {
                id: settingsTabBar
                Layout.fillWidth: true
                spacing: 8
                background: Rectangle {
                    color: root.ncPanel
                    radius: 8
                    border.width: 1
                    border.color: root.ncBorder
                }

                TabButton {
                    text: "Calendars & Display"
                    implicitHeight: 40
                    contentItem: Text {
                        text: parent.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: parent.checked ? "#ffffff" : root.ncMutedText
                        font.pixelSize: 15
                        font.bold: parent.checked
                    }
                    background: Rectangle {
                        radius: 6
                        color: parent.checked ? root.ncAccent : "transparent"
                        border.width: parent.checked ? 0 : 1
                        border.color: root.ncBorder
                    }
                }

                TabButton {
                    text: "Weather"
                    implicitHeight: 40
                    contentItem: Text {
                        text: parent.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: parent.checked ? "#ffffff" : root.ncMutedText
                        font.pixelSize: 15
                        font.bold: parent.checked
                    }
                    background: Rectangle {
                        radius: 6
                        color: parent.checked ? root.ncAccent : "transparent"
                        border.width: parent.checked ? 0 : 1
                        border.color: root.ncBorder
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: settingsTabBar.currentIndex

                Item {
                    ScrollView {
                        id: calendarSettingsScroll
                        anchors.fill: parent
                        clip: true

                        ColumnLayout {
                            width: calendarSettingsScroll.availableWidth
                            spacing: 10

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
                                            text: "Delete"
                                            icon.name: "edit-delete"
                                            implicitWidth: 86
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

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label { text: "Visible tabs"; color: root.ncText }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    Repeater {
                                        model: root.allViewDefs
                                        delegate: CheckBox {
                                            id: visibleTabCheck
                                            text: modelData.label
                                            checked: root.isViewEnabled(modelData.key)
                                            onToggled: root.setViewEnabled(modelData.key, checked)

                                            indicator: Rectangle {
                                                implicitWidth: 18
                                                implicitHeight: 18
                                                radius: 3
                                                color: visibleTabCheck.checked ? root.ncAccent : root.ncPanel
                                                border.width: 1
                                                border.color: root.ncBorder

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: visibleTabCheck.checked ? "✓" : ""
                                                    color: "#ffffff"
                                                    font.pixelSize: 12
                                                    font.bold: true
                                                }
                                            }

                                            contentItem: Text {
                                                text: visibleTabCheck.text
                                                color: root.ncText
                                                leftPadding: visibleTabCheck.indicator.width + visibleTabCheck.spacing
                                                verticalAlignment: Text.AlignVCenter
                                                font: visibleTabCheck.font
                                            }
                                        }
                                    }
                                }
                                Label {
                                    text: "At least one tab must remain enabled."
                                    color: root.ncSubtleText
                                    font.pixelSize: 12
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label { text: "Tab order"; color: root.ncText }
                                Label {
                                    text: "This order controls both tab display and auto-cycle order."
                                    color: root.ncSubtleText
                                    font.pixelSize: 12
                                }

                                Repeater {
                                    model: root.visibleViewKeys
                                    delegate: RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Rectangle {
                                            width: 8
                                            height: 8
                                            radius: 4
                                            color: root.ncAccent
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: root.viewLabelForKey(modelData)
                                            color: root.ncMutedText
                                        }

                                        Button {
                                            text: "Up"
                                            enabled: index > 0
                                            onClicked: root.moveVisibleView(index, index - 1)
                                        }

                                        Button {
                                            text: "Down"
                                            enabled: index < (root.visibleViewKeys.length - 1)
                                            onClicked: root.moveVisibleView(index, index + 1)
                                        }
                                    }
                                }
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

                Item {
                    ScrollView {
                        id: weatherSettingsScroll
                        anchors.fill: parent
                        clip: true

                        ColumnLayout {
                            width: weatherSettingsScroll.availableWidth
                            spacing: 10

                            Text { text: "Weather"; color: root.ncText; font.pixelSize: 22; font.bold: true }

                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Label { text: "Provider"; color: root.ncText }
                                ComboBox {
                                    model: ["Open-Meteo (free, no key)", "OpenWeatherMap (free key required)", "NOAA (US only, free)", "MET Norway (free, no key)"]
                                    currentIndex: weatherManager.provider
                                    onActivated: { weatherManager.provider = currentIndex; weatherManager.saveSettings() }
                                }
                                Item { Layout.fillWidth: true }
                            }

                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Label { text: "Location"; color: root.ncText }
                                TextField {
                                    id: weatherLocationField
                                    Layout.fillWidth: true
                                    text: weatherManager.locationQuery
                                    placeholderText: "City name or latitude,longitude"
                                    onEditingFinished: { weatherManager.locationQuery = text; weatherManager.saveSettings() }
                                }
                                BusyIndicator {
                                    visible: weatherManager.locationDetecting
                                    running: weatherManager.locationDetecting
                                    implicitWidth: 24; implicitHeight: 24
                                }
                                Button {
                                    text: "Detect"
                                    visible: !weatherManager.locationDetecting
                                    enabled: !weatherManager.busy
                                    onClicked: weatherManager.detectLocation()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                visible: weatherManager.provider === 1
                                Label { text: "API key"; color: root.ncText }
                                TextField {
                                    Layout.fillWidth: true
                                    text: weatherManager.apiKey
                                    placeholderText: "OpenWeatherMap API key"
                                    echoMode: TextInput.Password
                                    onEditingFinished: { weatherManager.apiKey = text; weatherManager.saveSettings() }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Label { text: "Weather units"; color: root.ncText }
                                ComboBox {
                                    model: ["Metric (\u00B0C, km/h)", "Imperial (\u00B0F, mph)"]
                                    currentIndex: weatherManager.temperatureUnit
                                    onActivated: { weatherManager.temperatureUnit = currentIndex; weatherManager.saveSettings() }
                                }
                                Item { Layout.fillWidth: true }
                            }

                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Label { text: "Auto-refresh"; color: root.ncText }
                                Switch {
                                    id: weatherAutoRefreshSwitch
                                    checked: weatherManager.autoRefreshEnabled
                                    onToggled: { weatherManager.autoRefreshEnabled = checked; weatherManager.saveSettings() }
                                }
                                Label { text: "Every"; color: root.ncText }
                                SpinBox {
                                    from: 5; to: 180; value: weatherManager.refreshIntervalMinutes; editable: true
                                    enabled: weatherAutoRefreshSwitch.checked
                                    onValueModified: { weatherManager.refreshIntervalMinutes = value; weatherManager.saveSettings() }
                                }
                                Label { text: "min"; color: root.ncText }
                                Item { Layout.fillWidth: true }
                            }

                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Button {
                                    text: weatherManager.busy ? "Fetching\u2026" : "Fetch Now"
                                    enabled: !weatherManager.busy && weatherManager.configured
                                    onClicked: weatherManager.refreshWeather()
                                }
                                Label {
                                    text: weatherManager.statusMessage
                                    color: root.ncSubtleText; font.pixelSize: 14
                                    Layout.fillWidth: true; elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

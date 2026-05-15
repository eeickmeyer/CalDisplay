import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: root
    visible: true
    visibility: windowedMode ? Window.Windowed : Window.FullScreen
    title: windowedMode ? "CalDisplay (Configuration)" : "CalDisplay"
    color: "#111418"
    property bool setupOpen: windowedMode
    property string currentTime: Qt.formatTime(new Date(), "HH:mm:ss")

    // ── View cycling ──────────────────────────────────────────────
    property int  currentView:   0      // 0 = Day, 1 = Week, 2 = Month
    property int  viewCycleSecs: 20
    property real cycleProgress: 0.0
    property var  allEvents:     []
    readonly property var viewNames: ["Day", "Week", "Month"]
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

    // Mon–Sun of the current week
    function weekDays() {
        var d = new Date()
        d.setHours(0, 0, 0, 0)
        var dow = d.getDay()
        d.setDate(d.getDate() - (dow === 0 ? 6 : dow - 1))
        var out = []
        for (var i = 0; i < 7; i++) { out.push(new Date(d)); d.setDate(d.getDate() + 1) }
        return out
    }

    // 42 cells for the month grid (Monday-first)
    function monthCells() {
        var today = new Date()
        var first = new Date(today.getFullYear(), today.getMonth(), 1)
        var dow = first.getDay()
        var gs = new Date(first)
        gs.setDate(first.getDate() - (dow === 0 ? 6 : dow - 1))
        var out = []
        for (var i = 0; i < 42; i++) { out.push(new Date(gs)); gs.setDate(gs.getDate() + 1) }
        return out
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

    onSetupOpenChanged: { if (setupOpen) populateFeedList(); else { viewCycleTimer.restart(); startCycle() } }

    // ── Timers ────────────────────────────────────────────────────
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: root.currentTime = Qt.formatTime(new Date(), "HH:mm:ss")
    }

    Timer {
        id: viewCycleTimer
        interval: root.viewCycleSecs * 1000
        running: !root.setupOpen; repeat: true
        onTriggered: { root.currentView = (root.currentView + 1) % 3; root.startCycle() }
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
            GradientStop { position: 0.0; color: "#1b2430" }
            GradientStop { position: 1.0; color: "#111418" }
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
                text: "Family Calendar"
                color: "#f7fafc"; font.pixelSize: 44; font.bold: true
            }
            Item { Layout.fillWidth: true }
            Text {
                text: Qt.formatDateTime(new Date(), "ddd yyyy-MM-dd")
                color: "#cbd5e0"; font.pixelSize: 26
            }
            Button { text: "Account"; onClicked: root.setupOpen = !root.setupOpen }
        }

        // Clock
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 120
            color: "#0f1720"; radius: 12; border.width: 1; border.color: "#2d3748"
            Text {
                anchors.centerIn: parent
                text: root.currentTime
                color: "#4fd1c5"; font.pixelSize: 80; font.bold: true; font.family: "monospace"
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
                        color: root.currentView === index ? "#2d4a6b" : "#1a2535"
                        border.width: root.currentView === index ? 1 : 0
                        border.color: "#4fd1c5"
                        Text {
                            id: tabLabel; anchors.centerIn: parent; text: modelData
                            color: root.currentView === index ? "#4fd1c5" : "#718096"
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
                Layout.fillWidth: true; height: 3; color: "#1a2535"; radius: 1
                Rectangle {
                    width: parent.width * root.cycleProgress; height: parent.height
                    radius: 1; color: "#4fd1c5"; opacity: 0.7
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
                    Text { text: modelData.name; color: "#cbd5e0"; font.pixelSize: 14 }
                }
            }
        }

        // ── StackLayout: three views ──────────────────────────────
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: root.currentView

            // ── Day view ──────────────────────────────────────────
            Rectangle {
                color: "#171e28"; radius: 16
                border.width: 1; border.color: "#2d3748"; clip: true

                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    Text {
                        text: Qt.formatDate(new Date(), "dddd, MMMM d")
                        color: "#a0aec0"; font.pixelSize: 22; font.bold: true
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 8; clip: true
                        model: {
                            var today = new Date()
                            return root.allEvents.filter(function(ev) {
                                return root.isSameDate(new Date(ev.startMs), today)
                            })
                        }
                        delegate: Rectangle {
                            width: ListView.view.width; height: 78; radius: 10
                            color: "#1f2937"; border.width: 1; border.color: "#2d3748"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 10; spacing: 12
                                Rectangle { width: 9; Layout.fillHeight: true; radius: 4; color: modelData.color }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 2
                                    Text {
                                        text: modelData.title; color: "#f7fafc"
                                        font.pixelSize: 28; font.bold: true
                                        elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                    Text {
                                        text: modelData.calendar + "  |  " +
                                              Qt.formatTime(new Date(modelData.startMs), "HH:mm") + " – " +
                                              Qt.formatTime(new Date(modelData.endMs), "HH:mm")
                                        color: "#cbd5e0"; font.pixelSize: 20
                                        elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: parent.count === 0
                            text: "No events today"; color: "#4a5568"; font.pixelSize: 28
                        }
                    }
                }
            }

            // ── Week view ─────────────────────────────────────────
            Rectangle {
                color: "#171e28"; radius: 16
                border.width: 1; border.color: "#2d3748"; clip: true

                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 6
                    Text {
                        property var days: root.weekDays()
                        text: "Week of " + Qt.formatDate(days[0], "MMM d") +
                              " – " + Qt.formatDate(days[6], "MMM d")
                        color: "#a0aec0"; font.pixelSize: 18; font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6
                        Repeater {
                            model: root.weekDays()
                            delegate: Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true; radius: 10
                                property var colDate: modelData
                                property bool isToday: root.isSameDate(colDate, new Date())
                                color: isToday ? "#1e3a5f" : "#1a2535"
                                border.width: isToday ? 1 : 0; border.color: "#4fd1c5"; clip: true

                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 6; spacing: 4
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 0
                                        Text {
                                            Layout.alignment: Qt.AlignHCenter
                                            text: Qt.formatDate(colDate, "ddd")
                                            color: isToday ? "#4fd1c5" : "#718096"
                                            font.pixelSize: 13; font.bold: true
                                        }
                                        Text {
                                            Layout.alignment: Qt.AlignHCenter
                                            text: colDate.getDate()
                                            color: isToday ? "#4fd1c5" : "#f7fafc"
                                            font.pixelSize: 22; font.bold: true
                                        }
                                    }
                                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2d3748" }
                                    Repeater {
                                        model: root.allEvents.filter(function(ev) {
                                            return root.isSameDate(new Date(ev.startMs), colDate)
                                        })
                                        delegate: Rectangle {
                                            Layout.fillWidth: true; height: 46; radius: 6
                                            color: "#1f2937"; border.width: 1; border.color: modelData.color; clip: true
                                            ColumnLayout {
                                                anchors.fill: parent; anchors.margins: 5; spacing: 1
                                                Text {
                                                    text: modelData.title; color: "#f7fafc"
                                                    font.pixelSize: 12; font.bold: true
                                                    elide: Text.ElideRight; Layout.fillWidth: true
                                                }
                                                Text {
                                                    text: Qt.formatTime(new Date(modelData.startMs), "HH:mm")
                                                    color: "#a0aec0"; font.pixelSize: 11
                                                }
                                            }
                                        }
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                            }
                        }
                    }
                }
            }

            // ── Month view ────────────────────────────────────────
            Rectangle {
                color: "#171e28"; radius: 16
                border.width: 1; border.color: "#2d3748"; clip: true

                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: Qt.formatDate(new Date(), "MMMM yyyy")
                        color: "#f7fafc"; font.pixelSize: 24; font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 4
                        Repeater {
                            model: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
                            delegate: Text {
                                Layout.fillWidth: true; text: modelData
                                color: "#718096"; font.pixelSize: 13; font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        columns: 7; rowSpacing: 4; columnSpacing: 4
                        Repeater {
                            model: root.monthCells()
                            delegate: Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true; radius: 6; clip: true
                                property var cellDate: modelData
                                property bool inMonth: cellDate.getMonth() === new Date().getMonth()
                                property bool isToday: root.isSameDate(cellDate, new Date())
                                property var dayEvents: root.allEvents.filter(function(ev) {
                                    return root.isSameDate(new Date(ev.startMs), cellDate)
                                })
                                color: isToday ? "#2d4a6b" : (inMonth ? "#1a2535" : "#131820")
                                border.width: isToday ? 1 : 0; border.color: "#4fd1c5"
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 3; spacing: 2
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: cellDate.getDate()
                                        color: isToday ? "#4fd1c5" : (inMonth ? "#f7fafc" : "#4a5568")
                                        font.pixelSize: 14; font.bold: isToday
                                    }
                                    Repeater {
                                        model: dayEvents.slice(0, 3)
                                        delegate: Rectangle {
                                            Layout.fillWidth: true; height: 16; radius: 2; clip: true
                                            color: "#1f2937"
                                            Row {
                                                anchors.fill: parent; spacing: 0
                                                Rectangle { width: 4; height: parent.height; color: modelData.color }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    leftPadding: 3
                                                    text: modelData.title; font.pixelSize: 11; color: "#e2e8f0"
                                                    elide: Text.ElideRight; width: parent.width - 5
                                                }
                                            }
                                        }
                                    }
                                    Text {
                                        visible: dayEvents.length > 3
                                        text: "+" + (dayEvents.length - 3) + " more"
                                        color: "#718096"; font.pixelSize: 10
                                        Layout.alignment: Qt.AlignLeft
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Footer
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Last refresh: " + eventModel.lastUpdated; color: "#a0aec0"; font.pixelSize: 18 }
            Item { Layout.fillWidth: true }
            Button {
                text: feedManager.busy ? "Refreshing..." : "Refresh Now"
                enabled: !feedManager.busy; onClicked: feedManager.refreshFeeds()
            }
        }
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
        z: 60; color: "#0f1720"; border.color: "#2d3748"; border.width: 1
        transform: Translate {
            x: root.setupOpen ? 0 : setupPanel.width
            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.InOutQuad } }
        }

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 18; spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Read-Only Calendar Sources"; color: "#f7fafc"; font.pixelSize: 28; font.bold: true }
                Item { Layout.fillWidth: true }
                Button { text: "Close"; onClicked: root.setupOpen = false }
            }

            Label { text: "Primary mode: shared ICS/webcal links (read-only)."; color: "#cbd5e0"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: "Calendars (Name | URL)"; color: "#e2e8f0" }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(feedListView.contentHeight + 10, 220)
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
                            text: model.url; placeholderText: "webcal:// or https://..."
                            Layout.fillWidth: true
                            onTextEdited: feedListModel.setProperty(index, "url", text)
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
                Text { text: "Last sync: " + feedManager.lastSync; color: "#a0aec0"; font.pixelSize: 15 }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Label { text: "Automatic refresh"; color: "#e2e8f0" }
                Switch {
                    id: autoRefreshSwitch; checked: feedManager.autoRefreshEnabled; enabled: !feedManager.busy
                    onToggled: { feedManager.autoRefreshEnabled = checked; feedManager.saveSettings() }
                }
                Label { text: "Every"; color: "#e2e8f0" }
                SpinBox {
                    id: refreshIntervalSpin; from: 1; to: 180; value: feedManager.refreshIntervalMinutes; editable: true
                    enabled: !feedManager.busy && autoRefreshSwitch.checked
                    onValueModified: { feedManager.refreshIntervalMinutes = value; feedManager.saveSettings() }
                }
                Label { text: "min"; color: "#e2e8f0" }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Label { text: "View cycle"; color: "#e2e8f0" }
                SpinBox {
                    from: 5; to: 300; value: root.viewCycleSecs; editable: true
                    onValueModified: { root.viewCycleSecs = value; viewCycleTimer.restart(); root.startCycle() }
                }
                Label { text: "sec"; color: "#e2e8f0" }
                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true; color: "#111827"; radius: 8
                border.width: 1; border.color: "#2d3748"
                implicitHeight: roStatusText.implicitHeight + 16
                Text {
                    id: roStatusText; anchors.fill: parent; anchors.margins: 8
                    text: feedManager.statusMessage; color: "#a0aec0"; wrapMode: Text.WordWrap; font.pixelSize: 16
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#2d3748" }

            Label { text: "Optional advanced: Nextcloud account discovery"; color: "#9ca3af"; font.bold: true }
            Label { text: "Server URL"; color: "#e2e8f0" }
            TextField {
                id: serverField; text: accountManager.serverUrl
                placeholderText: "https://cloud.example.com/remote.php/dav/"
                Layout.fillWidth: true; onEditingFinished: accountManager.serverUrl = text
            }
            Label { text: "Username"; color: "#e2e8f0" }
            TextField {
                id: userField; text: accountManager.username; placeholderText: "nextcloud-user"
                Layout.fillWidth: true; onEditingFinished: accountManager.username = text
            }
            Label { text: "App password"; color: "#e2e8f0" }
            TextField {
                id: passField; text: accountManager.password; echoMode: TextInput.Password
                placeholderText: "Nextcloud app password"
                Layout.fillWidth: true; onEditingFinished: accountManager.password = text
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Button {
                    text: "Save"; enabled: !accountManager.busy
                    onClicked: {
                        accountManager.serverUrl = serverField.text; accountManager.username = userField.text
                        accountManager.password = passField.text; accountManager.saveSettings()
                    }
                }
                Button {
                    text: accountManager.busy ? "Connecting..." : "Test & Discover"; enabled: !accountManager.busy
                    onClicked: {
                        accountManager.serverUrl = serverField.text; accountManager.username = userField.text
                        accountManager.password = passField.text; accountManager.saveSettings()
                        accountManager.discoverCalendars()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true; color: "#111827"; radius: 8
                border.width: 1; border.color: "#2d3748"
                implicitHeight: statusText.implicitHeight + 16
                Text {
                    id: statusText; anchors.fill: parent; anchors.margins: 8
                    text: accountManager.statusMessage; color: "#a0aec0"; wrapMode: Text.WordWrap; font.pixelSize: 16
                }
            }

            Label { text: "Discovered calendars"; color: "#e2e8f0"; font.bold: true }

            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: "#111827"; radius: 10; border.width: 1; border.color: "#2d3748"
                ListView {
                    id: calList; anchors.fill: parent; anchors.margins: 8; clip: true
                    model: accountManager.availableCalendars
                    delegate: Rectangle {
                        width: calList.width; height: 42; radius: 6
                        color: index % 2 === 0 ? "#1f2937" : "#18212f"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left; anchors.leftMargin: 10
                            text: modelData; color: "#f7fafc"; font.pixelSize: 17
                            elide: Text.ElideRight; width: parent.width - 20
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: accountManager.availableCalendars.length === 0 && !accountManager.busy
                        text: "No calendars yet"; color: "#6b7280"; font.pixelSize: 16
                    }
                }
            }
        }
    }
}

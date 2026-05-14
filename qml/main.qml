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

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.currentTime = Qt.formatTime(new Date(), "HH:mm:ss")
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1b2430" }
            GradientStop { position: 1.0; color: "#111418" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Text {
                text: "Family Calendar"
                color: "#f7fafc"
                font.pixelSize: 44
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: Qt.formatDateTime(new Date(), "ddd yyyy-MM-dd")
                color: "#cbd5e0"
                font.pixelSize: 26
            }

            Button {
                text: "Account"
                onClicked: root.setupOpen = !root.setupOpen
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            color: "#0f1720"
            radius: 12
            border.width: 1
            border.color: "#2d3748"

            Text {
                anchors.centerIn: parent
                text: root.currentTime
                color: "#4fd1c5"
                font.pixelSize: 80
                font.bold: true
                font.family: "monospace"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#171e28"
            radius: 16
            border.width: 1
            border.color: "#2d3748"

            ListView {
                id: eventsList
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8
                clip: true
                model: eventModel
                reuseItems: true
                cacheBuffer: 300

                delegate: Rectangle {
                    width: eventsList.width
                    height: 88
                    radius: 10
                    color: isSoon ? "#2d3748" : "#1f2937"
                    border.width: 1
                    border.color: isToday ? "#4fd1c5" : "#2d3748"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 9
                            Layout.fillHeight: true
                            radius: 4
                            color: model.color
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: model.title
                                color: "#f7fafc"
                                font.pixelSize: 28
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: model.calendar + "  |  " + model.startDisplay + " - " + model.endDisplay
                                color: "#cbd5e0"
                                font.pixelSize: 20
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            text: isSoon ? "Soon" : (isToday ? "Today" : "Upcoming")
                            color: isSoon ? "#f6ad55" : "#a0aec0"
                            font.pixelSize: 18
                            font.bold: isSoon
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "Last refresh: " + eventModel.lastUpdated
                color: "#a0aec0"
                font.pixelSize: 18
            }

            Item { Layout.fillWidth: true }

            Button {
                text: feedManager.busy ? "Refreshing..." : "Refresh Now"
                enabled: !feedManager.busy
                onClicked: feedManager.refreshFeeds()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.setupOpen ? 0.45 : 0.0
        visible: root.setupOpen
        z: 50

        MouseArea {
            anchors.fill: parent
            onClicked: root.setupOpen = false
        }
    }

    Rectangle {
        id: setupPanel
        width: Math.min(540, root.width * 0.85)
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        z: 60

        color: "#0f1720"
        border.color: "#2d3748"
        border.width: 1

        transform: Translate {
            x: root.setupOpen ? 0 : setupPanel.width
            Behavior on x {
                NumberAnimation { duration: 180; easing.type: Easing.InOutQuad }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "Read-Only Calendar Sources"
                    color: "#f7fafc"
                    font.pixelSize: 28
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Close"
                    onClicked: root.setupOpen = false
                }
            }

            Label {
                text: "Primary mode: shared ICS/webcal links (read-only)."
                color: "#cbd5e0"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label {
                text: "One URL per line"
                color: "#e2e8f0"
            }

            TextArea {
                id: feedUrlsArea
                text: feedManager.feedUrls
                placeholderText: "webcal://cloud.example.com/remote.php/dav/public-calendars/xxxx?export\nhttps://example.com/family.ics"
                wrapMode: TextArea.WrapAnywhere
                Layout.fillWidth: true
                Layout.preferredHeight: 120
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: "Save Links"
                    enabled: !feedManager.busy
                    onClicked: {
                        feedManager.feedUrls = feedUrlsArea.text
                        feedManager.saveSettings()
                    }
                }

                Button {
                    text: feedManager.busy ? "Refreshing..." : "Refresh Feeds"
                    enabled: !feedManager.busy
                    onClicked: {
                        feedManager.feedUrls = feedUrlsArea.text
                        feedManager.saveSettings()
                        feedManager.refreshFeeds()
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Last sync: " + feedManager.lastSync
                    color: "#a0aec0"
                    font.pixelSize: 15
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    text: "Automatic refresh"
                    color: "#e2e8f0"
                }

                Switch {
                    id: autoRefreshSwitch
                    checked: feedManager.autoRefreshEnabled
                    enabled: !feedManager.busy
                    onToggled: {
                        feedManager.autoRefreshEnabled = checked
                        feedManager.saveSettings()
                    }
                }

                Label {
                    text: "Every"
                    color: "#e2e8f0"
                }

                SpinBox {
                    id: refreshIntervalSpin
                    from: 1
                    to: 180
                    value: feedManager.refreshIntervalMinutes
                    editable: true
                    enabled: !feedManager.busy && autoRefreshSwitch.checked
                    onValueModified: {
                        feedManager.refreshIntervalMinutes = value
                        feedManager.saveSettings()
                    }
                }

                Label {
                    text: "min"
                    color: "#e2e8f0"
                }

                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true
                color: "#111827"
                radius: 8
                border.width: 1
                border.color: "#2d3748"
                implicitHeight: roStatusText.implicitHeight + 16

                Text {
                    id: roStatusText
                    anchors.fill: parent
                    anchors.margins: 8
                    text: feedManager.statusMessage
                    color: "#a0aec0"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 16
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#2d3748"
            }

            Label {
                text: "Optional advanced: Nextcloud account discovery"
                color: "#9ca3af"
                font.bold: true
            }

            Label {
                text: "Server URL"
                color: "#e2e8f0"
            }

            TextField {
                id: serverField
                text: accountManager.serverUrl
                placeholderText: "https://cloud.example.com/remote.php/dav/"
                Layout.fillWidth: true
                onEditingFinished: accountManager.serverUrl = text
            }

            Label {
                text: "Username"
                color: "#e2e8f0"
            }

            TextField {
                id: userField
                text: accountManager.username
                placeholderText: "nextcloud-user"
                Layout.fillWidth: true
                onEditingFinished: accountManager.username = text
            }

            Label {
                text: "App password"
                color: "#e2e8f0"
            }

            TextField {
                id: passField
                text: accountManager.password
                echoMode: TextInput.Password
                placeholderText: "Nextcloud app password"
                Layout.fillWidth: true
                onEditingFinished: accountManager.password = text
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: "Save"
                    enabled: !accountManager.busy
                    onClicked: {
                        accountManager.serverUrl = serverField.text
                        accountManager.username = userField.text
                        accountManager.password = passField.text
                        accountManager.saveSettings()
                    }
                }

                Button {
                    text: accountManager.busy ? "Connecting..." : "Test & Discover"
                    enabled: !accountManager.busy
                    onClicked: {
                        accountManager.serverUrl = serverField.text
                        accountManager.username = userField.text
                        accountManager.password = passField.text
                        accountManager.saveSettings()
                        accountManager.discoverCalendars()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                color: "#111827"
                radius: 8
                border.width: 1
                border.color: "#2d3748"
                implicitHeight: statusText.implicitHeight + 16

                Text {
                    id: statusText
                    anchors.fill: parent
                    anchors.margins: 8
                    text: accountManager.statusMessage
                    color: "#a0aec0"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 16
                }
            }

            Label {
                text: "Discovered calendars"
                color: "#e2e8f0"
                font.bold: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#111827"
                radius: 10
                border.width: 1
                border.color: "#2d3748"

                ListView {
                    id: calList
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    model: accountManager.availableCalendars

                    delegate: Rectangle {
                        width: calList.width
                        height: 42
                        color: index % 2 === 0 ? "#1f2937" : "#18212f"
                        radius: 6

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            text: modelData
                            color: "#f7fafc"
                            font.pixelSize: 17
                            elide: Text.ElideRight
                            width: parent.width - 20
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: accountManager.availableCalendars.length === 0 && !accountManager.busy
                        text: "No calendars yet"
                        color: "#6b7280"
                        font.pixelSize: 16
                    }
                }
            }
        }
    }
}

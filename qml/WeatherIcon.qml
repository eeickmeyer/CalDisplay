// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

import QtQuick 2.15

// Image that recovers from transient load failures (e.g. a cache file being
// rewritten while QML tries to read it) instead of staying blank.
Image {
    id: icon

    property string iconSource: ""
    property int retries: 0

    source: iconSource
    fillMode: Image.PreserveAspectFit
    asynchronous: true

    onIconSourceChanged: retries = 0

    onStatusChanged: {
        if (status === Image.Error && iconSource !== "" && retries < 5)
            retryTimer.restart()
    }

    Timer {
        id: retryTimer
        interval: 1000 * Math.pow(2, icon.retries)
        onTriggered: {
            icon.retries++
            // Force a fresh load: bypass the QML pixmap cache entry for the failed URL.
            icon.source = ""
            icon.source = Qt.binding(function() { return icon.iconSource })
        }
    }
}

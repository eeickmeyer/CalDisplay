// SPDX-License-Identifier: GPL-3.0-or-later
// CalDisplay - A calendar application for displaying events from shared ICS feeds
// Copyright (C) 2026 Erich Eickmeyer
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include <QApplication>
#include <QDateTime>
#include <QIcon>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>

#include "eventmodel.h"
#include "feedmanager.h"
#include "weathermanager.h"

namespace {

void schedulePostResumeRefresh(WeatherManager* weatherManager,
                               FeedManager* feedManager,
                               qint64* lastRefreshMs,
                               int settleDelayMs = 1500) {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (*lastRefreshMs > 0 && (nowMs - *lastRefreshMs) < 5000)
        return;
    *lastRefreshMs = nowMs;

    QTimer::singleShot(settleDelayMs, weatherManager, [weatherManager, feedManager]() {
        weatherManager->refreshWeatherIfDue();
        feedManager->refreshFeedsIfDue();
    });
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    bool windowed = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--windowed" || arg == "-w") {
            windowed = true;
            break;
        }
    }

    QStringList iconSearchPaths;
    iconSearchPaths << QStringLiteral("/usr/share/icons");
    const QByteArray iconSnap = qgetenv("SNAP");
    if (!iconSnap.isEmpty())
        iconSearchPaths.prepend(QString::fromLocal8Bit(iconSnap) + QStringLiteral("/usr/share/icons"));
    QIcon::setThemeSearchPaths(iconSearchPaths);
    QIcon::setThemeName(QStringLiteral("Papirus"));
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));

    EventModel model;
    FeedManager feedManager(&model);
    WeatherManager weatherManager;
    qint64 lastResumeRefreshMs = 0;

    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     &app, [&](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            schedulePostResumeRefresh(&weatherManager, &feedManager, &lastResumeRefreshMs);
        }
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("eventModel", &model);
    engine.rootContext()->setContextProperty("feedManager", &feedManager);
    engine.rootContext()->setContextProperty("weatherManager", &weatherManager);
    engine.rootContext()->setContextProperty("windowedMode", windowed);

    // Resolve QML path: check snap location, installed location, then source location
    QString qmlPath;
    const char* snap = qgetenv("SNAP");
    
    if (snap && strlen(snap) > 0) {
        // Running as a snap
        qmlPath = QString::fromLocal8Bit(snap) + "/share/CalDisplay/qml";
    } else {
        // Check installed location
        QString appDir = QCoreApplication::applicationDirPath();
        QFileInfo installedPath(appDir + "/../share/CalDisplay/qml/main.qml");
        if (installedPath.exists()) {
            qmlPath = appDir + "/../share/CalDisplay/qml";
        } else {
            // Fall back to source location (development)
            qmlPath = QStringLiteral(APP_QML_PATH);
        }
    }
    
    const QString rootQml = qmlPath + "/main.qml";
    engine.load(QUrl::fromLocalFile(rootQml));

    if (engine.rootObjects().isEmpty()) {
        qWarning() << "Failed to load QML from:" << rootQml;
        return -1;
    }

    if (windowed) {
        QQuickWindow* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window) {
            window->setWidth(1024);
            window->setHeight(768);
            window->setVisibility(QWindow::Windowed);
        }
    }

    return app.exec();
}

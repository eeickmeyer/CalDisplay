// SPDX-License-Identifier: GPL-3.0-or-later
// CalDisplay - A calendar application for displaying events from shared ICS feeds
// Copyright (C) 2026 Erich Eickmeyer
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <QObject>
#include <QDateTime>
#include <QTimer>

class QNetworkAccessManager;
class EventModel;

class FeedManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString feedUrls READ feedUrls WRITE setFeedUrls NOTIFY feedUrlsChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastSync READ lastSync NOTIFY lastSyncChanged)
    Q_PROPERTY(bool autoRefreshEnabled READ autoRefreshEnabled WRITE setAutoRefreshEnabled NOTIFY autoRefreshEnabledChanged)
    Q_PROPERTY(int refreshIntervalMinutes READ refreshIntervalMinutes WRITE setRefreshIntervalMinutes NOTIFY refreshIntervalMinutesChanged)
    Q_PROPERTY(int timeFormatPreference READ timeFormatPreference WRITE setTimeFormatPreference NOTIFY timeFormatPreferenceChanged)
    Q_PROPERTY(bool sundayFirst READ sundayFirst WRITE setSundayFirst NOTIFY sundayFirstChanged)
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY displayNameChanged)

public:
    explicit FeedManager(EventModel* eventModel, QObject* parent = nullptr);

    QString feedUrls() const;
    QString statusMessage() const;
    bool busy() const;
    QString lastSync() const;
    bool autoRefreshEnabled() const;
    int refreshIntervalMinutes() const;
    int timeFormatPreference() const;
    bool sundayFirst() const;
    QString displayName() const;

    void setFeedUrls(const QString& value);
    void setAutoRefreshEnabled(bool value);
    void setRefreshIntervalMinutes(int value);
    void setTimeFormatPreference(int value);
    void setSundayFirst(bool value);
    void setDisplayName(const QString& value);

    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void refreshFeeds();
    Q_INVOKABLE QString pickLocalIcsFile() const;

signals:
    void feedUrlsChanged();
    void statusMessageChanged();
    void busyChanged();
    void lastSyncChanged();
    void autoRefreshEnabledChanged();
    void refreshIntervalMinutesChanged();
    void timeFormatPreferenceChanged();
    void sundayFirstChanged();
    void displayNameChanged();

private:
    QString m_feedUrls;
    QString m_statusMessage;
    bool m_busy = false;
    QDateTime m_lastSync;
    bool m_autoRefreshEnabled = true;
    int m_refreshIntervalMinutes = 5;
    int m_timeFormatPreference = 0;
    bool m_sundayFirst = false;
    QString m_displayName = QStringLiteral("Family Calendar");
    QTimer m_autoRefreshTimer;

    EventModel* m_eventModel;
    QNetworkAccessManager* m_nam;

    void loadSettings();
    void setStatusMessage(const QString& value);
    void setBusy(bool value);
    void updateAutoRefreshTimer();
    void refreshFeedsInternal(bool interactive);

    QStringList normalizedEntryList() const;
    QStringList normalizedUrlList() const;
    QString colorForSource(const QString& source) const;
    QString sourceNameFromUrl(const QString& url) const;
};

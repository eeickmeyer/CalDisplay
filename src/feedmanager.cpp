// SPDX-License-Identifier: GPL-3.0-or-later
// CalDisplay - A calendar application for displaying events from shared ICS feeds
// Copyright (C) 2026 Erich Eickmeyer
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "feedmanager.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include "eventmodel.h"

namespace {
constexpr const char* kOrg = "CalDisplay";
constexpr const char* kApp = "CalDisplay";

QDateTime parseIcsDateTime(const QString& rawValue) {
    QString value = rawValue.trimmed();

    if (value.size() == 8) {
        const QDate d = QDate::fromString(value, "yyyyMMdd");
        if (d.isValid()) {
            return QDateTime(d, QTime(0, 0));
        }
        return {};
    }

    if (value.endsWith('Z')) {
        const QString utcValue = value.left(value.size() - 1);
        QDateTime dt = QDateTime::fromString(utcValue, "yyyyMMdd'T'HHmmss");
        if (!dt.isValid()) {
            dt = QDateTime::fromString(utcValue, "yyyyMMdd'T'HHmm");
        }
        if (!dt.isValid()) {
            return {};
        }
        dt.setTimeSpec(Qt::UTC);
        return dt.toLocalTime();
    }

    QDateTime local = QDateTime::fromString(value, "yyyyMMdd'T'HHmmss");
    if (!local.isValid()) {
        local = QDateTime::fromString(value, "yyyyMMdd'T'HHmm");
    }
    return local;
}

QString unfoldIcs(const QByteArray& data) {
    QString text = QString::fromUtf8(data);
    text.replace("\r\n", "\n");
    text.replace("\r", "\n");

    QStringList lines = text.split('\n');
    QStringList unfolded;
    unfolded.reserve(lines.size());

    for (const QString& line : lines) {
        if (!unfolded.isEmpty() && (line.startsWith(' ') || line.startsWith('\t'))) {
            unfolded.last().append(line.mid(1));
        } else {
            unfolded.append(line);
        }
    }

    return unfolded.join('\n');
}

void appendIcsEvents(const QByteArray& body,
                     const QString& source,
                     const QString& color,
                     QList<CalendarEvent>* mergedEvents) {
    const QString unfolded = unfoldIcs(body);
    const QStringList lines = unfolded.split('\n', Qt::KeepEmptyParts);

    bool inEvent = false;
    QString summary;
    QString dtStartRaw;
    QString dtEndRaw;
    QString status = QStringLiteral("CONFIRMED");

    for (const QString& originalLine : lines) {
        const QString line = originalLine.trimmed();
        if (line == "BEGIN:VEVENT") {
            inEvent = true;
            summary.clear();
            dtStartRaw.clear();
            dtEndRaw.clear();
            status = QStringLiteral("CONFIRMED");
            continue;
        }

        if (line == "END:VEVENT") {
            if (inEvent) {
                CalendarEvent ev;
                ev.title = summary.isEmpty() ? QStringLiteral("(No title)") : summary;
                ev.calendar = source;
                ev.color = color;
                ev.start = parseIcsDateTime(dtStartRaw);
                ev.end = parseIcsDateTime(dtEndRaw);
                ev.status = status;
                if (!ev.start.isValid()) {
                    inEvent = false;
                    continue;
                }
                if (!ev.end.isValid() || ev.end < ev.start) {
                    ev.end = ev.start.addSecs(60 * 60);
                }
                mergedEvents->append(ev);
            }
            inEvent = false;
            continue;
        }

        if (!inEvent) {
            continue;
        }

        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }

        QString key = line.left(colon).trimmed().toUpper();
        const QString value = line.mid(colon + 1).trimmed();

        const int semi = key.indexOf(';');
        if (semi > 0) {
            key = key.left(semi);
        }

        if (key == "SUMMARY") {
            summary = value;
        } else if (key == "DTSTART") {
            dtStartRaw = value;
        } else if (key == "DTEND") {
            dtEndRaw = value;
        } else if (key == "STATUS") {
            status = value.toUpper();
        }
    }
}

QString localFeedPathFromInput(QString input) {
    input = input.trimmed();
    if (input.isEmpty()) {
        return {};
    }

    if (input.startsWith("file://", Qt::CaseInsensitive)) {
        return QUrl(input).toLocalFile();
    }

    if (input.startsWith("~/")) {
        input.replace(0, 1, QDir::homePath());
    }

    const QUrl parsed = QUrl::fromUserInput(input);
    if (parsed.isLocalFile()) {
        return parsed.toLocalFile();
    }

    if (QFileInfo::exists(input)) {
        return QFileInfo(input).absoluteFilePath();
    }

    return {};
}
}

FeedManager::FeedManager(EventModel* eventModel, QObject* parent)
    : QObject(parent)
    , m_eventModel(eventModel)
    , m_nam(new QNetworkAccessManager(this)) {
    QCoreApplication::setOrganizationName(QString::fromUtf8(kOrg));
    QCoreApplication::setApplicationName(QString::fromUtf8(kApp));
    loadSettings();
}

QString FeedManager::feedUrls() const { return m_feedUrls; }
QString FeedManager::statusMessage() const { return m_statusMessage; }
bool FeedManager::busy() const { return m_busy; }
bool FeedManager::autoRefreshEnabled() const { return m_autoRefreshEnabled; }
int FeedManager::refreshIntervalMinutes() const { return m_refreshIntervalMinutes; }
int FeedManager::timeFormatPreference() const { return m_timeFormatPreference; }
bool FeedManager::sundayFirst() const { return m_sundayFirst; }
QString FeedManager::displayName() const { return m_displayName; }

QString FeedManager::lastSync() const {
    if (!m_lastSync.isValid()) {
        return "Never";
    }
    return m_lastSync.toString("ddd yyyy-MM-dd HH:mm:ss");
}

void FeedManager::setFeedUrls(const QString& value) {
    if (m_feedUrls == value) {
        return;
    }
    m_feedUrls = value;
    emit feedUrlsChanged();
}

void FeedManager::setAutoRefreshEnabled(bool value) {
    if (m_autoRefreshEnabled == value) {
        return;
    }
    m_autoRefreshEnabled = value;
    emit autoRefreshEnabledChanged();
    updateAutoRefreshTimer();
}

void FeedManager::setRefreshIntervalMinutes(int value) {
    const int clamped = std::clamp(value, 1, 180);
    if (m_refreshIntervalMinutes == clamped) {
        return;
    }
    m_refreshIntervalMinutes = clamped;
    emit refreshIntervalMinutesChanged();
    updateAutoRefreshTimer();
}

void FeedManager::setTimeFormatPreference(int value) {
    const int clamped = std::clamp(value, 0, 2);
    if (m_timeFormatPreference == clamped) {
        return;
    }
    m_timeFormatPreference = clamped;
    emit timeFormatPreferenceChanged();
}

void FeedManager::setSundayFirst(bool value) {
    if (m_sundayFirst == value) {
        return;
    }
    m_sundayFirst = value;
    emit sundayFirstChanged();
}

void FeedManager::setDisplayName(const QString& value) {
    const QString normalized = value.trimmed().isEmpty() ? QStringLiteral("Family Calendar") : value.trimmed();
    if (m_displayName == normalized) {
        return;
    }
    m_displayName = normalized;
    emit displayNameChanged();
}

void FeedManager::saveSettings() {
    QSettings settings;
    settings.setValue("readonly/feedUrls", m_feedUrls);
    settings.setValue("readonly/autoRefreshEnabled", m_autoRefreshEnabled);
    settings.setValue("readonly/refreshIntervalMinutes", m_refreshIntervalMinutes);
    settings.setValue("readonly/timeFormatPreference", m_timeFormatPreference);
    settings.setValue("readonly/sundayFirst", m_sundayFirst);
    settings.setValue("readonly/displayName", m_displayName);
    settings.sync();
    setStatusMessage("Settings saved.");
}

void FeedManager::refreshFeeds() {
    refreshFeedsInternal(true);
}

QString FeedManager::pickLocalIcsFile() const {
    return QFileDialog::getOpenFileName(nullptr,
                                        tr("Choose an ICS file"),
                                        QDir::homePath(),
                                        tr("Calendar files (*.ics);;All files (*)"));
}

void FeedManager::refreshFeedsInternal(bool interactive) {
    if (busy()) {
        return;
    }

    const QStringList entries = normalizedEntryList();
    if (entries.isEmpty()) {
        if (interactive) {
            setStatusMessage("Add one or more ICS/webcal links first.");
        }
        return;
    }

    setBusy(true);
    if (interactive) {
        setStatusMessage("Refreshing calendar feeds...");
    }

    auto pending = QSharedPointer<int>::create(entries.size());
    auto mergedEvents = QSharedPointer<QList<CalendarEvent>>::create();
    auto unauthorizedFeeds = QSharedPointer<int>::create(0);
    auto nonCalendarFeeds = QSharedPointer<int>::create(0);
    auto networkErrorFeeds = QSharedPointer<int>::create(0);

    auto finalize = [this,
                     pending,
                     mergedEvents,
                     unauthorizedFeeds,
                     nonCalendarFeeds,
                     networkErrorFeeds,
                     entryCount = (int)entries.size()]() {
        if (*pending != 0) {
            return;
        }

        QList<CalendarEvent> filtered;
        filtered.reserve(mergedEvents->size());

        const QDateTime now = QDateTime::currentDateTime();
        const QDate today = now.date();
        const QDateTime oldest = QDateTime(QDate(today.year(), today.month(), 1).addMonths(-1), QTime(0, 0));
        const QDateTime newest = now.addDays(60);

        for (const CalendarEvent& ev : *mergedEvents) {
            if (ev.end >= oldest && ev.start <= newest) {
                filtered.append(ev);
            }
        }

        std::sort(filtered.begin(), filtered.end(), [](const CalendarEvent& a, const CalendarEvent& b) {
            return a.start < b.start;
        });

        if (m_eventModel) {
            m_eventModel->setExternalEvents(filtered);
        }

        m_lastSync = QDateTime::currentDateTime();
        emit lastSyncChanged();

        QString message = QString("Loaded %1 event(s) from %2 feed(s).")
                              .arg(filtered.size())
                              .arg(entryCount);
        if (*unauthorizedFeeds > 0) {
            message += QString(" %1 feed(s) require authentication.").arg(*unauthorizedFeeds);
        }
        if (*nonCalendarFeeds > 0) {
            message += QString(" %1 feed(s) were not calendar ICS.").arg(*nonCalendarFeeds);
        }
        if (*networkErrorFeeds > 0) {
            message += QString(" %1 feed(s) had network errors.").arg(*networkErrorFeeds);
        }

        setStatusMessage(message);
        setBusy(false);
    };

    for (const QString& rawEntry : entries) {
        const int entryPipe = rawEntry.indexOf('|');
        const QString customName = entryPipe > 0 ? rawEntry.left(entryPipe).trimmed() : QString();
        QString urlText = entryPipe > 0 ? rawEntry.mid(entryPipe + 1).trimmed() : rawEntry.trimmed();
        if (urlText.startsWith("webcal://", Qt::CaseInsensitive)) {
            urlText.replace(0, 9, "https://");
        }
        // Convert Nextcloud public calendar web UI link to ICS export URL
        // e.g. https://host/index.php/apps/calendar/p/<TOKEN>
        //   -> https://host/remote.php/dav/public-calendars/<TOKEN>?export
        static const QRegularExpression ncShareRe(
            R"(^(https?://[^/]+)/index\.php/apps/calendar/p/([^/?#]+))",
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch ncMatch = ncShareRe.match(urlText);
        if (ncMatch.hasMatch()) {
            urlText = ncMatch.captured(1)
                      + "/remote.php/dav/public-calendars/"
                      + ncMatch.captured(2)
                      + "?export";
        }

        const QUrl url(urlText);
        if (!url.isValid()) {
            (*pending)--;
            finalize();
            continue;
        }

        const QString source = customName.isEmpty() ? sourceNameFromUrl(urlText) : customName;
        const QString color = colorForSource(source);

        const QString localPath = localFeedPathFromInput(urlText);
        if (!localPath.isEmpty()) {
            QFile file(localPath);
            if (!file.open(QIODevice::ReadOnly)) {
                (*networkErrorFeeds)++;
                (*pending)--;
                finalize();
                continue;
            }

            const QByteArray body = file.readAll();
            const QString bodyPrefix = QString::fromUtf8(body.left(4096));
            if (!bodyPrefix.contains("BEGIN:VCALENDAR", Qt::CaseInsensitive)) {
                (*nonCalendarFeeds)++;
            } else {
                appendIcsEvents(body, source, color, mergedEvents.data());
            }

            (*pending)--;
            finalize();
            continue;
        }

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "CalDisplay/0.1");

        QNetworkReply* reply = m_nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this,
                                                        reply,
                                                        pending,
                                                        mergedEvents,
                                                        source,
                                                        color,
                                                        unauthorizedFeeds,
                                                        nonCalendarFeeds,
                                                        networkErrorFeeds,
                                                        finalize]() {
            const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
            const QString bodyPrefix = QString::fromUtf8(body.left(4096));

            if (statusCode == 401 || statusCode == 403) {
                (*unauthorizedFeeds)++;
            } else if (reply->error() != QNetworkReply::NoError) {
                (*networkErrorFeeds)++;
            } else {
                const bool looksLikeIcs = contentType.contains("text/calendar")
                    || bodyPrefix.contains("BEGIN:VCALENDAR", Qt::CaseInsensitive);
                if (!looksLikeIcs) {
                    (*nonCalendarFeeds)++;
                } else {
                    appendIcsEvents(body, source, color, mergedEvents.data());
                }
            }

            reply->deleteLater();
            (*pending)--;
            finalize();
        });
    }

    finalize();
}

void FeedManager::loadSettings() {
    QSettings settings;
    m_feedUrls = settings.value("readonly/feedUrls").toString();
    m_autoRefreshEnabled = settings.value("readonly/autoRefreshEnabled", true).toBool();
    m_refreshIntervalMinutes = std::clamp(settings.value("readonly/refreshIntervalMinutes", 5).toInt(), 1, 180);
    m_timeFormatPreference = std::clamp(settings.value("readonly/timeFormatPreference", 0).toInt(), 0, 2);
    m_sundayFirst = settings.value("readonly/sundayFirst", false).toBool();
    m_displayName = settings.value("readonly/displayName", QStringLiteral("Family Calendar")).toString().trimmed();
    if (m_displayName.isEmpty()) {
        m_displayName = QStringLiteral("Family Calendar");
    }

    emit feedUrlsChanged();
    emit autoRefreshEnabledChanged();
    emit refreshIntervalMinutesChanged();
    emit timeFormatPreferenceChanged();
    emit sundayFirstChanged();
    emit displayNameChanged();

    setStatusMessage("Add shared ICS links.");
    updateAutoRefreshTimer();

    if (!normalizedUrlList().isEmpty()) {
        QTimer::singleShot(1200, this, [this]() {
            refreshFeedsInternal(false);
        });
    }
}

void FeedManager::setStatusMessage(const QString& value) {
    if (m_statusMessage == value) {
        return;
    }
    m_statusMessage = value;
    emit statusMessageChanged();
}

void FeedManager::setBusy(bool value) {
    if (m_busy == value) {
        return;
    }
    m_busy = value;
    emit busyChanged();
}

void FeedManager::updateAutoRefreshTimer() {
    m_autoRefreshTimer.stop();

    if (!m_autoRefreshEnabled) {
        return;
    }

    const int intervalMs = m_refreshIntervalMinutes * 60 * 1000;
    m_autoRefreshTimer.setInterval(intervalMs);
    m_autoRefreshTimer.setSingleShot(false);

    disconnect(&m_autoRefreshTimer, nullptr, this, nullptr);
    connect(&m_autoRefreshTimer, &QTimer::timeout, this, [this]() {
        refreshFeedsInternal(false);
    });

    m_autoRefreshTimer.start();
}

QStringList FeedManager::normalizedEntryList() const {
    QStringList parts = m_feedUrls.split('\n', Qt::SkipEmptyParts);
    QStringList result;
    result.reserve(parts.size());

    for (QString p : parts) {
        p = p.trimmed();
        if (!p.isEmpty()) {
            result.append(p);
        }
    }
    return result;
}

QStringList FeedManager::normalizedUrlList() const {
    QStringList result;
    for (const QString& entry : normalizedEntryList()) {
        const int pipe = entry.indexOf('|');
        QString url = pipe > 0 ? entry.mid(pipe + 1).trimmed() : entry;
        if (!url.isEmpty()) {
            result.append(url);
        }
    }
    result.removeDuplicates();
    return result;
}

QString FeedManager::colorForSource(const QString& source) const {
    static const QStringList palette = {
        "#0082c9", "#2daee0", "#4cb9c5", "#66bb6a",
        "#f4b13d", "#f28c38", "#e35d6a", "#9c6ade"
    };
    const uint hash = qHash(source.toLower());
    return palette.at(static_cast<int>(hash % palette.size()));
}

QString FeedManager::sourceNameFromUrl(const QString& url) const {
    QUrl parsed(url);
    if (!parsed.isValid()) {
        return QStringLiteral("Shared feed");
    }

    QString path = parsed.path();
    if (path.endsWith('/')) {
        path.chop(1);
    }
    const int slash = path.lastIndexOf('/');
    QString name = slash >= 0 ? path.mid(slash + 1) : path;
    if (name.endsWith(".ics", Qt::CaseInsensitive)) {
        name.chop(4);
    }
    if (name.isEmpty()) {
        name = parsed.host();
    }
    return name.isEmpty() ? QStringLiteral("Shared feed") : name;
}

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
#include <QColor>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QSharedPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>

#include "eventmodel.h"

namespace {
constexpr const char* kOrg = "CalDisplay";
constexpr const char* kApp = "CalDisplay";

QString settingsFilePath() {
    const QByteArray snapUserData = qgetenv("SNAP_USER_DATA");
    if (!snapUserData.isEmpty()) {
        return QString::fromLocal8Bit(snapUserData) + QStringLiteral("/CalDisplay.conf");
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/CalDisplay.conf");
}

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

QString extractTzidFromProperty(const QString& propertyNameWithParams) {
    const QStringList parts = propertyNameWithParams.split(';', Qt::SkipEmptyParts);
    for (int i = 1; i < parts.size(); ++i) {
        const QString part = parts.at(i).trimmed();
        if (!part.startsWith("TZID=", Qt::CaseInsensitive)) {
            continue;
        }

        QString tzid = part.mid(5).trimmed();
        if (tzid.size() >= 2 && tzid.startsWith('"') && tzid.endsWith('"')) {
            tzid = tzid.mid(1, tzid.size() - 2);
        }
        return tzid;
    }
    return {};
}

QDateTime parseIcsDateTimeWithTzid(const QString& rawValue, const QString& tzid) {
    const QString value = rawValue.trimmed();
    if (value.isEmpty()) {
        return {};
    }

    if (value.size() == 8) {
        return parseIcsDateTime(value);
    }

    if (value.endsWith('Z')) {
        return parseIcsDateTime(value);
    }

    QDateTime parsed = QDateTime::fromString(value, "yyyyMMdd'T'HHmmss");
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(value, "yyyyMMdd'T'HHmm");
    }
    if (!parsed.isValid()) {
        return {};
    }

    const QString normalizedTzid = tzid.trimmed();
    if (!normalizedTzid.isEmpty()) {
        QTimeZone zone(normalizedTzid.toUtf8());
        if (zone.isValid()) {
            return QDateTime(parsed.date(), parsed.time(), zone).toLocalTime();
        }
    }

    return parsed;
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

// ── RRULE helpers ─────────────────────────────────────────────────────────────

// Map a two-character ICS weekday name to Qt::DayOfWeek (1=Mon…7=Sun)
int icsDayToQt(const QString& s) {
    if (s == QStringLiteral("MO")) return Qt::Monday;
    if (s == QStringLiteral("TU")) return Qt::Tuesday;
    if (s == QStringLiteral("WE")) return Qt::Wednesday;
    if (s == QStringLiteral("TH")) return Qt::Thursday;
    if (s == QStringLiteral("FR")) return Qt::Friday;
    if (s == QStringLiteral("SA")) return Qt::Saturday;
    if (s == QStringLiteral("SU")) return Qt::Sunday;
    return -1;
}

// n-th (positive) or n-th-from-end (negative) occurrence of qtDow in year/month
QDate nthWeekdayInMonth(int year, int month, int n, int qtDow) {
    if (n > 0) {
        const QDate first(year, month, 1);
        int diff = qtDow - first.dayOfWeek();
        if (diff < 0) diff += 7;
        return first.addDays(diff + (n - 1) * 7);
    }
    // n < 0: count backwards from the last day of the month
    const QDate last(year, month, QDate(year, month, 1).daysInMonth());
    int diff = last.dayOfWeek() - qtDow;
    if (diff < 0) diff += 7;
    return last.addDays(-diff + (n + 1) * 7);
}

static constexpr int kRRuleWindowBefore = 30;
static constexpr int kRRuleWindowAfter  = 366;
static constexpr int kRRuleMaxExpand    = 100'000; // safety cap

// Expand a recurring VEVENT into individual CalendarEvent instances that fall
// inside the display window (today - kRRuleWindowBefore … today + kRRuleWindowAfter).
void expandRecurrences(
    const CalendarEvent&  base,
    const QString&        rruleStr,
    const QSet<QDate>&    exDates,
    QList<CalendarEvent>* out)
{
    // ── Parse RRULE key=value pairs ──────────────────────────────────────────
    QHash<QString, QString> rule;
    for (const QString& part : rruleStr.split(';', Qt::SkipEmptyParts)) {
        const int eq = part.indexOf('=');
        if (eq > 0)
            rule.insert(part.left(eq).toUpper(), part.mid(eq + 1).toUpper());
    }
    const QString freq = rule.value(QStringLiteral("FREQ"));
    if (freq.isEmpty()) return;

    const int interval = qMax(1, rule.value(QStringLiteral("INTERVAL"),
                                            QStringLiteral("1")).toInt());
    const int countLimit = rule.contains(QStringLiteral("COUNT"))
                           ? rule.value(QStringLiteral("COUNT")).toInt()
                           : kRRuleMaxExpand;
    QDateTime until;
    if (rule.contains(QStringLiteral("UNTIL")))
        until = parseIcsDateTime(rule.value(QStringLiteral("UNTIL")));

    // BYDAY: optional position prefix (e.g. "2MO", "-1FR") + weekday name
    struct ByDay { int pos; int qtDow; };
    QList<ByDay> byDays;
    if (rule.contains(QStringLiteral("BYDAY"))) {
        static const QRegularExpression bdRe(
            QStringLiteral(R"(([+-]?\d+)?(MO|TU|WE|TH|FR|SA|SU))"));
        for (const QString& tok :
             rule.value(QStringLiteral("BYDAY")).split(',', Qt::SkipEmptyParts)) {
            const auto m = bdRe.match(tok);
            if (m.hasMatch()) {
                const int pos = m.captured(1).isEmpty() ? 0 : m.captured(1).toInt();
                const int dow = icsDayToQt(m.captured(2));
                if (dow != -1) byDays.append({pos, dow});
            }
        }
    }

    QList<int> byMonthDay;
    for (const QString& s :
         rule.value(QStringLiteral("BYMONTHDAY")).split(',', Qt::SkipEmptyParts))
        if (const int v = s.toInt(); v != 0) byMonthDay.append(v);

    QList<int> byMonth;
    for (const QString& s :
         rule.value(QStringLiteral("BYMONTH")).split(',', Qt::SkipEmptyParts))
        if (const int v = s.toInt(); v >= 1 && v <= 12) byMonth.append(v);

    // ── Window & state ───────────────────────────────────────────────────────
    const QDate windowStart = QDate::currentDate().addDays(-kRRuleWindowBefore);
    const QDate windowEnd   = QDate::currentDate().addDays(kRRuleWindowAfter);
    const qint64 durSecs    = base.start.secsTo(base.end);

    bool done      = false;
    int  generated = 0;

    // Process one occurrence: count it, check limits, emit if in window.
    auto tryOcc = [&](const QDateTime& occ) {
        if (done) return;
        if (occ < base.start)                   return;       // before series start
        if (until.isValid() && occ > until)     { done = true; return; }
        if (occ.date() > windowEnd)             { done = true; return; }
        if (++generated > countLimit)           { done = true; return; }
        if (exDates.contains(occ.date()))        return;       // excluded date
        if (occ.date() < windowStart)            return;       // before display window
        CalendarEvent ev = base;
        ev.start = occ;
        ev.end   = occ.addSecs(durSecs);
        out->append(ev);
    };

    // ── Expand by FREQ ───────────────────────────────────────────────────────
    if (freq == QStringLiteral("DAILY")) {
        for (QDateTime cur = base.start; !done; cur = cur.addDays(interval))
            tryOcc(cur);

    } else if (freq == QStringLiteral("WEEKLY")) {
        for (QDateTime anchor = base.start;
             !done && anchor.date() <= windowEnd;
             anchor = anchor.addDays(7 * interval)) {
            if (byDays.isEmpty()) {
                tryOcc(anchor);
            } else {
                for (const ByDay& bd : byDays) {
                    if (done) break;
                    int diff = bd.qtDow - anchor.date().dayOfWeek();
                    QDateTime occ(anchor.date().addDays(diff),
                                  anchor.time(), anchor.timeZone());
                    tryOcc(occ);
                }
            }
        }

    } else if (freq == QStringLiteral("MONTHLY")) {
        for (int mo = 0; !done; mo += interval) {
            const QDate mBase = base.start.date().addMonths(mo);
            if (!mBase.isValid() || mBase > windowEnd) break;

            QList<QDate> candidates;
            if (!byDays.isEmpty()) {
                for (const ByDay& bd : byDays) {
                    if (bd.pos == 0) {
                        for (QDate d(mBase.year(), mBase.month(), 1);
                             d.month() == mBase.month(); d = d.addDays(1))
                            if (d.dayOfWeek() == bd.qtDow) candidates.append(d);
                    } else {
                        const QDate d = nthWeekdayInMonth(
                            mBase.year(), mBase.month(), bd.pos, bd.qtDow);
                        if (d.isValid()) candidates.append(d);
                    }
                }
            } else if (!byMonthDay.isEmpty()) {
                for (int mday : byMonthDay) {
                    const QDate d(mBase.year(), mBase.month(), mday);
                    if (d.isValid()) candidates.append(d);
                }
            } else {
                const QDate d(mBase.year(), mBase.month(), base.start.date().day());
                if (d.isValid()) candidates.append(d);
            }
            std::sort(candidates.begin(), candidates.end());
            for (const QDate& d : candidates)
                tryOcc(QDateTime(d, base.start.time(), base.start.timeZone()));
        }

    } else if (freq == QStringLiteral("YEARLY")) {
        for (int y = 0; !done; ++y) {
            const int year = base.start.date().year() + y * interval;
            if (year > windowEnd.year() + 1) break;

            QList<QDate> candidates;
            if (!byMonth.isEmpty() && !byMonthDay.isEmpty()) {
                for (int mo : byMonth)
                    for (int md : byMonthDay) {
                        const QDate d(year, mo, md);
                        if (d.isValid()) candidates.append(d);
                    }
            } else if (!byMonth.isEmpty()) {
                for (int mo : byMonth) {
                    const QDate d(year, mo, base.start.date().day());
                    if (d.isValid()) candidates.append(d);
                }
            } else if (!byDays.isEmpty()) {
                // e.g. FREQ=YEARLY;BYDAY=1SU;BYMONTH=4 → 1st Sunday in April
                for (const ByDay& bd : byDays) {
                    if (bd.pos != 0) {
                        const int mo = byMonth.isEmpty()
                                       ? base.start.date().month()
                                       : byMonth.first();
                        const QDate d = nthWeekdayInMonth(year, mo, bd.pos, bd.qtDow);
                        if (d.isValid()) candidates.append(d);
                    }
                }
            } else {
                const QDate d(year,
                              base.start.date().month(),
                              base.start.date().day());
                if (d.isValid()) candidates.append(d);
            }
            std::sort(candidates.begin(), candidates.end());
            for (const QDate& d : candidates)
                tryOcc(QDateTime(d, base.start.time(), base.start.timeZone()));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

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
    QString dtStartTzid;
    QString dtEndTzid;
    QString rruleRaw;
    QList<QPair<QString, QString>> exDateValues;
    QString status = QStringLiteral("CONFIRMED");

    for (const QString& originalLine : lines) {
        const QString line = originalLine.trimmed();
        if (line == "BEGIN:VEVENT") {
            inEvent = true;
            summary.clear();
            dtStartRaw.clear();
            dtEndRaw.clear();
            dtStartTzid.clear();
            dtEndTzid.clear();
            rruleRaw.clear();
            exDateValues.clear();
            status = QStringLiteral("CONFIRMED");
            continue;
        }

        if (line == "END:VEVENT") {
            if (inEvent) {
                CalendarEvent ev;
                ev.title = summary.isEmpty() ? QStringLiteral("(No title)") : summary;
                ev.calendar = source;
                ev.color = color;
                ev.start = parseIcsDateTimeWithTzid(dtStartRaw, dtStartTzid);
                ev.end = parseIcsDateTimeWithTzid(dtEndRaw, dtEndTzid);
                ev.status = status;
                if (!ev.start.isValid()) {
                    inEvent = false;
                    continue;
                }
                if (!ev.end.isValid() || ev.end < ev.start) {
                    ev.end = ev.start.addSecs(60 * 60);
                }
                if (!rruleRaw.isEmpty()) {
                    QSet<QDate> exDates;
                    for (const auto& ex : exDateValues) {
                        const QDateTime exDt = parseIcsDateTimeWithTzid(ex.first, ex.second);
                        if (exDt.isValid()) exDates.insert(exDt.date());
                    }
                    expandRecurrences(ev, rruleRaw, exDates, mergedEvents);
                } else {
                    mergedEvents->append(ev);
                }
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

        const QString propertyWithParams = line.left(colon).trimmed();
        QString key = propertyWithParams.toUpper();
        const QString value = line.mid(colon + 1).trimmed();
        const QString tzid = extractTzidFromProperty(propertyWithParams);

        const int semi = key.indexOf(';');
        if (semi > 0) {
            key = key.left(semi);
        }

        if (key == "SUMMARY") {
            summary = value;
        } else if (key == "DTSTART") {
            dtStartRaw = value;
            dtStartTzid = tzid;
        } else if (key == "DTEND") {
            dtEndRaw = value;
            dtEndTzid = tzid;
        } else if (key == "RRULE") {
            rruleRaw = value;
        } else if (key == "EXDATE") {
            const QStringList exList = value.split(',', Qt::SkipEmptyParts);
            for (const QString& ex : exList) {
                exDateValues.append({ex.trimmed(), tzid});
            }
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

QString sourceKeyFor(const QString& source) {
    return source.trimmed().toCaseFolded();
}

QString normalizeVisibleViews(const QString& raw) {
    static const QStringList allowed = {
        QStringLiteral("day"),
        QStringLiteral("week"),
        QStringLiteral("month"),
        QStringLiteral("twomonths"),
        QStringLiteral("weather")
    };

    QStringList out;
    for (const QString& token : raw.split(',', Qt::SkipEmptyParts)) {
        const QString key = token.trimmed().toLower();
        if (!allowed.contains(key) || out.contains(key))
            continue;
        out.append(key);
    }

    if (out.isEmpty())
        out.append(QStringLiteral("day"));

    return out.join(',');
}

QHash<QString, QString> buildSourceColorMap(const QStringList& sources) {
    static const QStringList basePalette = {
        QStringLiteral("#0082c9"),
        QStringLiteral("#2daee0"),
        QStringLiteral("#4cb9c5"),
        QStringLiteral("#66bb6a"),
        QStringLiteral("#f4b13d"),
        QStringLiteral("#f28c38"),
        QStringLiteral("#e35d6a"),
        QStringLiteral("#9c6ade")
    };

    QHash<QString, QString> assigned;
    QSet<QString> used;
    int nextIndex = 0;

    for (const QString& source : sources) {
        const QString key = sourceKeyFor(source);
        if (key.isEmpty() || assigned.contains(key))
            continue;

        QString color;
        if (nextIndex < basePalette.size()) {
            color = basePalette.at(nextIndex);
        } else {
            // Generate additional distinct colors after the base palette is exhausted.
            int attempt = 0;
            do {
                const int hue = (nextIndex * 137 + attempt * 37) % 360;
                const QColor generated = QColor::fromHsv(hue, 165, 224);
                color = generated.name(QColor::HexRgb);
                ++attempt;
            } while (used.contains(color) && attempt < 360);
        }

        assigned.insert(key, color);
        used.insert(color);
        ++nextIndex;
    }

    return assigned;
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
QString FeedManager::visibleViews() const { return m_visibleViews; }

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

void FeedManager::setVisibleViews(const QString& value) {
    const QString normalized = normalizeVisibleViews(value);
    if (m_visibleViews == normalized) {
        return;
    }
    m_visibleViews = normalized;
    emit visibleViewsChanged();
}

void FeedManager::saveSettings() {
    const QString filePath = settingsFilePath();
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QSettings settings(filePath, QSettings::IniFormat);
    settings.setValue("readonly/feedUrls", m_feedUrls);
    settings.setValue("readonly/autoRefreshEnabled", m_autoRefreshEnabled);
    settings.setValue("readonly/refreshIntervalMinutes", m_refreshIntervalMinutes);
    settings.setValue("readonly/timeFormatPreference", m_timeFormatPreference);
    settings.setValue("readonly/sundayFirst", m_sundayFirst);
    settings.setValue("readonly/displayName", m_displayName);
    settings.setValue("readonly/visibleViews", m_visibleViews);
    settings.sync();
    setStatusMessage(QStringLiteral("Settings saved."));
}

void FeedManager::refreshFeeds() {
    refreshFeedsInternal(true);
}

void FeedManager::refreshFeedsIfDue() {
    if (!m_autoRefreshEnabled || normalizedEntryList().isEmpty()) {
        return;
    }

    if (!m_lastSync.isValid()) {
        refreshFeedsInternal(false);
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const int intervalSeconds = std::clamp(m_refreshIntervalMinutes, 1, 180) * 60;
    if (m_lastSync.secsTo(now) >= intervalSeconds) {
        refreshFeedsInternal(false);
    }
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

    QStringList sourceOrder;
    sourceOrder.reserve(entries.size());
    for (const QString& rawEntry : entries) {
        const int entryPipe = rawEntry.indexOf('|');
        const QString customName = entryPipe > 0 ? rawEntry.left(entryPipe).trimmed() : QString();
        QString urlText = entryPipe > 0 ? rawEntry.mid(entryPipe + 1).trimmed() : rawEntry.trimmed();
        if (urlText.startsWith(QStringLiteral("webcal://"), Qt::CaseInsensitive)) {
            urlText.replace(0, 9, QStringLiteral("https://"));
        }

        static const QRegularExpression ncShareRe(
            QStringLiteral(R"(^(https?://[^/]+)/index\.php/apps/calendar/p/([^/?#]+))"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch ncMatch = ncShareRe.match(urlText);
        if (ncMatch.hasMatch()) {
            urlText = ncMatch.captured(1)
                      + QStringLiteral("/remote.php/dav/public-calendars/")
                      + ncMatch.captured(2)
                      + QStringLiteral("?export");
        }

        const QString source = customName.isEmpty() ? sourceNameFromUrl(urlText) : customName;
        sourceOrder.append(source);
    }
    const QHash<QString, QString> sourceColors = buildSourceColorMap(sourceOrder);

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
        const QDateTime newest = QDateTime(today.addDays(365), QTime(23, 59, 59));

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
        const QString color = sourceColors.value(sourceKeyFor(source), QStringLiteral("#0082c9"));

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
    const QString filePath = settingsFilePath();
    QSettings settings(filePath, QSettings::IniFormat);
    m_feedUrls = settings.value("readonly/feedUrls").toString();
    m_autoRefreshEnabled = settings.value("readonly/autoRefreshEnabled", true).toBool();
    m_refreshIntervalMinutes = std::clamp(settings.value("readonly/refreshIntervalMinutes", 5).toInt(), 1, 180);
    m_timeFormatPreference = std::clamp(settings.value("readonly/timeFormatPreference", 0).toInt(), 0, 2);
    m_sundayFirst = settings.value("readonly/sundayFirst", false).toBool();
    m_displayName = settings.value("readonly/displayName", QStringLiteral("Family Calendar")).toString().trimmed();
    m_visibleViews = normalizeVisibleViews(
        settings.value("readonly/visibleViews", QStringLiteral("day,week,month,twomonths,weather")).toString());
    if (m_displayName.isEmpty()) {
        m_displayName = QStringLiteral("Family Calendar");
    }

    emit feedUrlsChanged();
    emit autoRefreshEnabledChanged();
    emit refreshIntervalMinutesChanged();
    emit timeFormatPreferenceChanged();
    emit sundayFirstChanged();
    emit displayNameChanged();
    emit visibleViewsChanged();

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

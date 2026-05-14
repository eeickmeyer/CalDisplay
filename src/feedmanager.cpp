#include "feedmanager.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDate>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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

void FeedManager::saveSettings() {
    QSettings settings;
    settings.setValue("readonly/feedUrls", m_feedUrls);
    settings.setValue("readonly/autoRefreshEnabled", m_autoRefreshEnabled);
    settings.setValue("readonly/refreshIntervalMinutes", m_refreshIntervalMinutes);
    settings.sync();
    setStatusMessage("Read-only settings saved.");
}

void FeedManager::refreshFeeds() {
    refreshFeedsInternal(true);
}

void FeedManager::refreshFeedsInternal(bool interactive) {
    if (busy()) {
        return;
    }

    const QStringList urls = normalizedUrlList();
    if (urls.isEmpty()) {
        if (interactive) {
            setStatusMessage("Add one or more ICS/webcal links first.");
        }
        return;
    }

    setBusy(true);
    if (interactive) {
        setStatusMessage("Refreshing read-only calendar feeds...");
    }

    auto pending = QSharedPointer<int>::create(urls.size());
    auto mergedEvents = QSharedPointer<QList<CalendarEvent>>::create();

    auto finalize = [this, pending, mergedEvents, urls]() {
        if (*pending != 0) {
            return;
        }

        QList<CalendarEvent> filtered;
        filtered.reserve(mergedEvents->size());

        const QDateTime now = QDateTime::currentDateTime();
        const QDateTime oldest = now.addDays(-2);
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

        setStatusMessage(QString("Loaded %1 event(s) from %2 read-only feed(s).")
                             .arg(filtered.size())
                             .arg(urls.size()));
        setBusy(false);
    };

    for (const QString& rawUrl : urls) {
        QString urlText = rawUrl;
        if (urlText.startsWith("webcal://", Qt::CaseInsensitive)) {
            urlText.replace(0, 9, "https://");
        }

        const QUrl url(urlText);
        if (!url.isValid()) {
            (*pending)--;
            finalize();
            continue;
        }

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "CalDisplay/0.1");

        QNetworkReply* reply = m_nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, pending, mergedEvents, rawUrl, finalize]() {
            if (reply->error() == QNetworkReply::NoError) {
                const QByteArray body = reply->readAll();
                const QString unfolded = unfoldIcs(body);
                const QStringList lines = unfolded.split('\n', Qt::KeepEmptyParts);

                const QString source = sourceNameFromUrl(rawUrl);
                const QString color = colorForSource(source);

                bool inEvent = false;
                QString summary;
                QString dtStartRaw;
                QString dtEndRaw;

                for (const QString& originalLine : lines) {
                    const QString line = originalLine.trimmed();
                    if (line == "BEGIN:VEVENT") {
                        inEvent = true;
                        summary.clear();
                        dtStartRaw.clear();
                        dtEndRaw.clear();
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
                    }
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

    emit feedUrlsChanged();
    emit autoRefreshEnabledChanged();
    emit refreshIntervalMinutesChanged();

    setStatusMessage("Read-only mode: add shared ICS links.");
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

QStringList FeedManager::normalizedUrlList() const {
    QStringList parts = m_feedUrls.split('\n', Qt::SkipEmptyParts);
    QStringList result;
    result.reserve(parts.size());

    for (QString p : parts) {
        p = p.trimmed();
        if (!p.isEmpty()) {
            result.append(p);
        }
    }
    result.removeDuplicates();
    return result;
}

QString FeedManager::colorForSource(const QString& source) const {
    static const QStringList palette = {
        "#2f855a", "#2b6cb0", "#d69e2e", "#c53030", "#6b46c1", "#0f766e", "#b45309", "#7c3aed"
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

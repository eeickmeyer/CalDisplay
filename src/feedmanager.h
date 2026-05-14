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

public:
    explicit FeedManager(EventModel* eventModel, QObject* parent = nullptr);

    QString feedUrls() const;
    QString statusMessage() const;
    bool busy() const;
    QString lastSync() const;
    bool autoRefreshEnabled() const;
    int refreshIntervalMinutes() const;

    void setFeedUrls(const QString& value);
    void setAutoRefreshEnabled(bool value);
    void setRefreshIntervalMinutes(int value);

    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void refreshFeeds();

signals:
    void feedUrlsChanged();
    void statusMessageChanged();
    void busyChanged();
    void lastSyncChanged();
    void autoRefreshEnabledChanged();
    void refreshIntervalMinutesChanged();

private:
    QString m_feedUrls;
    QString m_statusMessage;
    bool m_busy = false;
    QDateTime m_lastSync;
    bool m_autoRefreshEnabled = true;
    int m_refreshIntervalMinutes = 5;
    QTimer m_autoRefreshTimer;

    EventModel* m_eventModel;
    QNetworkAccessManager* m_nam;

    void loadSettings();
    void setStatusMessage(const QString& value);
    void setBusy(bool value);
    void updateAutoRefreshTimer();
    void refreshFeedsInternal(bool interactive);

    QStringList normalizedUrlList() const;
    QString colorForSource(const QString& source) const;
    QString sourceNameFromUrl(const QString& url) const;
};

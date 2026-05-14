#pragma once

#include <QObject>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

class AccountManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY serverUrlChanged)
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QStringList availableCalendars READ availableCalendars NOTIFY availableCalendarsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY configuredChanged)

public:
    explicit AccountManager(QObject* parent = nullptr);

    QString serverUrl() const;
    QString username() const;
    QString password() const;
    QString statusMessage() const;
    QStringList availableCalendars() const;
    bool busy() const;
    bool configured() const;

    void setServerUrl(const QString& value);
    void setUsername(const QString& value);
    void setPassword(const QString& value);

    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void discoverCalendars();

signals:
    void serverUrlChanged();
    void usernameChanged();
    void passwordChanged();
    void statusMessageChanged();
    void availableCalendarsChanged();
    void busyChanged();
    void configuredChanged();

private:
    QString m_serverUrl;
    QString m_username;
    QString m_password;
    QString m_statusMessage;
    QStringList m_availableCalendars;
    bool m_busy = false;

    QNetworkAccessManager* m_nam;

    void loadSettings();
    void setStatusMessage(const QString& value);
    void setBusy(bool value);
    void setAvailableCalendars(const QStringList& value);

    QString normalizedBaseUrl() const;
    QByteArray basicAuthHeader() const;

    void beginPrincipalDiscovery();
    void handlePrincipalReply(QNetworkReply* reply);
    void beginCalendarHomeDiscovery(const QString& principalHref);
    void handleCalendarHomeReply(QNetworkReply* reply);
    void beginCalendarListDiscovery(const QString& calendarHomeHref);
    void handleCalendarListReply(QNetworkReply* reply);

    QString extractFirstHref(const QByteArray& xml) const;
    QString extractCalendarHomeHref(const QByteArray& xml) const;
    QStringList extractCalendarNames(const QByteArray& xml) const;
};

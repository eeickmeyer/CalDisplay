#include "accountmanager.h"

#include <QCoreApplication>
#include <QDomDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

namespace {
constexpr const char* kOrg = "CalDisplay";
constexpr const char* kApp = "CalDisplay";

const char* kPrincipalQuery =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\">"
    "  <d:prop><d:current-user-principal/></d:prop>"
    "</d:propfind>";

const char* kCalendarHomeQuery =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
    "  <d:prop><c:calendar-home-set/></d:prop>"
    "</d:propfind>";

const char* kCalendarListQuery =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\">"
    "  <d:prop><d:displayname/><d:resourcetype/></d:prop>"
    "</d:propfind>";
}

AccountManager::AccountManager(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this)) {
    QCoreApplication::setOrganizationName(QString::fromUtf8(kOrg));
    QCoreApplication::setApplicationName(QString::fromUtf8(kApp));
    loadSettings();
}

QString AccountManager::serverUrl() const { return m_serverUrl; }
QString AccountManager::username() const { return m_username; }
QString AccountManager::password() const { return m_password; }
QString AccountManager::statusMessage() const { return m_statusMessage; }
QStringList AccountManager::availableCalendars() const { return m_availableCalendars; }
bool AccountManager::busy() const { return m_busy; }

bool AccountManager::configured() const {
    return !m_serverUrl.trimmed().isEmpty() && !m_username.trimmed().isEmpty() && !m_password.isEmpty();
}

void AccountManager::setServerUrl(const QString& value) {
    if (m_serverUrl == value) {
        return;
    }
    const bool wasConfigured = configured();
    m_serverUrl = value;
    emit serverUrlChanged();
    if (wasConfigured != configured()) {
        emit configuredChanged();
    }
}

void AccountManager::setUsername(const QString& value) {
    if (m_username == value) {
        return;
    }
    const bool wasConfigured = configured();
    m_username = value;
    emit usernameChanged();
    if (wasConfigured != configured()) {
        emit configuredChanged();
    }
}

void AccountManager::setPassword(const QString& value) {
    if (m_password == value) {
        return;
    }
    const bool wasConfigured = configured();
    m_password = value;
    emit passwordChanged();
    if (wasConfigured != configured()) {
        emit configuredChanged();
    }
}

void AccountManager::saveSettings() {
    QSettings settings;
    settings.setValue("nextcloud/serverUrl", m_serverUrl.trimmed());
    settings.setValue("nextcloud/username", m_username.trimmed());
    settings.setValue("nextcloud/password", m_password);
    settings.sync();

    setStatusMessage("Account settings saved.");
}

void AccountManager::discoverCalendars() {
    if (busy()) {
        return;
    }

    if (!configured()) {
        setStatusMessage("Please complete server URL, username, and app password.");
        return;
    }

    setBusy(true);
    setStatusMessage("Connecting to Nextcloud CalDAV...");
    beginPrincipalDiscovery();
}

void AccountManager::loadSettings() {
    QSettings settings;
    m_serverUrl = settings.value("nextcloud/serverUrl").toString();
    m_username = settings.value("nextcloud/username").toString();
    m_password = settings.value("nextcloud/password").toString();

    emit serverUrlChanged();
    emit usernameChanged();
    emit passwordChanged();
    emit configuredChanged();

    if (configured()) {
        setStatusMessage("Saved account loaded.");
    } else {
        setStatusMessage("Configure Nextcloud account.");
    }
}

void AccountManager::setStatusMessage(const QString& value) {
    if (m_statusMessage == value) {
        return;
    }
    m_statusMessage = value;
    emit statusMessageChanged();
}

void AccountManager::setBusy(bool value) {
    if (m_busy == value) {
        return;
    }
    m_busy = value;
    emit busyChanged();
}

void AccountManager::setAvailableCalendars(const QStringList& value) {
    if (m_availableCalendars == value) {
        return;
    }
    m_availableCalendars = value;
    emit availableCalendarsChanged();
}

QString AccountManager::normalizedBaseUrl() const {
    QString url = m_serverUrl.trimmed();
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
        url.prepend("https://");
    }

    if (!url.endsWith('/')) {
        url += '/';
    }

    const QString lower = url.toLower();
    if (!lower.contains("/remote.php/dav")) {
        if (!url.endsWith('/')) {
            url += '/';
        }
        url += "remote.php/dav/";
    }

    return url;
}

QByteArray AccountManager::basicAuthHeader() const {
    const QByteArray creds = (m_username.trimmed() + ":" + m_password).toUtf8();
    return "Basic " + creds.toBase64();
}

void AccountManager::beginPrincipalDiscovery() {
    const QUrl url(normalizedBaseUrl());
    if (!url.isValid()) {
        setStatusMessage("Server URL is invalid.");
        setBusy(false);
        return;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Depth", "0");
    request.setRawHeader("Authorization", basicAuthHeader());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QNetworkReply* reply = m_nam->sendCustomRequest(request, "PROPFIND", QByteArray(kPrincipalQuery));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handlePrincipalReply(reply);
    });
}

void AccountManager::handlePrincipalReply(QNetworkReply* reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (status < 200 || status >= 300) {
        setStatusMessage(QString("Nextcloud auth/discovery failed (HTTP %1).").arg(status));
        setBusy(false);
        return;
    }

    const QString principalHref = extractFirstHref(body);
    if (principalHref.isEmpty()) {
        setStatusMessage("Could not find current-user-principal from server response.");
        setBusy(false);
        return;
    }

    beginCalendarHomeDiscovery(principalHref);
}

void AccountManager::beginCalendarHomeDiscovery(const QString& principalHref) {
    const QUrl base(normalizedBaseUrl());
    const QUrl url = base.resolved(QUrl(principalHref));

    QNetworkRequest request(url);
    request.setRawHeader("Depth", "0");
    request.setRawHeader("Authorization", basicAuthHeader());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QNetworkReply* reply = m_nam->sendCustomRequest(request, "PROPFIND", QByteArray(kCalendarHomeQuery));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleCalendarHomeReply(reply);
    });
}

void AccountManager::handleCalendarHomeReply(QNetworkReply* reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (status < 200 || status >= 300) {
        setStatusMessage(QString("Calendar-home discovery failed (HTTP %1).").arg(status));
        setBusy(false);
        return;
    }

    const QString calendarHomeHref = extractCalendarHomeHref(body);
    if (calendarHomeHref.isEmpty()) {
        setStatusMessage("No calendar-home-set found. Verify this account has calendars.");
        setBusy(false);
        return;
    }

    beginCalendarListDiscovery(calendarHomeHref);
}

void AccountManager::beginCalendarListDiscovery(const QString& calendarHomeHref) {
    const QUrl base(normalizedBaseUrl());
    const QUrl url = base.resolved(QUrl(calendarHomeHref));

    QNetworkRequest request(url);
    request.setRawHeader("Depth", "1");
    request.setRawHeader("Authorization", basicAuthHeader());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QNetworkReply* reply = m_nam->sendCustomRequest(request, "PROPFIND", QByteArray(kCalendarListQuery));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleCalendarListReply(reply);
    });
}

void AccountManager::handleCalendarListReply(QNetworkReply* reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (status < 200 || status >= 300) {
        setStatusMessage(QString("Calendar list discovery failed (HTTP %1).").arg(status));
        setBusy(false);
        return;
    }

    const QStringList calendars = extractCalendarNames(body);
    setAvailableCalendars(calendars);

    if (calendars.isEmpty()) {
        setStatusMessage("Connected, but no calendars were discovered.");
    } else {
        setStatusMessage(QString("Connected. Found %1 calendar(s).").arg(calendars.size()));
    }

    setBusy(false);
}

QString AccountManager::extractFirstHref(const QByteArray& xml) const {
    QDomDocument doc;
    if (!doc.setContent(xml)) {
        return {};
    }

    const QDomNodeList hrefNodes = doc.elementsByTagNameNS("DAV:", "href");
    if (hrefNodes.isEmpty()) {
        return {};
    }

    return hrefNodes.at(0).toElement().text().trimmed();
}

QString AccountManager::extractCalendarHomeHref(const QByteArray& xml) const {
    QDomDocument doc;
    if (!doc.setContent(xml)) {
        return {};
    }

    const QDomNodeList homeNodes = doc.elementsByTagNameNS("urn:ietf:params:xml:ns:caldav", "calendar-home-set");
    if (homeNodes.isEmpty()) {
        return {};
    }

    const QDomElement home = homeNodes.at(0).toElement();
    const QDomNodeList hrefNodes = home.elementsByTagNameNS("DAV:", "href");
    if (hrefNodes.isEmpty()) {
        return {};
    }

    return hrefNodes.at(0).toElement().text().trimmed();
}

QStringList AccountManager::extractCalendarNames(const QByteArray& xml) const {
    QStringList names;

    QDomDocument doc;
    if (!doc.setContent(xml)) {
        return names;
    }

    const QDomNodeList responses = doc.elementsByTagNameNS("DAV:", "response");
    for (int i = 0; i < responses.size(); ++i) {
        const QDomElement response = responses.at(i).toElement();

        const QDomNodeList propstats = response.elementsByTagNameNS("DAV:", "propstat");
        bool isCalendarCollection = false;
        QString displayName;

        for (int j = 0; j < propstats.size(); ++j) {
            const QDomElement propstat = propstats.at(j).toElement();
            const QDomNodeList props = propstat.elementsByTagNameNS("DAV:", "prop");
            if (props.isEmpty()) {
                continue;
            }
            const QDomElement prop = props.at(0).toElement();

            const QDomNodeList resourcetypes = prop.elementsByTagNameNS("DAV:", "resourcetype");
            if (!resourcetypes.isEmpty()) {
                const QDomElement resourcetype = resourcetypes.at(0).toElement();
                const QDomNodeList calendarNodes = resourcetype.elementsByTagNameNS("urn:ietf:params:xml:ns:caldav", "calendar");
                isCalendarCollection = !calendarNodes.isEmpty();
            }

            const QDomNodeList displayNames = prop.elementsByTagNameNS("DAV:", "displayname");
            if (!displayNames.isEmpty()) {
                displayName = displayNames.at(0).toElement().text().trimmed();
            }
        }

        if (isCalendarCollection) {
            if (displayName.isEmpty()) {
                const QDomNodeList hrefs = response.elementsByTagNameNS("DAV:", "href");
                if (!hrefs.isEmpty()) {
                    displayName = hrefs.at(0).toElement().text().trimmed();
                }
            }
            names.append(displayName);
        }
    }

    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

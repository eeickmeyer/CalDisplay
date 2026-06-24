// SPDX-License-Identifier: GPL-3.0-or-later
// CalDisplay - A calendar application for displaying events from shared ICS feeds
// Copyright (C) 2026 Erich Eickmeyer
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "weathermanager.h"

#include <algorithm>
#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QIcon>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QDirIterator>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#ifdef HAS_QT_POSITIONING
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#endif

// ── Anonymous namespace helpers ───────────────────────────────────────────────
namespace {

QString weatherSettingsFilePath() {
    const QByteArray snapUserData = qgetenv("SNAP_USER_DATA");
    if (!snapUserData.isEmpty())
        return QString::fromLocal8Bit(snapUserData) + QStringLiteral("/CalDisplay.conf");
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
           + QStringLiteral("/CalDisplay.conf");
}

// Safe JSON double extraction (returns defaultVal if key missing or not a number)
double jd(const QJsonObject& obj, const QString& key, double defaultVal = 0.0) {
    const QJsonValue v = obj.value(key);
    return v.isDouble() ? v.toDouble() : defaultVal;
}
int ji(const QJsonObject& obj, const QString& key, int defaultVal = 0) {
    const QJsonValue v = obj.value(key);
    return v.isDouble() ? static_cast<int>(v.toDouble()) : defaultVal;
}
QString js(const QJsonObject& obj, const QString& key) {
    return obj.value(key).toString();
}

QString renderSvgToCachedPng(const QString& svgPath, const QString& cachePath, int size) {
    if (svgPath.isEmpty() || cachePath.isEmpty() || size <= 0)
        return QString();

    QSvgRenderer renderer(svgPath);
    if (!renderer.isValid())
        return QString();

    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();

    if (image.save(cachePath, "PNG"))
        return QUrl::fromLocalFile(cachePath).toString();

    return QString();
}

} // namespace

// ── Static helpers ────────────────────────────────────────────────────────────

QString WeatherManager::wmoDescription(int code) {
    switch (code) {
    case 0:  return QStringLiteral("Clear sky");
    case 1:  return QStringLiteral("Mainly clear");
    case 2:  return QStringLiteral("Partly cloudy");
    case 3:  return QStringLiteral("Overcast");
    case 45: return QStringLiteral("Fog");
    case 48: return QStringLiteral("Icy fog");
    case 51: return QStringLiteral("Light drizzle");
    case 53: return QStringLiteral("Drizzle");
    case 55: return QStringLiteral("Heavy drizzle");
    case 56: return QStringLiteral("Freezing drizzle");
    case 57: return QStringLiteral("Heavy freezing drizzle");
    case 61: return QStringLiteral("Light rain");
    case 63: return QStringLiteral("Rain");
    case 65: return QStringLiteral("Heavy rain");
    case 66: return QStringLiteral("Freezing rain");
    case 67: return QStringLiteral("Heavy freezing rain");
    case 71: return QStringLiteral("Light snow");
    case 73: return QStringLiteral("Snow");
    case 75: return QStringLiteral("Heavy snow");
    case 77: return QStringLiteral("Snow grains");
    case 80: return QStringLiteral("Light showers");
    case 81: return QStringLiteral("Showers");
    case 82: return QStringLiteral("Heavy showers");
    case 85: return QStringLiteral("Snow showers");
    case 86: return QStringLiteral("Heavy snow showers");
    case 95: return QStringLiteral("Thunderstorm");
    case 96: return QStringLiteral("Thunderstorm w/ hail");
    case 99: return QStringLiteral("Thunderstorm w/ heavy hail");
    default: return QStringLiteral("Unknown");
    }
}

QString WeatherManager::wmoIcon(int code) {
    // Return freedesktop-style icon names so QML can resolve them from the active icon theme (Papirus).
    if (code == 0)                       return QStringLiteral("weather-clear");
    if (code == 1)                       return QStringLiteral("weather-few-clouds");
    if (code == 2)                       return QStringLiteral("weather-few-clouds");
    if (code == 3)                       return QStringLiteral("weather-overcast");
    if (code == 45 || code == 48)        return QStringLiteral("weather-fog");
    if (code >= 51 && code <= 57)        return QStringLiteral("weather-showers-scattered");
    if (code >= 61 && code <= 67)        return QStringLiteral("weather-showers");
    if (code >= 71 && code <= 77)        return QStringLiteral("weather-snow");
    if (code >= 80 && code <= 82)        return QStringLiteral("weather-showers");
    if (code == 85 || code == 86)        return QStringLiteral("weather-snow");
    if (code >= 95)                      return QStringLiteral("weather-storm");
    return QStringLiteral("weather-overcast");
}

QString WeatherManager::precipTypeFromCode(int code) {
    // Determine precipitation type from WMO weather code
    if (code == 0 || code == 1 || code == 2 || code == 3) return QString(); // clear/cloudy, no precip
    if (code >= 45 && code <= 48) return QString(); // fog, no precip
    if (code >= 51 && code <= 57) return QStringLiteral("Drizzle");
    if (code >= 61 && code <= 67) return QStringLiteral("Rain");
    if (code >= 71 && code <= 77) return QStringLiteral("Snow");
    if (code >= 80 && code <= 82) return QStringLiteral("Rain");
    if (code == 85 || code == 86) return QStringLiteral("Snow");
    if (code >= 95 && code <= 99) return QStringLiteral("Storm");
    return QString(); // unknown, no label
}

QString WeatherManager::windDirText(int degrees) {
    static const char* dirs[] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    const int idx = static_cast<int>((degrees + 11.25) / 22.5) % 16;
    return QString::fromLatin1(dirs[idx]);
}

// Map OWM weather ID to approximate WMO code for consistent icon/description
int WeatherManager::owmIdToWmo(int id) {
    if (id == 800) return 0;
    if (id == 801) return 1;
    if (id == 802) return 2;
    if (id >= 803) return 3;
    if (id >= 700) return 45;  // atmosphere (fog, mist, haze, dust…)
    if (id >= 600) return 73;  // snow
    if (id >= 500) return 63;  // rain
    if (id >= 300) return 53;  // drizzle
    if (id >= 200) return 95;  // thunderstorm
    return 3;
}

QString WeatherManager::settingsFilePath() {
    return weatherSettingsFilePath();
}

QString WeatherManager::iconPathForName(const QString& iconName, int size) {
    if (iconName.isEmpty() || size <= 0)
        return QString();

    // Use a versioned cache subdir so any previously-cached symbolic icons are
    // not served again after we switched to always-colour lookups.
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                              + QStringLiteral("/weather-icons-color");
    QDir().mkpath(cacheRoot);

    const QString cachePath = cacheRoot
                              + QStringLiteral("/")
                              + iconName
                              + QStringLiteral("-")
                              + QString::number(size)
                              + QStringLiteral(".png");

    QFileInfo cacheInfo(cachePath);
    if (cacheInfo.exists()) {
        if (cacheInfo.size() > 0)
            return QUrl::fromLocalFile(cachePath).toString();
        QFile::remove(cachePath);
    }

    // Prefer Papirus colour icons (48x48/status) before falling back to the
    // system theme engine, which often resolves to symbolic panel variants.
    QStringList roots;
    const QByteArray snap = qgetenv("SNAP");
    if (!snap.isEmpty())
        roots << (QString::fromLocal8Bit(snap) + QStringLiteral("/usr/share/icons/Papirus"));
    roots << QStringLiteral("/usr/share/icons/Papirus");

    // Only search colour-context directories; skip panel/symbolic ones.
    const QStringList colorDirs = {
        QStringLiteral("64x64/status"),
        QStringLiteral("48x48/status"),
        QStringLiteral("32x32/status"),
        QStringLiteral("24x24/status"),
        QStringLiteral("22x22/status"),
        QStringLiteral("16x16/status"),
        QStringLiteral("scalable/status"),
    };
    const QStringList exts = { QStringLiteral(".png"), QStringLiteral(".xpm"), QStringLiteral(".svg") };

    for (const QString& root : roots) {
        for (const QString& rel : colorDirs) {
            for (const QString& ext : exts) {
                const QString candidate = root + QStringLiteral("/") + rel
                                          + QStringLiteral("/") + iconName + ext;
                if (!QFileInfo::exists(candidate))
                    continue;
                if (ext == QStringLiteral(".svg")) {
                    const QString rendered = renderSvgToCachedPng(candidate, cachePath, size);
                    if (!rendered.isEmpty())
                        return rendered;
                    continue;
                }
                return QUrl::fromLocalFile(candidate).toString();
            }
        }

        // Last manual fallback: recursively search Papirus for a matching icon
        // while avoiding panel/symbolic variants.
        QDirIterator it(root,
                        QStringList() << (iconName + QStringLiteral(".png"))
                                      << (iconName + QStringLiteral(".xpm"))
                                      << (iconName + QStringLiteral(".svg")),
                        QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString candidate = it.next();
            if (candidate.contains(QStringLiteral("/panel/")))
                continue;
            if (candidate.contains(QStringLiteral("-symbolic")))
                continue;

            if (candidate.endsWith(QStringLiteral(".svg"))) {
                const QString rendered = renderSvgToCachedPng(candidate, cachePath, size);
                if (!rendered.isEmpty())
                    return rendered;
                continue;
            }
            return QUrl::fromLocalFile(candidate).toString();
        }
    }

    // Theme-engine last resort (Hicolor or whatever the desktop has configured).
    const QIcon icon = QIcon::fromTheme(iconName);
    if (!icon.isNull()) {
        const QPixmap pixmap = icon.pixmap(size, size);
        if (!pixmap.isNull() && pixmap.save(cachePath, "PNG"))
            return QUrl::fromLocalFile(cachePath).toString();
    }

    return QString();
}

QString WeatherManager::fmtTemp(double celsius) const {
    if (m_temperatureUnit == Fahrenheit)
        return QString::number(qRound(celsius * 9.0 / 5.0 + 32.0)) + QStringLiteral("\u00B0F");
    return QString::number(qRound(celsius)) + QStringLiteral("\u00B0C");
}

QString WeatherManager::fmtWind(double kmh) const {
    if (m_temperatureUnit == Fahrenheit)
        return QString::number(qRound(kmh * 0.621371)) + QStringLiteral(" mph");
    return QString::number(qRound(kmh)) + QStringLiteral(" km/h");
}

QString WeatherManager::tempUnitStr() const {
    return m_temperatureUnit == Fahrenheit ? QStringLiteral("\u00B0F") : QStringLiteral("\u00B0C");
}

// ── Constructor ───────────────────────────────────────────────────────────────

WeatherManager::WeatherManager(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    connect(&m_autoRefreshTimer, &QTimer::timeout, this, &WeatherManager::refreshWeather);
    loadSettings();
    // Fetch weather shortly after startup (avoids blocking app launch)
    if (configured())
        QTimer::singleShot(3000, this, &WeatherManager::refreshWeather);
}

// ── Property accessors ────────────────────────────────────────────────────────

int     WeatherManager::provider()              const { return m_provider; }
QString WeatherManager::locationQuery()         const { return m_locationQuery; }
QString WeatherManager::locationName()          const { return m_locationName; }
QString WeatherManager::apiKey()                const { return m_apiKey; }
int     WeatherManager::temperatureUnit()       const { return m_temperatureUnit; }
bool    WeatherManager::autoRefreshEnabled()    const { return m_autoRefreshEnabled; }
int     WeatherManager::refreshIntervalMinutes() const { return m_refreshIntervalMinutes; }
bool    WeatherManager::busy()                  const { return m_busy; }
bool    WeatherManager::locationDetecting()      const { return m_locationDetecting; }
QString WeatherManager::statusMessage()         const { return m_statusMessage; }
bool    WeatherManager::configured()            const { return !m_locationQuery.trimmed().isEmpty(); }
bool    WeatherManager::hasData()               const { return m_rawCurrent.valid; }
QVariantMap  WeatherManager::currentWeather()   const { return m_currentWeather; }
QVariantList WeatherManager::hourlyForecast()   const { return m_hourlyForecast; }
QVariantList WeatherManager::dailyForecast()    const { return m_dailyForecast; }
QVariantList WeatherManager::weatherAlerts()    const { return m_weatherAlerts; }

void WeatherManager::setProvider(int v) {
    if (m_provider == v) return;
    m_provider = v;
    m_hasCoordinates = false; // force re-geocode with new provider
    emit providerChanged();
}

void WeatherManager::setLocationQuery(const QString& v) {
    if (m_locationQuery == v) return;
    const bool wasConfigured = configured();
    m_locationQuery = v;
    emit locationQueryChanged();
    if (configured() != wasConfigured)
        emit configuredChanged();
}

void WeatherManager::setApiKey(const QString& v) {
    if (m_apiKey == v) return;
    m_apiKey = v;
    emit apiKeyChanged();
}

void WeatherManager::setTemperatureUnit(int v) {
    const int clamped = qBound(0, v, 1);
    if (m_temperatureUnit == clamped) return;
    m_temperatureUnit = clamped;
    emit temperatureUnitChanged();
    if (m_rawCurrent.valid)
        buildWeatherData(); // reformat without re-fetching
}

void WeatherManager::setAutoRefreshEnabled(bool v) {
    if (m_autoRefreshEnabled == v) return;
    m_autoRefreshEnabled = v;
    emit autoRefreshEnabledChanged();
    updateAutoRefreshTimer();
}

void WeatherManager::setRefreshIntervalMinutes(int v) {
    const int clamped = qBound(5, v, 180);
    if (m_refreshIntervalMinutes == clamped) return;
    m_refreshIntervalMinutes = clamped;
    emit refreshIntervalMinutesChanged();
    updateAutoRefreshTimer();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void WeatherManager::setBusy(bool v) {
    if (m_busy == v) return;
    m_busy = v;
    if (!m_busy)
        m_refreshStartedAtUtc = QDateTime();
    emit busyChanged();
}

void WeatherManager::setStatus(const QString& msg) {
    if (m_statusMessage == msg) return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void WeatherManager::updateAutoRefreshTimer() {
    if (m_autoRefreshEnabled && configured()) {
        m_autoRefreshTimer.start(m_refreshIntervalMinutes * 60 * 1000);
    } else {
        m_autoRefreshTimer.stop();
    }
}

// ── Settings persistence ──────────────────────────────────────────────────────

void WeatherManager::saveSettings() {
    QDir().mkpath(QFileInfo(settingsFilePath()).absolutePath());
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Weather"));
    s.setValue(QStringLiteral("Provider"),               m_provider);
    s.setValue(QStringLiteral("LocationQuery"),          m_locationQuery);
    s.setValue(QStringLiteral("LocationName"),           m_locationName);
    s.setValue(QStringLiteral("Latitude"),               m_latitude);
    s.setValue(QStringLiteral("Longitude"),              m_longitude);
    s.setValue(QStringLiteral("HasCoordinates"),         m_hasCoordinates);
    s.setValue(QStringLiteral("CachedLocationQuery"),    m_cachedLocationQuery);
    s.setValue(QStringLiteral("ApiKey"),                 m_apiKey);
    s.setValue(QStringLiteral("TemperatureUnit"),        m_temperatureUnit);
    s.setValue(QStringLiteral("AutoRefreshEnabled"),     m_autoRefreshEnabled);
    s.setValue(QStringLiteral("RefreshIntervalMinutes"), m_refreshIntervalMinutes);
    s.endGroup();
    s.sync();
}

void WeatherManager::loadSettings() {
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Weather"));
    m_provider               = s.value(QStringLiteral("Provider"), OpenMeteo).toInt();
    m_locationQuery          = s.value(QStringLiteral("LocationQuery")).toString();
    m_locationName           = s.value(QStringLiteral("LocationName")).toString();
    m_latitude               = s.value(QStringLiteral("Latitude"),  0.0).toDouble();
    m_longitude              = s.value(QStringLiteral("Longitude"), 0.0).toDouble();
    m_hasCoordinates         = s.value(QStringLiteral("HasCoordinates"), false).toBool();
    m_cachedLocationQuery    = s.value(QStringLiteral("CachedLocationQuery")).toString();
    m_apiKey                 = s.value(QStringLiteral("ApiKey")).toString();
    m_temperatureUnit        = s.value(QStringLiteral("TemperatureUnit"), Celsius).toInt();
    m_autoRefreshEnabled     = s.value(QStringLiteral("AutoRefreshEnabled"), true).toBool();
    m_refreshIntervalMinutes = s.value(QStringLiteral("RefreshIntervalMinutes"), 30).toInt();
    s.endGroup();
    updateAutoRefreshTimer();
}

// ── Public refresh entry point ────────────────────────────────────────────────

void WeatherManager::refreshWeather() {
    if (m_locationQuery.trimmed().isEmpty()) {
        setStatus(QStringLiteral("Configure a location in Settings."));
        return;
    }

    if (m_busy) {
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const bool refreshTimedOut = !m_refreshStartedAtUtc.isValid()
                                     || m_refreshStartedAtUtc.secsTo(nowUtc) >= 120;
        if (!refreshTimedOut)
            return;

        setBusy(false);
        setStatus(QStringLiteral("Previous weather refresh timed out. Retrying."));
    }

    setBusy(true);
    m_refreshStartedAtUtc = QDateTime::currentDateTimeUtc();
    setStatus(QStringLiteral("Updating…"));

    // Check if location looks like "lat,lon"
    static const QRegularExpression coordRe(
        QStringLiteral(R"(^\s*(-?\d+\.?\d*)\s*,\s*(-?\d+\.?\d*)\s*$)"));
    const auto m = coordRe.match(m_locationQuery);
    if (m.hasMatch()) {
        m_latitude       = m.captured(1).toDouble();
        m_longitude      = m.captured(2).toDouble();
        m_hasCoordinates = true;
        m_cachedLocationQuery = m_locationQuery;
        if (m_locationName.isEmpty())
            m_locationName = m_locationQuery;
        doFetch();
        return;
    }

    // City name: use cached coordinates if query hasn't changed
    if (m_hasCoordinates && m_cachedLocationQuery == m_locationQuery) {
        doFetch();
        return;
    }

    // Need to geocode
    if (m_provider == OpenWeatherMap && !m_apiKey.isEmpty())
        geocodeOWM();
    else
        geocodeOpenMeteo();
}

void WeatherManager::refreshWeatherIfDue() {
    if (!configured() || !m_autoRefreshEnabled)
        return;

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!m_lastSuccessfulRefreshUtc.isValid()) {
        refreshWeather();
        return;
    }

    const int intervalSeconds = qMax(5, m_refreshIntervalMinutes) * 60;
    if (m_lastSuccessfulRefreshUtc.secsTo(nowUtc) >= intervalSeconds)
        refreshWeather();
}

void WeatherManager::doFetch() {
    if (m_provider == OpenWeatherMap && !m_apiKey.isEmpty())
        fetchOWMCurrent();
    else if (m_provider == NOAA)
        fetchNOAA();
    else if (m_provider == MetNorway)
        fetchMetNorway();
    else
        fetchOpenMeteo();
}

// ── Open-Meteo geocoding ──────────────────────────────────────────────────────

void WeatherManager::geocodeOpenMeteo() {
    QUrl url(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"),     m_locationQuery.trimmed());
    q.addQueryItem(QStringLiteral("count"),    QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("language"), QStringLiteral("en"));
    q.addQueryItem(QStringLiteral("format"),   QStringLiteral("json"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CalDisplay/1.0 (github.com/caldisplay)"));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false);
            setStatus(QStringLiteral("Geocode error: ") + reply->errorString());
            return;
        }
        parseOpenMeteoGeocode(reply->readAll());
    });
}

void WeatherManager::parseOpenMeteoGeocode(const QByteArray& data) {
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    const QJsonArray results = root.value(QStringLiteral("results")).toArray();
    if (results.isEmpty()) {
        setBusy(false);
        setStatus(QStringLiteral("Location not found: ") + m_locationQuery);
        return;
    }
    const QJsonObject first = results.first().toObject();
    m_latitude  = jd(first, QStringLiteral("latitude"));
    m_longitude = jd(first, QStringLiteral("longitude"));
    m_hasCoordinates      = true;
    m_cachedLocationQuery = m_locationQuery;

    // Build display name: "City, Country"
    const QString city    = js(first, QStringLiteral("name"));
    const QString country = js(first, QStringLiteral("country"));
    m_locationName = country.isEmpty() ? city : city + QStringLiteral(", ") + country;
    emit locationNameChanged();

    doFetch();
}

// ── Open-Meteo weather ────────────────────────────────────────────────────────

void WeatherManager::fetchOpenMeteo() {
    QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"),         QString::number(m_latitude));
    q.addQueryItem(QStringLiteral("longitude"),        QString::number(m_longitude));
    q.addQueryItem(QStringLiteral("current"),
        QStringLiteral("temperature_2m,relative_humidity_2m,apparent_temperature,"
                       "weather_code,wind_speed_10m,wind_direction_10m"));
    q.addQueryItem(QStringLiteral("hourly"),
        QStringLiteral("temperature_2m,weather_code,precipitation_probability"));
    q.addQueryItem(QStringLiteral("daily"),
        QStringLiteral("weather_code,temperature_2m_max,temperature_2m_min,"
                       "sunrise,sunset,precipitation_probability_max"));
    q.addQueryItem(QStringLiteral("timezone"),         QStringLiteral("auto"));
    q.addQueryItem(QStringLiteral("forecast_days"),    QStringLiteral("16"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CalDisplay/1.0 (github.com/caldisplay)"));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false);
            setStatus(QStringLiteral("Weather fetch error: ") + reply->errorString());
            return;
        }
        parseOpenMeteoWeather(reply->readAll());
    });
}

void WeatherManager::parseOpenMeteoWeather(const QByteArray& data) {
    const QJsonObject root = QJsonDocument::fromJson(data).object();    m_weatherAlerts.clear();
    // ── Current conditions ────────────────────────────────────────────────────
    const QJsonObject cur = root.value(QStringLiteral("current")).toObject();
    m_rawCurrent.valid     = true;
    m_rawCurrent.temp      = jd(cur, QStringLiteral("temperature_2m"));
    m_rawCurrent.feelsLike = jd(cur, QStringLiteral("apparent_temperature"));
    m_rawCurrent.humidity  = ji(cur, QStringLiteral("relative_humidity_2m"));
    m_rawCurrent.windSpeed = jd(cur, QStringLiteral("wind_speed_10m"));
    m_rawCurrent.windDir   = ji(cur, QStringLiteral("wind_direction_10m"));
    m_rawCurrent.code      = ji(cur, QStringLiteral("weather_code"));

    // ── Hourly (next 12 hours starting from current hour) ─────────────────────
    const QJsonObject hourly = root.value(QStringLiteral("hourly")).toObject();
    const QJsonArray  hTimes  = hourly.value(QStringLiteral("time")).toArray();
    const QJsonArray  hTemps  = hourly.value(QStringLiteral("temperature_2m")).toArray();
    const QJsonArray  hCodes  = hourly.value(QStringLiteral("weather_code")).toArray();
    const QJsonArray  hPrec   = hourly.value(QStringLiteral("precipitation_probability")).toArray();

    m_rawHourly.clear();
    const int nowHour = QTime::currentTime().hour();
    const QDate today = QDate::currentDate();
    int count = 0;
    for (int i = 0; i < hTimes.size() && count < 12; ++i) {
        // Parse time string "2026-05-18T14:00"
        const QString ts = hTimes.at(i).toString();
        if (ts.size() < 16) continue;
        const QDate   d = QDate::fromString(ts.left(10), QStringLiteral("yyyy-MM-dd"));
        const int     h = ts.mid(11, 2).toInt();
        // Only upcoming hours: today from nowHour, or tomorrow if today is exhausted
        if (d == today && h < nowHour) continue;
        if (d > today.addDays(1)) break;

        RawHourly entry;
        entry.hour       = h;
        entry.temp       = hTemps.size() > i ? hTemps.at(i).toDouble() : 0.0;
        entry.code       = hCodes.size() > i ? static_cast<int>(hCodes.at(i).toDouble()) : 0;
        entry.precipProb = hPrec.size()  > i ? static_cast<int>(hPrec.at(i).toDouble()) : 0;
        m_rawHourly.append(entry);
        ++count;
    }

    // ── Daily ─────────────────────────────────────────────────────────────────
    const QJsonObject daily  = root.value(QStringLiteral("daily")).toObject();
    const QJsonArray  dTimes = daily.value(QStringLiteral("time")).toArray();
    const QJsonArray  dCodes = daily.value(QStringLiteral("weather_code")).toArray();
    const QJsonArray  dMax   = daily.value(QStringLiteral("temperature_2m_max")).toArray();
    const QJsonArray  dMin   = daily.value(QStringLiteral("temperature_2m_min")).toArray();
    const QJsonArray  dRise  = daily.value(QStringLiteral("sunrise")).toArray();
    const QJsonArray  dSet   = daily.value(QStringLiteral("sunset")).toArray();
    const QJsonArray  dPrec  = daily.value(QStringLiteral("precipitation_probability_max")).toArray();

    m_rawDaily.clear();
    for (int i = 0; i < dTimes.size(); ++i) {
        const QDate d = QDate::fromString(dTimes.at(i).toString(), QStringLiteral("yyyy-MM-dd"));
        if (!d.isValid()) continue;

        RawDaily entry;
        entry.date      = d;
        entry.tempMax   = dMax.size()  > i ? dMax.at(i).toDouble()  : 0.0;
        entry.tempMin   = dMin.size()  > i ? dMin.at(i).toDouble()  : 0.0;
        entry.code      = dCodes.size() > i ? static_cast<int>(dCodes.at(i).toDouble()) : 0;
        entry.precipProb = dPrec.size() > i ? static_cast<int>(dPrec.at(i).toDouble()) : 0;

        // Sunrise/sunset come as "2026-05-18T05:25" – keep just time portion
        const QString riseStr = dRise.size() > i ? dRise.at(i).toString() : QString();
        const QString  setStr = dSet.size()  > i ? dSet.at(i).toString()  : QString();
        entry.sunrise = riseStr.size() >= 16 ? riseStr.mid(11, 5) : QString();
        entry.sunset  =  setStr.size() >= 16 ?  setStr.mid(11, 5) : QString();

        m_rawDaily.append(entry);
    }

    buildWeatherData();
    setBusy(false);
}

// ── OpenWeatherMap geocoding ──────────────────────────────────────────────────

void WeatherManager::geocodeOWM() {
    QUrl url(QStringLiteral("http://api.openweathermap.org/geo/1.0/direct"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"),     m_locationQuery.trimmed());
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("appid"), m_apiKey);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CalDisplay/1.0"));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false);
            setStatus(QStringLiteral("Geocode error: ") + reply->errorString());
            return;
        }
        parseOWMGeocode(reply->readAll());
    });
}

void WeatherManager::parseOWMGeocode(const QByteArray& data) {
    const QJsonArray arr = QJsonDocument::fromJson(data).array();
    if (arr.isEmpty()) {
        setBusy(false);
        setStatus(QStringLiteral("Location not found: ") + m_locationQuery);
        return;
    }
    const QJsonObject first = arr.first().toObject();
    m_latitude  = jd(first, QStringLiteral("lat"));
    m_longitude = jd(first, QStringLiteral("lon"));
    m_hasCoordinates      = true;
    m_cachedLocationQuery = m_locationQuery;

    const QString city    = js(first, QStringLiteral("name"));
    const QString country = js(first, QStringLiteral("country"));
    m_locationName = country.isEmpty() ? city : city + QStringLiteral(", ") + country;
    emit locationNameChanged();

    fetchOWMCurrent();
}

// ── OpenWeatherMap current weather ────────────────────────────────────────────

void WeatherManager::fetchOWMCurrent() {
    QUrl url(QStringLiteral("https://api.openweathermap.org/data/2.5/weather"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("lat"),   QString::number(m_latitude));
    q.addQueryItem(QStringLiteral("lon"),   QString::number(m_longitude));
    q.addQueryItem(QStringLiteral("appid"), m_apiKey);
    q.addQueryItem(QStringLiteral("units"), QStringLiteral("metric"));
    url.setQuery(q);

    QNetworkRequest req(url);
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false);
            setStatus(QStringLiteral("Weather error: ") + reply->errorString());
            return;
        }
        parseOWMCurrent(reply->readAll());
    });
}

void WeatherManager::parseOWMCurrent(const QByteArray& data) {
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    m_weatherAlerts.clear();
    const QJsonObject main = root.value(QStringLiteral("main")).toObject();
    const QJsonObject wind = root.value(QStringLiteral("wind")).toObject();
    const QJsonArray  wx   = root.value(QStringLiteral("weather")).toArray();
    const int owmId = wx.isEmpty() ? 800 : ji(wx.first().toObject(), QStringLiteral("id"), 800);

    m_rawCurrent.valid     = true;
    m_rawCurrent.temp      = jd(main, QStringLiteral("temp"));
    m_rawCurrent.feelsLike = jd(main, QStringLiteral("feels_like"));
    m_rawCurrent.humidity  = ji(main, QStringLiteral("humidity"));
    m_rawCurrent.windSpeed = jd(wind, QStringLiteral("speed")) * 3.6; // m/s → km/h
    m_rawCurrent.windDir   = ji(wind, QStringLiteral("deg"));
    m_rawCurrent.code      = owmIdToWmo(owmId);

    // Location name from OWM response (city name)
    const QString owmCity = js(root, QStringLiteral("name"));
    if (!owmCity.isEmpty() && m_locationName.isEmpty())
        m_locationName = owmCity;

    fetchOWMForecast();
}

// ── OpenWeatherMap 5-day/3-hour forecast ─────────────────────────────────────

void WeatherManager::fetchOWMForecast() {
    QUrl url(QStringLiteral("https://api.openweathermap.org/data/2.5/forecast"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("lat"),   QString::number(m_latitude));
    q.addQueryItem(QStringLiteral("lon"),   QString::number(m_longitude));
    q.addQueryItem(QStringLiteral("appid"), m_apiKey);
    q.addQueryItem(QStringLiteral("units"), QStringLiteral("metric"));
    q.addQueryItem(QStringLiteral("cnt"),   QStringLiteral("40")); // 5 days × 8
    url.setQuery(q);

    QNetworkRequest req(url);
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Current data already fetched – show partial data
            buildWeatherData();
            setBusy(false);
            setStatus(QStringLiteral("Forecast error: ") + reply->errorString());
            return;
        }
        parseOWMForecast(reply->readAll());
    });
}

void WeatherManager::parseOWMForecast(const QByteArray& data) {
    const QJsonArray list = QJsonDocument::fromJson(data).object()
                                .value(QStringLiteral("list")).toArray();

    // ── Hourly: today's remaining entries ────────────────────────────────────
    m_rawHourly.clear();
    const QDate today    = QDate::currentDate();
    const int   nowHour  = QTime::currentTime().hour();
    int hourCount = 0;

    for (const QJsonValue& v : list) {
        if (hourCount >= 8) break;
        const QJsonObject entry = v.toObject();
        // dt_txt: "2026-05-18 15:00:00"
        const QString dtTxt = js(entry, QStringLiteral("dt_txt"));
        if (dtTxt.size() < 16) continue;
        const QDate d = QDate::fromString(dtTxt.left(10), QStringLiteral("yyyy-MM-dd"));
        const int   h = dtTxt.mid(11, 2).toInt();
        if (d != today) continue;
        if (h < nowHour) continue;

        const QJsonObject main = entry.value(QStringLiteral("main")).toObject();
        const QJsonArray  wx   = entry.value(QStringLiteral("weather")).toArray();
        const int owmId = wx.isEmpty() ? 800 : ji(wx.first().toObject(), QStringLiteral("id"), 800);

        RawHourly hourly;
        hourly.hour       = h;
        hourly.temp       = jd(main, QStringLiteral("temp"));
        hourly.code       = owmIdToWmo(owmId);
        hourly.precipProb = static_cast<int>(entry.value(QStringLiteral("pop")).toDouble() * 100);
        m_rawHourly.append(hourly);
        ++hourCount;
    }

    // ── Daily: aggregate 3-hourly data by date ───────────────────────────────
    struct DayAccum {
        double maxTemp = -999, minTemp = 999;
        int    codeFreq[100] = {};   // tally WMO codes (mapped) – simplified
        int    mostCommonCode = 0;
        int    maxPrecipProb  = 0;
        int    count = 0;
    };
    QMap<QDate, DayAccum> accum;

    for (const QJsonValue& v : list) {
        const QJsonObject entry = v.toObject();
        const QString dtTxt = js(entry, QStringLiteral("dt_txt"));
        if (dtTxt.size() < 10) continue;
        const QDate d = QDate::fromString(dtTxt.left(10), QStringLiteral("yyyy-MM-dd"));
        const QJsonObject main = entry.value(QStringLiteral("main")).toObject();
        const QJsonArray  wx   = entry.value(QStringLiteral("weather")).toArray();
        const int owmId = wx.isEmpty() ? 800 : ji(wx.first().toObject(), QStringLiteral("id"), 800);
        const int wmo = owmIdToWmo(owmId);
        const int pp  = static_cast<int>(entry.value(QStringLiteral("pop")).toDouble() * 100);

        DayAccum& da = accum[d];
        da.maxTemp = qMax(da.maxTemp, jd(main, QStringLiteral("temp_max")));
        da.minTemp = qMin(da.minTemp, jd(main, QStringLiteral("temp_min")));
        // Tally WMO code severity (higher = worse; pick highest for the day)
        if (wmo > da.mostCommonCode) da.mostCommonCode = wmo;
        da.maxPrecipProb = qMax(da.maxPrecipProb, pp);
        ++da.count;
    }

    m_rawDaily.clear();
    for (auto it = accum.begin(); it != accum.end(); ++it) {
        const DayAccum& da = it.value();
        RawDaily rd;
        rd.date      = it.key();
        rd.tempMax   = da.maxTemp > -998 ? da.maxTemp : 0.0;
        rd.tempMin   = da.minTemp <  998 ? da.minTemp : 0.0;
        rd.code      = da.mostCommonCode;
        rd.precipProb = da.maxPrecipProb;
        // OWM free tier doesn't provide sunrise/sunset in forecast; leave blank
        m_rawDaily.append(rd);
    }

    buildWeatherData();
    setBusy(false);
}

// ── Build formatted QVariant output ──────────────────────────────────────────

void WeatherManager::buildWeatherData() {
    // Current conditions
    QVariantMap cur;
    if (m_rawCurrent.valid) {
        cur[QStringLiteral("icon")]         = wmoIcon(m_rawCurrent.code);
        cur[QStringLiteral("iconPath")]     = iconPathForName(wmoIcon(m_rawCurrent.code), 64);
        cur[QStringLiteral("description")]  = wmoDescription(m_rawCurrent.code);
        cur[QStringLiteral("tempStr")]      = fmtTemp(m_rawCurrent.temp);
        cur[QStringLiteral("feelsLikeStr")] = fmtTemp(m_rawCurrent.feelsLike);
        cur[QStringLiteral("humidity")]     = m_rawCurrent.humidity;
        cur[QStringLiteral("windStr")]      = fmtWind(m_rawCurrent.windSpeed)
                                              + QStringLiteral(" ")
                                              + windDirText(m_rawCurrent.windDir);
        cur[QStringLiteral("code")]         = m_rawCurrent.code;
    }
    m_currentWeather = cur;

    // Hourly
    QVariantList hourly;
    for (const RawHourly& h : m_rawHourly) {
        QVariantMap item;
        const QTime t(h.hour, 0);
        item[QStringLiteral("timeText")]  = t.toString(QStringLiteral("HH:00"));
        item[QStringLiteral("icon")]      = wmoIcon(h.code);
        item[QStringLiteral("iconPath")]  = iconPathForName(wmoIcon(h.code), 32);
        item[QStringLiteral("tempStr")]   = fmtTemp(h.temp);
        item[QStringLiteral("precipProb")] = h.precipProb;
        {
            QString pt = precipTypeFromCode(h.code);
            if (pt.isEmpty() && h.precipProb > 0)
                pt = QStringLiteral("Rain");
            item[QStringLiteral("precipType")] = pt;
        }
        item[QStringLiteral("code")]      = h.code;
        hourly.append(item);
    }
    m_hourlyForecast = hourly;

    // Daily
    const QDate today = QDate::currentDate();
    QVariantList daily;
    for (const RawDaily& d : m_rawDaily) {
        QVariantMap item;
        QString dayName;
        if (d.date == today)
            dayName = QStringLiteral("Today");
        else if (d.date == today.addDays(1))
            dayName = QStringLiteral("Tomorrow");
        else
            dayName = d.date.toString(QStringLiteral("ddd"));

        item[QStringLiteral("dayName")]    = dayName;
        item[QStringLiteral("dateStr")]    = d.date.toString(QStringLiteral("MMM d"));
        item[QStringLiteral("icon")]       = wmoIcon(d.code);
        item[QStringLiteral("iconPath")]   = iconPathForName(wmoIcon(d.code), 32);
        item[QStringLiteral("description")] = wmoDescription(d.code);
        item[QStringLiteral("tempMaxStr")] = fmtTemp(d.tempMax);
        item[QStringLiteral("tempMinStr")] = fmtTemp(d.tempMin);
        item[QStringLiteral("precipProb")] = d.precipProb;
        {
            QString pt = precipTypeFromCode(d.code);
            if (pt.isEmpty() && d.precipProb > 0)
                pt = QStringLiteral("Rain");
            item[QStringLiteral("precipType")] = pt;
        }
        item[QStringLiteral("sunrise")]    = d.sunrise;
        item[QStringLiteral("sunset")]     = d.sunset;
        item[QStringLiteral("code")]       = d.code;
        daily.append(item);
    }
    m_dailyForecast = daily;

    const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    m_lastSuccessfulRefreshUtc = QDateTime::currentDateTimeUtc();
    setStatus(QStringLiteral("Updated ") + ts);
    emit weatherUpdated();
}

// ── NOAA (National Weather Service) ───────────────────────────────────────────

int WeatherManager::noaaForecastTextToWmo(const QString& text) {
    const QString t = text.toLower();
    if (t.contains(QLatin1String("thunder")))                                      return 95;
    if (t.contains(QLatin1String("blizzard")) ||
        (t.contains(QLatin1String("heavy")) && t.contains(QLatin1String("snow")))) return 75;
    if (t.contains(QLatin1String("snow shower")) ||
        t.contains(QLatin1String("snow showers")))                                 return 85;
    if (t.contains(QLatin1String("snow")))                                         return 71;
    if (t.contains(QLatin1String("freezing rain")) ||
        t.contains(QLatin1String("ice")))                                          return 67;
    if (t.contains(QLatin1String("sleet")) ||
        t.contains(QLatin1String("wintry mix")))                                   return 77;
    if (t.contains(QLatin1String("heavy")) && t.contains(QLatin1String("rain")))   return 65;
    if (t.contains(QLatin1String("shower")) ||
        t.contains(QLatin1String("showers")))                                      return 80;
    if (t.contains(QLatin1String("drizzle")))                                      return 51;
    if (t.contains(QLatin1String("rain")))                                         return 63;
    if (t.contains(QLatin1String("fog")) || t.contains(QLatin1String("mist")) ||
        t.contains(QLatin1String("haze")))                                         return 45;
    if (t.contains(QLatin1String("overcast")) ||
        t.contains(QLatin1String("mostly cloudy")))                                return 3;
    if (t.contains(QLatin1String("partly")))                                       return 2;
    if (t.contains(QLatin1String("cloudy")))                                       return 3;
    return 0;
}

int WeatherManager::cardinalToDegrees(const QString& dir) {
    static const struct { const char* d; int deg; } table[] = {
        {"N",0},{"NNE",22},{"NE",45},{"ENE",67},{"E",90},{"ESE",112},
        {"SE",135},{"SSE",157},{"S",180},{"SSW",202},{"SW",225},{"WSW",247},
        {"W",270},{"WNW",292},{"NW",315},{"NNW",337}
    };
    for (const auto& t : table)
        if (dir == QLatin1String(t.d)) return t.deg;
    return 0;
}

void WeatherManager::fetchNOAA() {
    if (!m_hasCoordinates) {
        geocodeOpenMeteo();
        return;
    }
    const QString urlStr = QStringLiteral("https://api.weather.gov/points/%1,%2")
        .arg(m_latitude,  0, 'f', 4)
        .arg(m_longitude, 0, 'f', 4);
    QUrl noaaPointsUrl(urlStr);
    QNetworkRequest req(noaaPointsUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CalDisplay/1.0 (github.com/caldisplay)"));
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/geo+json"));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false);
            setStatus(QStringLiteral("NOAA error (US locations only): ") + reply->errorString());
            return;
        }
        parseNOAAPoints(reply->readAll());
    });
}

void WeatherManager::parseNOAAPoints(const QByteArray& data) {
    const QJsonObject root  = QJsonDocument::fromJson(data).object();
    const QJsonObject props = root.value(QStringLiteral("properties")).toObject();
    m_noaaForecastUrl       = js(props, QStringLiteral("forecast"));
    m_noaaForecastHourlyUrl = js(props, QStringLiteral("forecastHourly"));
    m_weatherAlerts.clear();
    if (m_noaaForecastUrl.isEmpty() || m_noaaForecastHourlyUrl.isEmpty()) {
        setBusy(false);
        setStatus(QStringLiteral("NOAA: no forecast URLs returned (US locations only)"));
        return;
    }
    // Update location name from NOAA relativeLocation
    const QJsonObject relLoc = props.value(QStringLiteral("relativeLocation"))
                                    .toObject()
                                    .value(QStringLiteral("properties"))
                                    .toObject();
    const QString city  = js(relLoc, QStringLiteral("city"));
    const QString state = js(relLoc, QStringLiteral("state"));
    if (!city.isEmpty()) {
        m_locationName = state.isEmpty() ? city : city + QStringLiteral(", ") + state;
        emit locationNameChanged();
    }
    fetchNOAAHourly();
}

void WeatherManager::fetchNOAAHourly() {
    QUrl hourlyUrl(m_noaaForecastHourlyUrl);
    QNetworkRequest req(hourlyUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CalDisplay/1.0 (github.com/caldisplay)"));
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/geo+json"));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false);
            setStatus(QStringLiteral("NOAA hourly error: ") + reply->errorString());
            return;
        }
        parseNOAAHourly(reply->readAll());
    });
}

void WeatherManager::parseNOAAHourly(const QByteArray& data) {
    const QJsonArray periods = QJsonDocument::fromJson(data).object()
                                   .value(QStringLiteral("properties")).toObject()
                                   .value(QStringLiteral("periods")).toArray();
    m_rawHourly.clear();
    m_rawCurrent.valid = false;
    const QDateTime now   = QDateTime::currentDateTime();
    const QDate     today = now.date();
    int count = 0;
    static const QRegularExpression windNumRe(QStringLiteral(R"((\d+))"));

    for (const QJsonValue& v : periods) {
        const QJsonObject p   = v.toObject();
        const QDateTime   dt  = QDateTime::fromString(
                                    js(p, QStringLiteral("startTime")),
                                    Qt::ISODate).toLocalTime();
        if (!dt.isValid()) continue;

        const double tempF    = p.value(QStringLiteral("temperature")).toDouble();
        const double tempC    = (tempF - 32.0) * 5.0 / 9.0;
        const QJsonObject precipObj = p.value(QStringLiteral("probabilityOfPrecipitation")).toObject();
        const int precipProb  = precipObj.value(QStringLiteral("value")).isNull()
                                ? 0 : static_cast<int>(precipObj.value(QStringLiteral("value")).toDouble());
        const int wmoCode     = noaaForecastTextToWmo(js(p, QStringLiteral("shortForecast")));

        // First period → current conditions
        if (!m_rawCurrent.valid) {
            m_rawCurrent.valid     = true;
            m_rawCurrent.temp      = tempC;
            m_rawCurrent.feelsLike = tempC;
            m_rawCurrent.humidity  = 0;
            m_rawCurrent.code      = wmoCode;
            const QString windStr  = js(p, QStringLiteral("windSpeed"));
            const auto    wm       = windNumRe.match(windStr);
            m_rawCurrent.windSpeed = wm.hasMatch() ? wm.captured(1).toDouble() * 1.60934 : 0.0;
            m_rawCurrent.windDir   = cardinalToDegrees(js(p, QStringLiteral("windDirection")));
        }

        // Collect next 12 upcoming hours
        if (dt < now) continue;
        if (dt.date() > today.addDays(1)) break;
        if (count >= 12) break;

        RawHourly entry;
        entry.hour       = dt.time().hour();
        entry.temp       = tempC;
        entry.code       = wmoCode;
        entry.precipProb = precipProb;
        m_rawHourly.append(entry);
        ++count;
    }
    fetchNOAADaily();
}

void WeatherManager::fetchNOAADaily() {
    QUrl dailyUrl(m_noaaForecastUrl);
    QNetworkRequest req(dailyUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CalDisplay/1.0 (github.com/caldisplay)"));
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/geo+json"));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            buildWeatherData();
            setBusy(false);
            setStatus(QStringLiteral("NOAA daily error: ") + reply->errorString());
            return;
        }
        parseNOAADaily(reply->readAll());
    });
}

void WeatherManager::parseNOAADaily(const QByteArray& data) {
    const QJsonArray periods = QJsonDocument::fromJson(data).object()
                                   .value(QStringLiteral("properties")).toObject()
                                   .value(QStringLiteral("periods")).toArray();
    struct DayData {
        double tempMax = -9999, tempMin = 9999;
        int    code = 0, precipProb = 0;
    };
    QMap<QDate, DayData> days;

    for (const QJsonValue& v : periods) {
        const QJsonObject p      = v.toObject();
        const QDateTime   dt     = QDateTime::fromString(
                                       js(p, QStringLiteral("startTime")),
                                       Qt::ISODate).toLocalTime();
        if (!dt.isValid()) continue;
        const double tempF    = p.value(QStringLiteral("temperature")).toDouble();
        const double tempC    = (tempF - 32.0) * 5.0 / 9.0;
        const QJsonObject precipObj = p.value(QStringLiteral("probabilityOfPrecipitation")).toObject();
        const int precipProb  = precipObj.value(QStringLiteral("value")).isNull()
                                ? 0 : static_cast<int>(precipObj.value(QStringLiteral("value")).toDouble());
        const int  wmoCode    = noaaForecastTextToWmo(js(p, QStringLiteral("shortForecast")));
        const bool isDaytime  = p.value(QStringLiteral("isDaytime")).toBool(true);

        DayData& dd = days[dt.date()];
        if (isDaytime) {
            dd.tempMax = qMax(dd.tempMax, tempC);
            dd.code    = wmoCode;
        } else {
            dd.tempMin = qMin(dd.tempMin, tempC);
            // Upgrade the day's code when night carries a precipitation event
            // that the daytime period missed (e.g. "Overcast" day + "Slight Chance
            // Rain" night).  Only upgrade – never downgrade a rain day to cloudy.
            if (wmoCode >= 51 && dd.code < 51)
                dd.code = wmoCode;
        }
        dd.precipProb = qMax(dd.precipProb, precipProb);
    }

    m_rawDaily.clear();
    for (auto it = days.begin(); it != days.end(); ++it) {
        const DayData& dd = it.value();
        RawDaily rd;
        rd.date       = it.key();
        rd.tempMax    = dd.tempMax > -9998 ? dd.tempMax : 0.0;
        rd.tempMin    = dd.tempMin <  9998 ? dd.tempMin : 0.0;
        rd.code       = dd.code;
        rd.precipProb = dd.precipProb;
        m_rawDaily.append(rd);
    }
    fetchNOAAAlerts();
}

void WeatherManager::fetchNOAAAlerts() {
    const QString urlStr = QStringLiteral("https://api.weather.gov/alerts/active?point=%1,%2")
        .arg(m_latitude,  0, 'f', 4)
        .arg(m_longitude, 0, 'f', 4);
    QUrl alertsUrl(urlStr);
    QNetworkRequest req(alertsUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CalDisplay/1.0 (github.com/caldisplay)"));
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/geo+json"));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            parseNOAAAlerts(reply->readAll());
        buildWeatherData();
        setBusy(false);
    });
}

void WeatherManager::parseNOAAAlerts(const QByteArray& data) {
    const QJsonArray features = QJsonDocument::fromJson(data).object()
                                    .value(QStringLiteral("features")).toArray();
    m_weatherAlerts.clear();
    const QDateTime now = QDateTime::currentDateTime();
    for (const QJsonValue& f : features) {
        const QJsonObject props = f.toObject()
                                   .value(QStringLiteral("properties")).toObject();
        const QString expiresStr = js(props, QStringLiteral("expires"));
        if (!expiresStr.isEmpty()) {
            const QDateTime expires = QDateTime::fromString(expiresStr, Qt::ISODate);
            if (expires.isValid() && expires < now) continue;
        }
        QVariantMap alert;
        const QString severity = js(props, QStringLiteral("severity"));
        QString iconPath = iconPathForName(
            severity.compare(QStringLiteral("Extreme"), Qt::CaseInsensitive) == 0
                ? QStringLiteral("dialog-error")
                : QStringLiteral("dialog-warning"),
            24);
        if (iconPath.isEmpty())
            iconPath = iconPathForName(QStringLiteral("weather-storm"), 24);

        alert[QStringLiteral("event")]       = js(props, QStringLiteral("event"));
        alert[QStringLiteral("headline")]    = js(props, QStringLiteral("headline"));
        alert[QStringLiteral("description")] = js(props, QStringLiteral("description"));
        alert[QStringLiteral("severity")]    = severity;
        alert[QStringLiteral("expires")]     = expiresStr;
        alert[QStringLiteral("iconPath")]    = iconPath;
        m_weatherAlerts.append(alert);
    }
}

// ── MET Norway (api.met.no) ────────────────────────────────────────────────────

int WeatherManager::metNoSymbolToWmo(const QString& symbol) {
    const QString s = symbol.toLower();
    if (s.contains(QLatin1String("thunder")))                                    return 95;
    if (s.startsWith(QLatin1String("fog")))                                      return 45;
    if (s.contains(QLatin1String("sleet")))                                      return 77;
    if (s.contains(QLatin1String("heavysnow")))
        return s.contains(QLatin1String("shower")) ? 86 : 75;
    if (s.contains(QLatin1String("lightsnow")))
        return s.contains(QLatin1String("shower")) ? 85 : 71;
    if (s.contains(QLatin1String("snow")))
        return s.contains(QLatin1String("shower")) ? 85 : 73;
    if (s.contains(QLatin1String("heavyrain")))
        return s.contains(QLatin1String("shower")) ? 82 : 65;
    if (s.contains(QLatin1String("lightrain")))
        return s.contains(QLatin1String("shower")) ? 80 : 61;
    if (s.contains(QLatin1String("rain")))
        return s.contains(QLatin1String("shower")) ? 80 : 63;
    if (s.contains(QLatin1String("heavydrizzle")))                               return 55;
    if (s.contains(QLatin1String("drizzle")))                                    return 51;
    if (s.startsWith(QLatin1String("cloudy")))                                   return 3;
    if (s.startsWith(QLatin1String("partlycloudy")))                             return 2;
    if (s.startsWith(QLatin1String("fair")))                                     return 1;
    return 0; // clearsky or unknown
}

void WeatherManager::fetchMetNorway() {
    if (!m_hasCoordinates) {
        geocodeOpenMeteo();
        return;
    }
    QUrl url(QStringLiteral("https://api.met.no/weatherapi/locationforecast/2.0/compact"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("lat"), QString::number(m_latitude,  'f', 4));
    q.addQueryItem(QStringLiteral("lon"), QString::number(m_longitude, 'f', 4));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CalDisplay/1.0 github.com/caldisplay"));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setBusy(false);
            setStatus(QStringLiteral("MET Norway error: ") + reply->errorString());
            return;
        }
        parseMetNorway(reply->readAll());
    });
}

void WeatherManager::parseMetNorway(const QByteArray& data) {
    const QJsonArray timeseries = QJsonDocument::fromJson(data).object()
                                      .value(QStringLiteral("properties")).toObject()
                                      .value(QStringLiteral("timeseries")).toArray();
    m_rawCurrent.valid = false;
    m_rawHourly.clear();
    m_rawDaily.clear();
    m_weatherAlerts.clear();

    const QDateTime now   = QDateTime::currentDateTime();
    const QDate     today = now.date();
    int hourlyCount = 0;

    struct DayAccum {
        double tempMax = -9999, tempMin = 9999;
        int    code = 0;
        double totalPrecip = 0.0;
        int    count = 0;
    };
    QMap<QDate, DayAccum> dayMap;

    for (const QJsonValue& v : timeseries) {
        const QJsonObject entry = v.toObject();
        const QDateTime   dt    = QDateTime::fromString(
                                      entry.value(QStringLiteral("time")).toString(),
                                      Qt::ISODate).toLocalTime();
        if (!dt.isValid()) continue;

        const QJsonObject entryData = entry.value(QStringLiteral("data")).toObject();
        const QJsonObject instant   = entryData.value(QStringLiteral("instant"))
                                               .toObject()
                                               .value(QStringLiteral("details"))
                                               .toObject();
        const QJsonObject next1h    = entryData.value(QStringLiteral("next_1_hours")).toObject();
        const QJsonObject next6h    = entryData.value(QStringLiteral("next_6_hours")).toObject();
        const QJsonObject next12h   = entryData.value(QStringLiteral("next_12_hours")).toObject();

        const double airTemp   = instant.value(QStringLiteral("air_temperature")).toDouble();
        const double windSpeed = instant.value(QStringLiteral("wind_speed")).toDouble() * 3.6;
        const int    windDir   = static_cast<int>(
                                     instant.value(QStringLiteral("wind_from_direction")).toDouble());
        const int    humidity  = static_cast<int>(
                                     instant.value(QStringLiteral("relative_humidity")).toDouble());

        // Symbol and precipitation: prefer next_1_hours, then next_6_hours, then next_12_hours
        QString symbolCode;
        double  precipAmount = 0.0;
        if (!next1h.isEmpty()) {
            symbolCode   = next1h.value(QStringLiteral("summary")).toObject()
                               .value(QStringLiteral("symbol_code")).toString();
            precipAmount = next1h.value(QStringLiteral("details")).toObject()
                               .value(QStringLiteral("precipitation_amount")).toDouble();
        } else if (!next6h.isEmpty()) {
            symbolCode   = next6h.value(QStringLiteral("summary")).toObject()
                               .value(QStringLiteral("symbol_code")).toString();
            precipAmount = next6h.value(QStringLiteral("details")).toObject()
                               .value(QStringLiteral("precipitation_amount")).toDouble();
        } else if (!next12h.isEmpty()) {
            symbolCode   = next12h.value(QStringLiteral("summary")).toObject()
                               .value(QStringLiteral("symbol_code")).toString();
        }

        const int wmoCode    = metNoSymbolToWmo(symbolCode);
        const int precipProb = precipAmount > 0.1
                               ? static_cast<int>(qMin(20.0 + precipAmount * 30.0, 90.0)) : 0;

        // Current: first entry at or just before now
        if (!m_rawCurrent.valid && dt >= now.addSecs(-3600)) {
            m_rawCurrent.valid     = true;
            m_rawCurrent.temp      = airTemp;
            m_rawCurrent.feelsLike = airTemp;
            m_rawCurrent.humidity  = humidity;
            m_rawCurrent.windSpeed = windSpeed;
            m_rawCurrent.windDir   = windDir;
            m_rawCurrent.code      = wmoCode;
        }

        // Hourly: next 12 hours from now
        if (hourlyCount < 12 && dt >= now && dt.date() <= today.addDays(1)) {
            RawHourly rh;
            rh.hour       = dt.time().hour();
            rh.temp       = airTemp;
            rh.code       = wmoCode;
            rh.precipProb = precipProb;
            m_rawHourly.append(rh);
            ++hourlyCount;
        }

        // Daily accumulation (9-day window)
        if (dt.date() <= today.addDays(9)) {
            DayAccum& da = dayMap[dt.date()];
            da.tempMax     = qMax(da.tempMax, airTemp);
            da.tempMin     = qMin(da.tempMin, airTemp);
            da.totalPrecip += precipAmount;
            if (wmoCode > da.code) da.code = wmoCode;
            ++da.count;
        }
    }

    m_rawDaily.clear();
    for (auto it = dayMap.begin(); it != dayMap.end(); ++it) {
        const DayAccum& da = it.value();
        if (da.count == 0) continue;
        RawDaily rd;
        rd.date       = it.key();
        rd.tempMax    = da.tempMax > -9998 ? da.tempMax : 0.0;
        rd.tempMin    = da.tempMin <  9998 ? da.tempMin : 0.0;
        rd.code       = da.code;
        rd.precipProb = da.totalPrecip > 0.1
                        ? static_cast<int>(qMin(20.0 + da.totalPrecip * 10.0, 90.0)) : 0;
        m_rawDaily.append(rd);
    }

    buildWeatherData();
    setBusy(false);
}

// ── Auto-location detection ────────────────────────────────────────────────────

void WeatherManager::detectLocation() {
    if (m_locationDetecting) return;
    m_locationDetecting = true;
    emit locationDetectingChanged();
    setStatus(QStringLiteral("Detecting location\u2026"));

#ifdef HAS_QT_POSITIONING
    if (!m_geoSource)
        m_geoSource = QGeoPositionInfoSource::createDefaultSource(this);

    if (m_geoSource) {
        disconnect(m_geoSource, nullptr, this, nullptr);

        connect(m_geoSource, &QGeoPositionInfoSource::positionUpdated, this,
                [this](const QGeoPositionInfo& info) {
                    disconnect(m_geoSource, nullptr, this, nullptr);
                    onCoordsDetected(info.coordinate().latitude(),
                                     info.coordinate().longitude());
                }, Qt::SingleShotConnection);

        connect(m_geoSource, &QGeoPositionInfoSource::errorOccurred, this,
                [this](QGeoPositionInfoSource::Error) {
                    disconnect(m_geoSource, nullptr, this, nullptr);
                    fetchIPGeolocation();
                }, Qt::SingleShotConnection);

        m_geoSource->requestUpdate(15000);
        return;
    }
#endif
    fetchIPGeolocation();
}

void WeatherManager::fetchIPGeolocation() {
    QNetworkRequest req(QUrl(QStringLiteral("https://ipinfo.io/json")));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CalDisplay/1.0 (github.com/caldisplay)"));
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_locationDetecting = false;
            emit locationDetectingChanged();
            setStatus(QStringLiteral("Location detection failed: ") + reply->errorString());
            return;
        }
        parseIPGeolocation(reply->readAll());
    });
}

void WeatherManager::parseIPGeolocation(const QByteArray& data) {
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    const QString loc = root.value(QStringLiteral("loc")).toString();
    const int commaIdx = loc.indexOf(QLatin1Char(','));
    if (commaIdx < 0) {
        m_locationDetecting = false;
        emit locationDetectingChanged();
        setStatus(QStringLiteral("Location detection: unexpected response"));
        return;
    }
    const double lat = loc.left(commaIdx).toDouble();
    const double lon = loc.mid(commaIdx + 1).toDouble();
    const QString city   = root.value(QStringLiteral("city")).toString();
    const QString region = root.value(QStringLiteral("region")).toString();
    QString name;
    if (!city.isEmpty())
        name = region.isEmpty() ? city : city + QStringLiteral(", ") + region;
    onCoordsDetected(lat, lon, name);
}

void WeatherManager::onCoordsDetected(double lat, double lon, const QString& locationName) {
    m_latitude       = lat;
    m_longitude      = lon;
    m_hasCoordinates = true;

    const QString coordStr = QString::number(lat, 'f', 4)
                             + QStringLiteral(",")
                             + QString::number(lon, 'f', 4);
    m_locationQuery       = coordStr;
    m_cachedLocationQuery = coordStr;
    emit locationQueryChanged();

    if (!locationName.isEmpty()) {
        m_locationName = locationName;
        emit locationNameChanged();
    }

    m_locationDetecting = false;
    emit locationDetectingChanged();

    saveSettings();
    setBusy(true);
    setStatus(QStringLiteral("Updating\u2026"));
    doFetch();
}

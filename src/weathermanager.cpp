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
#include <QFileInfo>
#include <QPixmap>
#include <QDirIterator>

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

    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                              + QStringLiteral("/weather-icons");
    QDir().mkpath(cacheRoot);

    const QString cachePath = cacheRoot
                              + QStringLiteral("/")
                              + iconName
                              + QStringLiteral("-")
                              + QString::number(size)
                              + QStringLiteral(".png");

    if (QFileInfo::exists(cachePath))
        return QUrl::fromLocalFile(cachePath).toString();

    const QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull())
        return QString();

    const QPixmap pixmap = icon.pixmap(size, size);
    if (pixmap.isNull())
        return QString();

    if (pixmap.save(cachePath, "PNG"))
        return QUrl::fromLocalFile(cachePath).toString();

    // Fallback: direct lookup in Papirus icon directories when theme engine cannot render.
    QStringList roots;
    const QByteArray snap = qgetenv("SNAP");
    if (!snap.isEmpty())
        roots << (QString::fromLocal8Bit(snap) + QStringLiteral("/usr/share/icons/Papirus"));
    roots << QStringLiteral("/usr/share/icons/Papirus");

    const QStringList relDirs = {
        QStringLiteral("48x48/status"),
        QStringLiteral("24x24/panel"),
        QStringLiteral("16x16/panel"),
        QStringLiteral("scalable/status"),
        QStringLiteral("scalable/panel")
    };
    const QStringList exts = { QStringLiteral(".svg"), QStringLiteral(".png"), QStringLiteral(".xpm") };

    for (const QString& root : roots) {
        for (const QString& rel : relDirs) {
            for (const QString& ext : exts) {
                const QString candidate = root + QStringLiteral("/") + rel + QStringLiteral("/") + iconName + ext;
                if (QFileInfo::exists(candidate))
                    return QUrl::fromLocalFile(candidate).toString();
            }
        }
    }

    return QString();
}

QString WeatherManager::fmtTemp(double celsius) const {
    if (m_temperatureUnit == Fahrenheit)
        return QString::number(qRound(celsius * 9.0 / 5.0 + 32.0)) + QStringLiteral("\u00B0F");
    return QString::number(qRound(celsius)) + QStringLiteral("\u00B0C");
}

QString WeatherManager::fmtWind(double kmh) const {
    // Always show km/h for now; could add mph toggle alongside temperature unit
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
QString WeatherManager::statusMessage()         const { return m_statusMessage; }
bool    WeatherManager::configured()            const { return !m_locationQuery.trimmed().isEmpty(); }
bool    WeatherManager::hasData()               const { return m_rawCurrent.valid; }
QVariantMap  WeatherManager::currentWeather()   const { return m_currentWeather; }
QVariantList WeatherManager::hourlyForecast()   const { return m_hourlyForecast; }
QVariantList WeatherManager::dailyForecast()    const { return m_dailyForecast; }

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
    if (m_busy) return;
    setBusy(true);
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

void WeatherManager::doFetch() {
    if (m_provider == OpenWeatherMap && !m_apiKey.isEmpty())
        fetchOWMCurrent();
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
    const QJsonObject root = QJsonDocument::fromJson(data).object();

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
        item[QStringLiteral("sunrise")]    = d.sunrise;
        item[QStringLiteral("sunset")]     = d.sunset;
        item[QStringLiteral("code")]       = d.code;
        daily.append(item);
    }
    m_dailyForecast = daily;

    const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    setStatus(QStringLiteral("Updated ") + ts);
    emit weatherUpdated();
}

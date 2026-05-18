// SPDX-License-Identifier: GPL-3.0-or-later
// CalDisplay - A calendar application for displaying events from shared ICS feeds
// Copyright (C) 2026 Erich Eickmeyer
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <QDate>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;

class WeatherManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(int     provider             READ provider             WRITE setProvider             NOTIFY providerChanged)
    Q_PROPERTY(QString locationQuery        READ locationQuery        WRITE setLocationQuery        NOTIFY locationQueryChanged)
    Q_PROPERTY(QString locationName         READ locationName                                       NOTIFY locationNameChanged)
    Q_PROPERTY(QString apiKey               READ apiKey               WRITE setApiKey               NOTIFY apiKeyChanged)
    Q_PROPERTY(int     temperatureUnit      READ temperatureUnit      WRITE setTemperatureUnit      NOTIFY temperatureUnitChanged)
    Q_PROPERTY(bool    autoRefreshEnabled   READ autoRefreshEnabled   WRITE setAutoRefreshEnabled   NOTIFY autoRefreshEnabledChanged)
    Q_PROPERTY(int     refreshIntervalMinutes READ refreshIntervalMinutes WRITE setRefreshIntervalMinutes NOTIFY refreshIntervalMinutesChanged)
    Q_PROPERTY(bool    busy                 READ busy                                               NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage        READ statusMessage                                      NOTIFY statusMessageChanged)
    Q_PROPERTY(bool    configured           READ configured                                         NOTIFY configuredChanged)
    Q_PROPERTY(bool    hasData              READ hasData                                            NOTIFY weatherUpdated)
    Q_PROPERTY(QVariantMap  currentWeather  READ currentWeather                                    NOTIFY weatherUpdated)
    Q_PROPERTY(QVariantList hourlyForecast  READ hourlyForecast                                    NOTIFY weatherUpdated)
    Q_PROPERTY(QVariantList dailyForecast   READ dailyForecast                                     NOTIFY weatherUpdated)

public:
    // provider values
    static constexpr int OpenMeteo       = 0;
    static constexpr int OpenWeatherMap  = 1;

    // temperatureUnit values
    static constexpr int Celsius         = 0;
    static constexpr int Fahrenheit      = 1;

    explicit WeatherManager(QObject* parent = nullptr);

    int     provider()              const;
    void    setProvider(int);
    QString locationQuery()         const;
    void    setLocationQuery(const QString&);
    QString locationName()          const;
    QString apiKey()                const;
    void    setApiKey(const QString&);
    int     temperatureUnit()       const;
    void    setTemperatureUnit(int);
    bool    autoRefreshEnabled()    const;
    void    setAutoRefreshEnabled(bool);
    int     refreshIntervalMinutes() const;
    void    setRefreshIntervalMinutes(int);
    bool    busy()                  const;
    QString statusMessage()         const;
    bool    configured()            const;
    bool    hasData()               const;
    QVariantMap  currentWeather()   const;
    QVariantList hourlyForecast()   const;
    QVariantList dailyForecast()    const;

    Q_INVOKABLE void refreshWeather();
    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void loadSettings();

signals:
    void providerChanged();
    void locationQueryChanged();
    void locationNameChanged();
    void apiKeyChanged();
    void temperatureUnitChanged();
    void autoRefreshEnabledChanged();
    void refreshIntervalMinutesChanged();
    void busyChanged();
    void statusMessageChanged();
    void configuredChanged();
    void weatherUpdated();

private:
    // ── Settings ─────────────────────────────────────────────────────────────
    int     m_provider               = OpenMeteo;
    QString m_locationQuery;
    QString m_locationName;
    QString m_cachedLocationQuery;   // query used to obtain current coordinates
    double  m_latitude               = 0.0;
    double  m_longitude              = 0.0;
    bool    m_hasCoordinates         = false;
    QString m_apiKey;
    int     m_temperatureUnit        = Celsius;
    bool    m_autoRefreshEnabled     = true;
    int     m_refreshIntervalMinutes = 30;

    // ── Runtime state ─────────────────────────────────────────────────────────
    bool    m_busy         = false;
    QString m_statusMessage;

    QNetworkAccessManager* m_nam;
    QTimer  m_autoRefreshTimer;

    // ── Raw (always-Celsius) storage ─────────────────────────────────────────
    struct RawCurrent {
        bool    valid     = false;
        double  temp      = 0;
        double  feelsLike = 0;
        int     humidity  = 0;
        double  windSpeed = 0;   // km/h
        int     windDir   = 0;
        int     code      = 0;
    };
    struct RawHourly {
        int    hour;
        double temp;             // Celsius
        int    code;
        int    precipProb;
    };
    struct RawDaily {
        QDate  date;
        double tempMax;          // Celsius
        double tempMin;          // Celsius
        int    code;
        int    precipProb;
        QString sunrise;
        QString sunset;
    };

    RawCurrent         m_rawCurrent;
    QList<RawHourly>   m_rawHourly;
    QList<RawDaily>    m_rawDaily;

    // Formatted output (rebuilt whenever rawdata or unit changes)
    QVariantMap  m_currentWeather;
    QVariantList m_hourlyForecast;
    QVariantList m_dailyForecast;

    // ── Private methods ───────────────────────────────────────────────────────
    void setBusy(bool);
    void setStatus(const QString&);
    void updateAutoRefreshTimer();
    void doFetch();

    // Open-Meteo
    void geocodeOpenMeteo();
    void parseOpenMeteoGeocode(const QByteArray&);
    void fetchOpenMeteo();
    void parseOpenMeteoWeather(const QByteArray&);

    // OpenWeatherMap
    void geocodeOWM();
    void parseOWMGeocode(const QByteArray&);
    void fetchOWMCurrent();
    void parseOWMCurrent(const QByteArray&);
    void fetchOWMForecast();
    void parseOWMForecast(const QByteArray&);

    void buildWeatherData();

    // ── Static helpers ────────────────────────────────────────────────────────
    static QString wmoDescription(int code);
    static QString wmoIcon(int code);
    static QString windDirText(int degrees);
    static int     owmIdToWmo(int id);
    static QString settingsFilePath();
    static QString iconPathForName(const QString& iconName, int size);

    QString fmtTemp(double celsius) const;
    QString fmtWind(double kmh) const;
    QString tempUnitStr() const;
};

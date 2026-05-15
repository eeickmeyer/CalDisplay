// SPDX-License-Identifier: GPL-3.0-or-later
// CalDisplay - A calendar application for displaying events from shared ICS feeds
// Copyright (C) 2026 Erich Eickmeyer
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QTimer>

struct CalendarEvent {
    QString title;
    QString calendar;
    QString color;
    QDateTime start;
    QDateTime end;
    QString status = QStringLiteral("CONFIRMED");
};

class EventModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString lastUpdated READ lastUpdated NOTIFY lastUpdatedChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        CalendarRole,
        ColorRole,
        StartRole,
        EndRole,
        StartDisplayRole,
        EndDisplayRole,
        IsTodayRole,
        IsSoonRole
    };

    explicit EventModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void manualRefresh();
    Q_INVOKABLE QVariantList getEvents() const;
    QString lastUpdated() const;
    void setExternalEvents(const QList<CalendarEvent>& events);

signals:
    void lastUpdatedChanged();

private:
    QList<CalendarEvent> m_events;
    QDateTime m_lastUpdated;
    QTimer m_tick;

    void loadSampleData();
    void refreshDerivedState();
};

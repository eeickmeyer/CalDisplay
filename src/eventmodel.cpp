// SPDX-License-Identifier: GPL-3.0-or-later
// CalDisplay - A calendar application for displaying events from shared ICS feeds
// Copyright (C) 2026 Erich Eickmeyer
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "eventmodel.h"

#include <QDate>
#include <QLocale>

EventModel::EventModel(QObject* parent)
    : QAbstractListModel(parent) {
    loadSampleData();
    m_lastUpdated = QDateTime::currentDateTime();

    m_tick.setInterval(60 * 1000);
    m_tick.setSingleShot(false);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        refreshDerivedState();
    });
    m_tick.start();
}

int EventModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_events.size();
}

QVariant EventModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_events.size()) {
        return {};
    }

    const CalendarEvent& ev = m_events.at(index.row());
    const QDate today = QDate::currentDate();
    const QDateTime now = QDateTime::currentDateTime();

    switch (role) {
    case TitleRole:
        return ev.title;
    case CalendarRole:
        return ev.calendar;
    case ColorRole:
        return ev.color;
    case StartRole:
        return ev.start;
    case EndRole:
        return ev.end;
    case StartDisplayRole:
        return QLocale().toString(ev.start, "ddd HH:mm");
    case EndDisplayRole:
        return QLocale().toString(ev.end, "HH:mm");
    case IsTodayRole:
        return ev.start.date() == today;
    case IsSoonRole:
        return now.secsTo(ev.start) >= 0 && now.secsTo(ev.start) <= 60 * 60;
    default:
        return {};
    }
}

QHash<int, QByteArray> EventModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {CalendarRole, "calendar"},
        {ColorRole, "color"},
        {StartRole, "start"},
        {EndRole, "end"},
        {StartDisplayRole, "startDisplay"},
        {EndDisplayRole, "endDisplay"},
        {IsTodayRole, "isToday"},
        {IsSoonRole, "isSoon"},
    };
}

void EventModel::manualRefresh() {
    refreshDerivedState();
}

QVariantList EventModel::getEvents() const {
    QVariantList result;
    for (const CalendarEvent& ev : m_events) {
        QVariantMap m;
        m["title"]   = ev.title;
        m["calendar"] = ev.calendar;
        m["color"]   = ev.color;
        m["startMs"] = ev.start.toMSecsSinceEpoch();
        m["endMs"]   = ev.end.toMSecsSinceEpoch();
        m["status"]  = ev.status;
        result.append(m);
    }
    return result;
}

QString EventModel::lastUpdated() const {
    return QLocale().toString(m_lastUpdated, "ddd yyyy-MM-dd HH:mm:ss");
}

void EventModel::setExternalEvents(const QList<CalendarEvent>& events) {
    beginResetModel();
    m_events = events;
    endResetModel();

    m_lastUpdated = QDateTime::currentDateTime();
    emit lastUpdatedChanged();
}

void EventModel::loadSampleData() {
    const QDate today = QDate::currentDate();

    m_events = {
        {
            "All-day family outing",
            "Family",
            "#f97316",
            QDateTime(today, QTime(0, 0)),
            QDateTime(today.addDays(1), QTime(0, 0))
        },
        {
            "Morning planning",
            "Work",
            "#2daee0",
            QDateTime(today, QTime(7, 15)),
            QDateTime(today, QTime(7, 45))
        },
        {
            "School drop-off",
            "Family",
            "#2f855a",
            QDateTime(today, QTime(8, 0)),
            QDateTime(today, QTime(8, 20))
        },
        {
            "Dentist - Alex",
            "Health",
            "#2b6cb0",
            QDateTime(today, QTime(9, 30)),
            QDateTime(today, QTime(10, 30))
        },
        {
            "Team standup",
            "Work",
            "#0082c9",
            QDateTime(today, QTime(10, 0)),
            QDateTime(today, QTime(10, 30))
        },
        {
            "Project review",
            "Work",
            "#9c6ade",
            QDateTime(today, QTime(10, 15)),
            QDateTime(today, QTime(11, 0))
        },
        {
            "Lunch break",
            "Personal",
            "#4cb9c5",
            QDateTime(today, QTime(12, 0)),
            QDateTime(today, QTime(12, 45))
        },
        {
            "Errands",
            "House",
            "#f4b13d",
            QDateTime(today, QTime(14, 0)),
            QDateTime(today, QTime(15, 15))
        },
        {
            "Kids pickup",
            "Family",
            "#66bb6a",
            QDateTime(today, QTime(15, 30)),
            QDateTime(today, QTime(16, 0))
        },
        {
            "Groceries",
            "House",
            "#d69e2e",
            QDateTime(today, QTime(17, 15)),
            QDateTime(today, QTime(18, 0))
        },
        {
            "Workout",
            "Health",
            "#e35d6a",
            QDateTime(today, QTime(18, 30)),
            QDateTime(today, QTime(19, 15))
        },
        {
            "Call with parents",
            "Family",
            "#7c3aed",
            QDateTime(today, QTime(19, 0)),
            QDateTime(today, QTime(19, 30))
        },
        {
            "Tomorrow prep",
            "Personal",
            "#0f766e",
            QDateTime(today, QTime(20, 0)),
            QDateTime(today, QTime(20, 30))
        },
        {
            "Yesterday follow-up",
            "Work",
            "#2daee0",
            QDateTime(today.addDays(-1), QTime(16, 15)),
            QDateTime(today.addDays(-1), QTime(17, 0))
        },
        {
            "Past due admin",
            "House",
            "#b45309",
            QDateTime(today.addDays(-1), QTime(18, 45)),
            QDateTime(today.addDays(-1), QTime(19, 15))
        },
        {
            "Soccer practice",
            "Kids",
            "#c53030",
            QDateTime(today.addDays(1), QTime(18, 30)),
            QDateTime(today.addDays(1), QTime(19, 45))
        },
        {
            "Grandma dinner",
            "Family",
            "#6b46c1",
            QDateTime(today.addDays(2), QTime(19, 0)),
            QDateTime(today.addDays(2), QTime(21, 0))
        },
        {
            "Coffee with Alex",
            "Personal",
            "#ec4899",
            QDateTime(today, QTime(14, 30)),
            QDateTime(today, QTime(15, 30)),
            QStringLiteral("TENTATIVE")
        }
    };
}

void EventModel::refreshDerivedState() {
    if (!m_events.isEmpty()) {
        const QModelIndex first = index(0, 0);
        const QModelIndex last = index(m_events.size() - 1, 0);
        emit dataChanged(first, last, {IsTodayRole, IsSoonRole, StartDisplayRole, EndDisplayRole});
    }

    m_lastUpdated = QDateTime::currentDateTime();
    emit lastUpdatedChanged();
}

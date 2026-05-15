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
            "Groceries",
            "House",
            "#d69e2e",
            QDateTime(today, QTime(17, 15)),
            QDateTime(today, QTime(18, 0))
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

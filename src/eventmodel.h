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

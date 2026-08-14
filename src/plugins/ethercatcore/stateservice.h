// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <utils/id.h>

#include <QHash>
#include <QObject>

namespace EtherCAT::Core {

enum class StatusSeverity { Ready, Busy, Warning, Error };

struct ETHERCATCORE_EXPORT StatusEntry
{
    Utils::Id sourceId;
    StatusSeverity severity = StatusSeverity::Ready;
    QString summary;
    QString details;

    friend bool operator==(const StatusEntry &left, const StatusEntry &right)
    {
        return left.sourceId == right.sourceId && left.severity == right.severity
               && left.summary == right.summary && left.details == right.details;
    }
};

class ETHERCATCORE_EXPORT StateService final : public QObject
{
    Q_OBJECT

public:
    explicit StateService(QObject *parent = nullptr);

    bool setStatus(const StatusEntry &status);
    void clearStatus(Utils::Id sourceId);
    void clearAll();

    QList<StatusEntry> statuses() const;
    StatusSeverity aggregateSeverity() const;

signals:
    void statusChanged(Utils::Id sourceId);
    void aggregateSeverityChanged(EtherCAT::Core::StatusSeverity severity);

private:
    StatusSeverity calculateAggregateSeverity() const;

    QHash<Utils::Id, StatusEntry> m_statuses;
    StatusSeverity m_aggregateSeverity = StatusSeverity::Ready;
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::StatusSeverity)
Q_DECLARE_METATYPE(EtherCAT::Core::StatusEntry)

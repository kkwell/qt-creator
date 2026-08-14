// Copyright (C) 2026 Kvell

#include "stateservice.h"

#include <utils/algorithm.h>
#include <utils/qtcassert.h>

#include <QThread>

namespace EtherCAT::Core {

StateService::StateService(QObject *parent)
    : QObject(parent)
{}

bool StateService::setStatus(const StatusEntry &status)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return false);

    if (!status.sourceId.isValid())
        return false;

    const auto existing = m_statuses.constFind(status.sourceId);
    if (existing != m_statuses.cend() && existing.value() == status)
        return true;

    const StatusSeverity previousAggregate = m_aggregateSeverity;
    m_statuses.insert(status.sourceId, status);
    m_aggregateSeverity = calculateAggregateSeverity();

    emit statusChanged(status.sourceId);
    if (previousAggregate != m_aggregateSeverity)
        emit aggregateSeverityChanged(m_aggregateSeverity);
    return true;
}

void StateService::clearStatus(Utils::Id sourceId)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    if (!m_statuses.remove(sourceId))
        return;

    const StatusSeverity previousAggregate = m_aggregateSeverity;
    m_aggregateSeverity = calculateAggregateSeverity();
    emit statusChanged(sourceId);
    if (previousAggregate != m_aggregateSeverity)
        emit aggregateSeverityChanged(m_aggregateSeverity);
}

void StateService::clearAll()
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    const QList<Utils::Id> sourceIds = m_statuses.keys();
    if (sourceIds.isEmpty())
        return;

    const StatusSeverity previousAggregate = m_aggregateSeverity;
    m_statuses.clear();
    m_aggregateSeverity = StatusSeverity::Ready;

    for (Utils::Id sourceId : sourceIds)
        emit statusChanged(sourceId);
    if (previousAggregate != m_aggregateSeverity)
        emit aggregateSeverityChanged(m_aggregateSeverity);
}

QList<StatusEntry> StateService::statuses() const
{
    QList<StatusEntry> result = m_statuses.values();
    Utils::sort(result, [](const StatusEntry &left, const StatusEntry &right) {
        return left.sourceId.toString() < right.sourceId.toString();
    });
    return result;
}

StatusSeverity StateService::aggregateSeverity() const
{
    return m_aggregateSeverity;
}

StatusSeverity StateService::calculateAggregateSeverity() const
{
    StatusSeverity result = StatusSeverity::Ready;
    for (const StatusEntry &status : m_statuses) {
        if (static_cast<int>(status.severity) > static_cast<int>(result))
            result = status.severity;
    }
    return result;
}

} // namespace EtherCAT::Core

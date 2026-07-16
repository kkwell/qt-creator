// Copyright (C) 2026 Kvell

#include "selectionservice.h"

#include <utils/qtcassert.h>

#include <QThread>

namespace EtherCAT::Core {

SelectionService::SelectionService(QObject *parent)
    : QObject(parent)
{}

Data::NodeId SelectionService::currentNodeId() const
{
    return m_currentNodeId;
}

void SelectionService::setCurrentNodeId(const Data::NodeId &nodeId)
{
    QTC_ASSERT(QThread::currentThread() == thread(), return);

    if (m_currentNodeId == nodeId)
        return;

    const Data::NodeId previous = m_currentNodeId;
    m_currentNodeId = nodeId;
    emit currentNodeChanged(m_currentNodeId, previous);
}

void SelectionService::clear()
{
    setCurrentNodeId({});
}

} // namespace EtherCAT::Core

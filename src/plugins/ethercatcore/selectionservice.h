// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/nodeid.h>

#include <QObject>

namespace EtherCAT::Core {

class ETHERCATCORE_EXPORT SelectionService final : public QObject
{
    Q_OBJECT

public:
    explicit SelectionService(QObject *parent = nullptr);

    Data::NodeId currentNodeId() const;
    void setCurrentNodeId(const Data::NodeId &nodeId);
    void clear();

signals:
    void currentNodeChanged(
        const EtherCAT::Data::NodeId &current, const EtherCAT::Data::NodeId &previous);

private:
    Data::NodeId m_currentNodeId;
};

} // namespace EtherCAT::Core

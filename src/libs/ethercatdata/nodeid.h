// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatdata_global.h"

#include <QMetaType>
#include <QString>
#include <QUuid>

namespace EtherCAT::Data {

class NodeId;
ETHERCATDATA_EXPORT size_t qHash(const NodeId &id, size_t seed = 0) noexcept;

class ETHERCATDATA_EXPORT NodeId
{
public:
    NodeId() = default;

    static NodeId create();
    static NodeId fromString(const QString &value);

    bool isNull() const;
    QString toString() const;

    friend bool operator==(const NodeId &left, const NodeId &right)
    {
        return left.m_uuid == right.m_uuid;
    }

    friend bool operator!=(const NodeId &left, const NodeId &right) { return !(left == right); }

private:
    explicit NodeId(const QUuid &uuid);

    QUuid m_uuid;

    friend size_t qHash(const NodeId &id, size_t seed) noexcept;
};

} // namespace EtherCAT::Data

Q_DECLARE_METATYPE(EtherCAT::Data::NodeId)

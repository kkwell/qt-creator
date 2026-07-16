// Copyright (C) 2026 Kvell

#include "nodeid.h"

namespace EtherCAT::Data {

NodeId::NodeId(const QUuid &uuid)
    : m_uuid(uuid)
{}

NodeId NodeId::create()
{
    return NodeId(QUuid::createUuid());
}

NodeId NodeId::fromString(const QString &value)
{
    return NodeId(QUuid(value));
}

bool NodeId::isNull() const
{
    return m_uuid.isNull();
}

QString NodeId::toString() const
{
    return m_uuid.toString(QUuid::WithoutBraces);
}

size_t qHash(const NodeId &id, size_t seed) noexcept
{
    return ::qHash(id.m_uuid, seed);
}

} // namespace EtherCAT::Data

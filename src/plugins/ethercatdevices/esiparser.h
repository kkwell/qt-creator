// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/devicedescription.h>

#include <utils/result.h>

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace EtherCAT::Devices::Internal {

Utils::Result<QList<Data::DeviceDescription>> parseEsiFile(
    const QByteArray &contents, const QString &sourcePath, const QDateTime &importedAt);

} // namespace EtherCAT::Devices::Internal

// Copyright (C) 2026 Embed Labs

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/controllerconnection.h>

#include <utils/result.h>

#include <QObject>

namespace EtherCAT::Core {

class ControllerConnectionProvider;

// Owns the IDE-local evidence transaction around one controller package
// deployment. Implementations may call the provider only after independently
// validating the exact package and compiled-project source.
class ETHERCATCORE_EXPORT RuntimePackageActivationService : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual Utils::Result<> deployAndTrack(
        ControllerConnectionProvider *provider,
        const Data::ControllerPackageDeploymentRequest &request)
        = 0;
};

} // namespace EtherCAT::Core


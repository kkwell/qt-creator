// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/devicedescription.h>
#include <ethercatdata/diagnosticssnapshot.h>
#include <ethercatdata/projectsnapshot.h>
#include <ethercatdata/scansnapshot.h>

#include <QObject>
#include <QString>

#include <optional>

namespace EtherCAT::Core {

// This aggregate is deliberately value-only. In particular, it must never grow
// Provider pointers or callbacks: automation clients observe the same IDE state
// as the Workbench without acquiring a second state or control path.
struct ETHERCATCORE_EXPORT AutomationContextSnapshot
{
    QString controllerId;
    QString identitySource;
    Data::ProjectSnapshot project;
    Data::ControllerConnectionScope scope;
    std::optional<Data::ControllerConnectionProfileConfiguration> connectionProfile;
    Data::ControllerConnectionSnapshot connection;
    std::optional<Data::ScanResult> scan;
    std::optional<Data::DiagnosticsSnapshot> diagnostics;
    QList<Data::DeviceDescription> deviceDescriptions;
    bool mock = false;

    friend bool operator==(const AutomationContextSnapshot &, const AutomationContextSnapshot &)
        = default;
};

ETHERCATCORE_EXPORT QString automationControllerId(const Data::ControllerConnectionScope &scope);

class ETHERCATCORE_EXPORT AutomationService : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // Implementations must build fresh value snapshots on every call.
    virtual QList<AutomationContextSnapshot> contexts() const = 0;
    virtual std::optional<AutomationContextSnapshot> context(const QString &controllerId) const;

signals:
    void contextsChanged();
};

} // namespace EtherCAT::Core

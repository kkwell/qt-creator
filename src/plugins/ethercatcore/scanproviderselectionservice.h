// Copyright (C) 2026 Kvell

#pragma once

#include "ethercatcore_global.h"

#include <ethercatdata/controllerconnection.h>
#include <ethercatdata/projectsnapshot.h>

#include <utils/id.h>
#include <utils/result.h>

#include <QList>
#include <QObject>

#include <optional>

namespace EtherCAT::Core {

class ProjectService;
class Provider;
class ProviderRegistry;
class ScanProvider;

struct ETHERCATCORE_EXPORT ScanProviderSelection
{
    Data::ControllerConnectionScope scope;
    Utils::Id providerId;

    bool isValid() const;

    friend bool operator==(const ScanProviderSelection &, const ScanProviderSelection &) = default;
};

// Owns only the process-lifetime choice of ScanProvider for each open
// project/master scope. Providers remain the sole owners of scan state and
// results; this service never starts, cancels, clears, or caches a scan.
class ETHERCATCORE_EXPORT ScanProviderSelectionService final : public QObject
{
    Q_OBJECT

public:
    explicit ScanProviderSelectionService(
        ProviderRegistry *providerRegistry, QObject *parent = nullptr);

    std::optional<ScanProviderSelection> selection(
        const Data::ControllerConnectionScope &scope) const;
    QList<ScanProviderSelection> selections() const;
    bool selectionIsAvailable(const Data::ControllerConnectionScope &scope) const;

    Utils::Result<> select(
        const Data::ControllerConnectionScope &scope, Utils::Id providerId);
    Utils::Result<> clear(const Data::ControllerConnectionScope &scope);

signals:
    void selectionChanged(const EtherCAT::Data::ControllerConnectionScope &scope);
    void selectionValidityChanged(
        const EtherCAT::Core::ScanProviderSelection &selection, bool available);

private:
    ScanProvider *registeredScanProvider(Utils::Id providerId) const;
    bool scopeExists(const Data::ControllerConnectionScope &scope) const;
    void attachProvider(Provider *provider);
    void handleProviderAboutToBeRemoved(Provider *provider);
    void handleProjectChanged(const Data::ProjectSnapshot &project);
    void notifyProviderValidity(Utils::Id providerId, bool available);
    void clearProjectSelections(const Data::NodeId &projectId);

    ProviderRegistry *m_providerRegistry = nullptr;
    QList<ScanProviderSelection> m_selections;
};

} // namespace EtherCAT::Core

Q_DECLARE_METATYPE(EtherCAT::Core::ScanProviderSelection)

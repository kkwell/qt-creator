// Copyright (C) 2026 Embed Labs

#include "runtimepackagecompilerprojectrequestbuilder.h"

#include "providerregistry.h"
#include "runtimepackagecompilerpreparationcoordinator.h"

#include <QRegularExpression>
#include <QThread>

namespace EtherCAT::Core {

bool RuntimePackageCompilerProjectRequestSeed::isValid() const
{
    static const QRegularExpression stableId(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,191}$"));
    return !scope.projectId.isNull() && !scope.masterId.isNull()
           && compileOperationId.isValid() && verifyOperationId.isValid()
           && activationOperationId.isValid()
           && compileOperationId.value() != verifyOperationId.value()
           && compileOperationId.value() != activationOperationId.value()
           && verifyOperationId.value() != activationOperationId.value()
           && stableId.match(intentId).hasMatch() && configurationId != 0
           && buildTimestampNs != 0 && compileTimeNs >= buildTimestampNs;
}

RuntimePackageCompilerProjectRequestBuilder::RuntimePackageCompilerProjectRequestBuilder(
    Utils::Id id, const QString &displayName, QObject *parent)
    : Provider(
          ProviderKind::RuntimePackageCompilerProjectRequestBuilder,
          id,
          displayName,
          parent)
{}

Utils::Result<RuntimePackageCompilerProjectRequestBuilder *>
uniqueRuntimePackageCompilerProjectRequestBuilder(ProviderRegistry *providerRegistry)
{
    if (!providerRegistry)
        return Utils::ResultError(QStringLiteral("Compiler request builder registry is unavailable."));
    if (QThread::currentThread() != providerRegistry->thread()) {
        return Utils::ResultError(
            QStringLiteral("Compiler request builder registry belongs to another thread."));
    }

    QList<RuntimePackageCompilerProjectRequestBuilder *> available;
    for (Provider *provider : providerRegistry->providers(
             ProviderKind::RuntimePackageCompilerProjectRequestBuilder)) {
        if (!provider || !provider->isAvailable())
            continue;
        if (provider->thread() != QThread::currentThread()) {
            return Utils::ResultError(
                QStringLiteral("A compiler request builder belongs to another thread."));
        }
        auto *builder = qobject_cast<RuntimePackageCompilerProjectRequestBuilder *>(provider);
        if (!builder) {
            return Utils::ResultError(
                QStringLiteral("A compiler request builder provider has the wrong type."));
        }
        available.append(builder);
    }
    if (available.size() != 1) {
        return Utils::ResultError(
            available.isEmpty()
                ? QStringLiteral("No compiler request builder is available.")
                : QStringLiteral("Compiler request builder selection is ambiguous."));
    }
    return available.constFirst();
}

} // namespace EtherCAT::Core

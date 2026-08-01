// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatcore/runtimepackagecompilerpreparationcoordinator.h>

#include <utils/filepath.h>
#include <utils/result.h>

#include <QList>

#include <optional>

namespace EtherCAT::ProjectCompiler {

// This is deliberately a summary, not a serialized compiler request. The
// exact typed request must be supplied by the caller after a restart. Only
// public identities, result digests, and an externally supplied detached
// signing response are durable here.
struct RuntimePackageCompilerPreparationJournalEntry
{
    Data::RuntimePackageCompilerOperationId compileOperationId;
    Data::RuntimePackageCompilerSha256 startRequestFingerprint;
    Data::RuntimePackageCompilerOperationId verifyOperationId;
    Data::RuntimePackageActivationOperationId activationOperationId;
    QString compilerProviderId;
    Data::RuntimePackageCompilerContractIdentity contractIdentity;
    quint64 configurationId = 0;
    quint64 buildTimestampNs = 0;
    quint64 revision = 0;
    Core::RuntimePackageCompilerPreparationPhase phase
        = Core::RuntimePackageCompilerPreparationPhase::Idle;
    std::optional<Data::RuntimePackageCompilerSha256> compileResultSha256;
    std::optional<Data::RuntimePackageCompilerSha256> signRequestSha256;
    std::optional<Data::RuntimePackageCompilerCanonicalJson> detachedSigningResponse;
    std::optional<Data::RuntimePackageCompilerSha256> finalizeResultSha256;
    std::optional<Data::RuntimePackageCompilerSha256> packageSha256;
    std::optional<Data::RuntimePackageCompilerSha256> verifyResultSha256;
    QString detail;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerPreparationJournalEntry &,
        const RuntimePackageCompilerPreparationJournalEntry &) = default;
};

struct RuntimePackageCompilerPreparationJournalState
{
    quint64 sequence = 0;
    QList<RuntimePackageCompilerPreparationJournalEntry> entries;

    bool isValid() const;

    friend bool operator==(
        const RuntimePackageCompilerPreparationJournalState &,
        const RuntimePackageCompilerPreparationJournalState &) = default;
};

class RuntimePackageCompilerPreparationJournal
{
public:
    explicit RuntimePackageCompilerPreparationJournal(Utils::FilePath root);

    // Creates the private journal root and an empty journal when absent. Every
    // call validates the root, lock, and journal leaf before trusting bytes.
    Utils::Result<RuntimePackageCompilerPreparationJournalState> initialize();
    Utils::Result<RuntimePackageCompilerPreparationJournalState> load() const;

    // expectedSequence and expectedEntry are optimistic concurrency gates. A
    // successful update proves the exact durable predecessor, is atomically
    // renamed, and is directory-synchronized before it is returned.
    Utils::Result<RuntimePackageCompilerPreparationJournalState> commit(
        const RuntimePackageCompilerPreparationJournalEntry &entry,
        quint64 expectedSequence,
        std::optional<RuntimePackageCompilerPreparationJournalEntry> expectedEntry);

    Utils::FilePath root() const;
    Utils::FilePath journalFile() const;

private:
    Utils::FilePath m_root;
};

} // namespace EtherCAT::ProjectCompiler

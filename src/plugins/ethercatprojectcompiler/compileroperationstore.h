// Copyright (C) 2026 Embed Labs

#pragma once

#include <ethercatdata/runtimepackagecompiler.h>

#include <utils/filepath.h>
#include <utils/result.h>

#include <memory>

#ifdef WITH_TESTS
#include <functional>
#endif

namespace EtherCAT::ProjectCompiler {

struct CompilerStoreRoot;
struct CompilerStoreLock;

class CompilerOperationLease
{
public:
    CompilerOperationLease();
    ~CompilerOperationLease();
    CompilerOperationLease(CompilerOperationLease &&) noexcept;
    CompilerOperationLease &operator=(CompilerOperationLease &&) noexcept;

    CompilerOperationLease(const CompilerOperationLease &) = delete;
    CompilerOperationLease &operator=(const CompilerOperationLease &) = delete;

    bool isValid() const;

private:
    friend class CompilerOperationStore;
    CompilerOperationLease(std::unique_ptr<CompilerStoreLock> lock, QString storeIdentity);

    std::unique_ptr<CompilerStoreLock> m_lock;
    QString m_storeIdentity;
};

struct CompilerOperationPaths
{
    Utils::FilePath compilerRoot;
    Utils::FilePath compilerLedger;
    Utils::FilePath operationRoot;
    Utils::FilePath artifactRoot;
    Utils::FilePath outputDir;
    Utils::FilePath compileRequest;
    Utils::FilePath activationCapture;
    Utils::FilePath finalizeRequest;
    Utils::FilePath verifyRequest;
    Utils::FilePath signRequest;
    Utils::FilePath signResponse;
    Utils::FilePath publicKey;
    Utils::FilePath package;
};

enum class CompilerCanonicalEvidenceKind {
    CompilerRecord,
    SignRequest,
    SignResponse,
    QueryResponse,
    VerifyResponse,
};

enum class CompilerProvisionedFileKind {
    Executable,
    ProductionPublicKey,
};

class CompilerOperationStore
{
public:
    explicit CompilerOperationStore(Utils::FilePath compilerRoot);

    Utils::Result<> initialize();
    Utils::Result<CompilerOperationLease> acquireLease() const;

    Utils::Result<CompilerOperationPaths> reserveCompile(
        const CompilerOperationLease &lease,
        const Data::RuntimePackageCompilerCompileRequest &request,
        const Data::RuntimePackageCompilerCanonicalJson &canonicalRequest);
    Utils::Result<CompilerOperationPaths> reserveVerify(
        const CompilerOperationLease &lease,
        const Data::RuntimePackageCompilerVerifyRequest &request,
        const Data::RuntimePackageCompilerCanonicalJson &canonicalRequest);
    Utils::Result<CompilerOperationPaths> reserveFinalize(
        const CompilerOperationLease &lease,
        const Data::RuntimePackageCompilerFinalizeRequest &request,
        const Data::RuntimePackageCompilerCanonicalJson &canonicalRequest);
    Utils::Result<CompilerOperationPaths> validateQuery(
        const CompilerOperationLease &lease,
        const Data::RuntimePackageCompilerQueryRequest &request) const;

    Utils::Result<Utils::FilePath> persistCanonicalResponse(
        const CompilerOperationLease &lease,
        const Data::RuntimePackageCompilerOperationId &operationId,
        CompilerCanonicalEvidenceKind kind,
        const Data::RuntimePackageCompilerCanonicalJson &response);
    Utils::Result<Utils::FilePath> persistPackage(
        const CompilerOperationLease &lease,
        const Data::RuntimePackageCompilerOperationId &operationId,
        const QByteArray &packageBytes,
        const Data::RuntimePackageCompilerSha256 &packageSha256);
    Utils::Result<Utils::FilePath> persistFileEvidence(
        const CompilerOperationLease &lease,
        const Data::RuntimePackageCompilerOperationId &operationId,
        const Utils::FilePath &sourceFile,
        const Data::RuntimePackageCompilerSha256 &expectedSha256,
        qsizetype maximumBytes);

    Utils::Result<Utils::FilePath> pinProvisionedFile(
        const CompilerOperationLease &lease,
        const QByteArray &exactBytes,
        const Data::RuntimePackageCompilerSha256 &expectedSha256,
        CompilerProvisionedFileKind kind);
    Utils::Result<> validatePinnedProvisionedFile(
        const CompilerOperationLease &lease,
        const Utils::FilePath &file,
        const Data::RuntimePackageCompilerSha256 &expectedSha256,
        CompilerProvisionedFileKind kind) const;

    Utils::Result<QByteArray> readProviderFile(
        const CompilerOperationLease &lease,
        const Utils::FilePath &file,
        qsizetype maximumBytes) const;

    Utils::Result<> validateActivationProofEvidence(
        const Data::RuntimePackageCompilerActivationProof &proof,
        const Data::RuntimePackageCompilerCanonicalJson &canonicalCompileRequest,
        const Data::RuntimePackageCompilerCanonicalJson &canonicalFinalizeRequest,
        const Data::RuntimePackageCompilerCanonicalJson &canonicalVerifyRequest,
        qsizetype maximumArtifactBytes) const;

    Utils::FilePath compilerRoot() const;
    Utils::FilePath compilerLedger() const;
    Utils::FilePath operationRoot(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath artifactRoot(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath outputDir(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath compileRequest(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath activationCapture(
        const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath finalizeRequest(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath verifyRequest(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath signRequest(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath signResponse(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath publicKey(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::FilePath package(const Data::RuntimePackageCompilerOperationId &operationId) const;

#ifdef WITH_TESTS
    static void setBeforeLeafOpenHookForTest(std::function<void()> hook);
#endif

private:
    bool ownsLease(const CompilerOperationLease &lease) const;
    Utils::Result<CompilerOperationLease> acquireReadLease() const;
    CompilerOperationPaths paths(const Data::RuntimePackageCompilerOperationId &operationId) const;
    Utils::Result<> validateStore() const;
    Utils::Result<> materializeCompileArtifacts(
        const CompilerOperationLease &lease,
        const Data::RuntimePackageCompilerCompileRequest &request,
        const CompilerOperationPaths &paths);
    Utils::Result<> sealCompilerLedger(const CompilerOperationLease &lease) const;

    Utils::FilePath m_compilerRoot;
    QString m_storeIdentity;
    std::shared_ptr<CompilerStoreRoot> m_rootHandle;
};

} // namespace EtherCAT::ProjectCompiler

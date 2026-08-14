// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>

namespace EtherCAT::Diagnostics::Internal {

class DiagnosticsWorkflow;
class MockDiagnosticsProvider;

class DiagnosticsPropertyPageProvider final : public Core::PropertyPageProvider
{
    Q_OBJECT

public:
    DiagnosticsPropertyPageProvider(
        MockDiagnosticsProvider *diagnosticsProvider,
        DiagnosticsWorkflow *workflow,
        QObject *parent = nullptr);

    QList<Core::PropertyPageDescriptor> pages(
        const Core::PropertyPageContext &context) const final;
    QWidget *createPage(Utils::Id pageId, QWidget *parent) final;
    void updatePage(
        Utils::Id pageId, QWidget *page, const Core::PropertyPageContext &context) final;

private:
    QPointer<MockDiagnosticsProvider> m_diagnosticsProvider;
    QPointer<DiagnosticsWorkflow> m_workflow;
};

} // namespace EtherCAT::Diagnostics::Internal

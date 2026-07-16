// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>

namespace EtherCAT::Scan::Internal {

class MockScanProvider;
class ScanWorkflow;

class ScanPropertyPageProvider final : public Core::PropertyPageProvider
{
    Q_OBJECT

public:
    ScanPropertyPageProvider(
        MockScanProvider *scanProvider, ScanWorkflow *workflow, QObject *parent = nullptr);

    QList<Core::PropertyPageDescriptor> pages(
        const Core::PropertyPageContext &context) const final;
    QWidget *createPage(Utils::Id pageId, QWidget *parent) final;
    void updatePage(
        Utils::Id pageId, QWidget *page, const Core::PropertyPageContext &context) final;

private:
    QPointer<MockScanProvider> m_scanProvider;
    QPointer<ScanWorkflow> m_workflow;
};

} // namespace EtherCAT::Scan::Internal

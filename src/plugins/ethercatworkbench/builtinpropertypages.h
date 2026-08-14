// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QPointer>

namespace EtherCAT::Core {
class SemanticRuntimeService;
}

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class BuiltinPropertyPageProvider final : public Core::PropertyPageProvider
{
    Q_OBJECT

public:
    explicit BuiltinPropertyPageProvider(
        WorkbenchController *controller,
        QObject *parent = nullptr,
        Core::SemanticRuntimeService *runtimeService = nullptr);

    QList<Core::PropertyPageDescriptor> pages(
        const Core::PropertyPageContext &context) const final;
    QWidget *createPage(Utils::Id pageId, QWidget *parent) final;
    void updatePage(
        Utils::Id pageId, QWidget *page, const Core::PropertyPageContext &context) final;

private:
    QPointer<WorkbenchController> m_controller;
    QPointer<Core::SemanticRuntimeService> m_runtimeService;
};

} // namespace EtherCAT::Workbench::Internal

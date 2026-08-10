// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QHash>
#include <QList>
#include <QPointer>
#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace Utils {
class InfoLabel;
}

namespace EtherCAT::Workbench::Internal {

class WorkbenchController;

class DeviceParametersPage final : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceParametersPage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    struct ParameterRow
    {
        Data::DeviceParameterDefinition definition;
        QPointer<QWidget> editor;
        QTreeWidgetItem *item = nullptr;
    };

    struct CandidateConfiguration
    {
        Data::DeviceParameterConfiguration configuration;
        QString firstError;
        QHash<QString, QString> parameterErrors;

        bool valid() const { return firstError.isEmpty(); }
    };

    void clearBaseline(const QString &summary, const QString &detail);
    void rebuildRows();
    void updateDraftPresentation();
    CandidateConfiguration candidateConfiguration() const;
    bool hasEditorDraft() const;
    void markDraftStale(const Core::PropertyPageContext &context, const QString &reason);
    void reloadFromProject();
    void applyConfiguration();

    WorkbenchController *m_controller = nullptr;
    Core::PropertyPageContext m_context;
    std::optional<Data::ProjectSnapshot> m_baselineProject;
    std::optional<Data::DeviceAdapterManifest> m_baselineManifest;
    QPointer<Core::DeviceAdapterProvider> m_baselineProvider;
    Data::NodeId m_baselineSlaveId;
    QList<ParameterRow> m_rows;
    bool m_rebuilding = false;
    bool m_stale = false;
    bool m_forceReload = false;
    bool m_applyInProgress = false;

    QLabel *m_summary = nullptr;
    Utils::InfoLabel *m_feedback = nullptr;
    QTreeWidget *m_table = nullptr;
    QPushButton *m_reload = nullptr;
    QPushButton *m_apply = nullptr;
};

} // namespace EtherCAT::Workbench::Internal

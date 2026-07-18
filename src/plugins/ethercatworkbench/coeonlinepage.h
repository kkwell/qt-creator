// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatcore/providers.h>

#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeView;
QT_END_NAMESPACE

namespace Utils {
class FancyLineEdit;
class InfoLabel;
} // namespace Utils

namespace EtherCAT::Workbench::Internal {

class CoeFilterModel;
class CoeObjectModel;
class WorkbenchController;

class CoeOnlinePage final : public QWidget
{
    Q_OBJECT

public:
    explicit CoeOnlinePage(WorkbenchController *controller, QWidget *parent = nullptr);

    void setContext(const Core::PropertyPageContext &context);

private:
    void rebuildObjects();
    void updateList();
    void updateButtonState();
    void refreshFilterResults();
    void updateFilterState();
    void ensureDictionarySelection();
    void clearFilters();
    void showAdvancedSettings();
    void addSelectedToStartup();

    WorkbenchController *m_controller = nullptr;
    Core::PropertyPageContext m_context;
    int m_mockGeneration = 0;

    Utils::InfoLabel *m_banner = nullptr;
    Utils::InfoLabel *m_feedback = nullptr;
    QPushButton *m_updateList = nullptr;
    QPushButton *m_advanced = nullptr;
    QPushButton *m_addToStartup = nullptr;
    QCheckBox *m_autoUpdate = nullptr;
    QCheckBox *m_singleUpdate = nullptr;
    QCheckBox *m_showOffline = nullptr;
    QLabel *m_dataSource = nullptr;
    QLineEdit *m_moduleOd = nullptr;
    Utils::FancyLineEdit *m_filter = nullptr;
    QStackedWidget *m_dictionaryStack = nullptr;
    QTreeView *m_dictionary = nullptr;
    QWidget *m_filterEmptyState = nullptr;
    QPushButton *m_clearFilters = nullptr;
    CoeObjectModel *m_model = nullptr;
    CoeFilterModel *m_filterModel = nullptr;
    std::optional<quint32> m_selectedObjectAddress;
    int m_filterResultChangeDepth = 0;
};

} // namespace EtherCAT::Workbench::Internal

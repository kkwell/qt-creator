// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/devicedescription.h>

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTreeView;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class EsiDeviceSelectionDialog final : public QDialog
{
public:
    explicit EsiDeviceSelectionDialog(
        const QList<Data::DeviceSummary> &devices, QWidget *parent = nullptr);

    Data::NodeId selectedDeviceId() const;

private:
    void selectFirstVisibleDevice();
    void updateSelection();
    void accept() final;

    QStandardItemModel *m_model;
    QSortFilterProxyModel *m_proxyModel;
    QLineEdit *m_filter;
    QCheckBox *m_extendedInformation;
    QCheckBox *m_showPrevious;
    QTreeView *m_tree;
    QLabel *m_status;
    QDialogButtonBox *m_buttons;
    QPushButton *m_addButton;
    Data::NodeId m_selectedDeviceId;
    bool m_selectedDeviceSupported = false;
};

} // namespace EtherCAT::Workbench::Internal

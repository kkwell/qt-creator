// Copyright (C) 2026 Kvell

#pragma once

#include <ethercatdata/devicedescription.h>

#include <QWidget>

#include <optional>

QT_BEGIN_NAMESPACE
class QGroupBox;
class QLabel;
class QLineEdit;
class QScrollArea;
QT_END_NAMESPACE

namespace EtherCAT::Workbench::Internal {

class EsiDeviceGeneralPage final : public QWidget
{
public:
    explicit EsiDeviceGeneralPage(QWidget *parent = nullptr);

    void setDevice(
        const std::optional<Data::DeviceDescription> &device, const QString &displayName);

private:
    void clear();

    QLabel *m_description;
    QLabel *m_unavailable;
    QScrollArea *m_scrollArea;
    QWidget *m_details;
    QGroupBox *m_identity;
    QLineEdit *m_name;
    QLineEdit *m_type;
    QLineEdit *m_objectId;
    QLineEdit *m_vendor;
    QLineEdit *m_product;
    QLineEdit *m_revision;
    QLineEdit *m_group;
    QGroupBox *m_capabilities;
    QLineEdit *m_syncManagers;
    QLineEdit *m_rxPdos;
    QLineEdit *m_txPdos;
    QLineEdit *m_coe;
    QLabel *m_coeDetails;
    QLineEdit *m_startup;
    QLineEdit *m_dcModes;
    QGroupBox *m_qualification;
    QLineEdit *m_support;
    QLineEdit *m_warningCount;
    QLineEdit *m_unsupportedCount;
    QLabel *m_warningDetails;
    QLabel *m_unsupportedDetails;
    QGroupBox *m_source;
    QLineEdit *m_sourceFile;
    QLineEdit *m_sourceHash;
    QLineEdit *m_imported;
};

} // namespace EtherCAT::Workbench::Internal

// Copyright (C) 2026 Kvell

#include "esidevicegeneralpage.h"

#include "ethercatworkbenchtr.h"

#include <utils/stylehelper.h>

#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

namespace EtherCAT::Workbench::Internal {

static QLineEdit *readOnlyField(
    QWidget *parent, const char *objectName, const QString &accessibleName)
{
    auto field = new QLineEdit(parent);
    field->setObjectName(objectName);
    field->setAccessibleName(accessibleName);
    field->setReadOnly(true);
    return field;
}

static void configureForm(QFormLayout *layout)
{
    layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    layout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    layout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVS);
}

static QString hexValue(quint64 value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0'));
}

static QString yesNo(bool value)
{
    return value ? Tr::tr("Yes") : Tr::tr("No");
}

static QString pdoSummary(const QList<Data::PdoDescription> &pdos)
{
    int entryCount = 0;
    int bitCount = 0;
    for (const Data::PdoDescription &pdo : pdos) {
        entryCount += pdo.entries.size();
        for (const Data::PdoEntryDescription &entry : pdo.entries)
            bitCount += entry.bitLength;
    }
    const QString pdoCount = pdos.size() == 1 ? Tr::tr("1 PDO")
                                               : Tr::tr("%1 PDOs").arg(pdos.size());
    const QString entries = entryCount == 1 ? Tr::tr("1 entry")
                                             : Tr::tr("%1 entries").arg(entryCount);
    return Tr::tr("%1, %2, %3 bits").arg(pdoCount, entries).arg(bitCount);
}

static QString textOrNotRecorded(const QString &text)
{
    return text.isEmpty() ? Tr::tr("Not recorded") : text;
}

static void configureDetailsLabel(QLabel *label, const QString &accessibleName)
{
    label->setAccessibleName(accessibleName);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
}

EsiDeviceGeneralPage::EsiDeviceGeneralPage(QWidget *parent)
    : QWidget(parent)
    , m_description(new QLabel(this))
    , m_unavailable(new QLabel(this))
    , m_scrollArea(new QScrollArea(this))
    , m_details(new QWidget(m_scrollArea))
    , m_identity(new QGroupBox(Tr::tr("Device identity"), m_details))
    , m_name(readOnlyField(m_identity, "EtherCATEsiDeviceGeneralName", Tr::tr("ESI device name")))
    , m_type(readOnlyField(m_identity, "EtherCATEsiDeviceGeneralType", Tr::tr("ESI device type")))
    , m_objectId(readOnlyField(
          m_identity, "EtherCATEsiDeviceGeneralObjectId", Tr::tr("Stable ESI device object ID")))
    , m_vendor(readOnlyField(
          m_identity, "EtherCATEsiDeviceGeneralVendor", Tr::tr("ESI vendor ID")))
    , m_product(readOnlyField(
          m_identity, "EtherCATEsiDeviceGeneralProduct", Tr::tr("ESI product code")))
    , m_revision(readOnlyField(
          m_identity, "EtherCATEsiDeviceGeneralRevision", Tr::tr("ESI revision number")))
    , m_group(readOnlyField(
          m_identity, "EtherCATEsiDeviceGeneralGroup", Tr::tr("ESI device group")))
    , m_capabilities(new QGroupBox(Tr::tr("Offline configuration coverage"), m_details))
    , m_syncManagers(readOnlyField(
          m_capabilities,
          "EtherCATEsiDeviceGeneralSyncManagers",
          Tr::tr("ESI SyncManager count")))
    , m_rxPdos(readOnlyField(
          m_capabilities, "EtherCATEsiDeviceGeneralRxPdos", Tr::tr("ESI RxPDO summary")))
    , m_txPdos(readOnlyField(
          m_capabilities, "EtherCATEsiDeviceGeneralTxPdos", Tr::tr("ESI TxPDO summary")))
    , m_coe(readOnlyField(
          m_capabilities, "EtherCATEsiDeviceGeneralCoe", Tr::tr("ESI CoE support")))
    , m_coeDetails(new QLabel(m_capabilities))
    , m_startup(readOnlyField(
          m_capabilities,
          "EtherCATEsiDeviceGeneralStartup",
          Tr::tr("ESI Startup parameter count")))
    , m_dcModes(readOnlyField(
          m_capabilities, "EtherCATEsiDeviceGeneralDcModes", Tr::tr("ESI DC mode count")))
    , m_qualification(new QGroupBox(Tr::tr("Import qualification"), m_details))
    , m_support(readOnlyField(
          m_qualification,
          "EtherCATEsiDeviceGeneralSupport",
          Tr::tr("ESI configuration support level")))
    , m_warningCount(readOnlyField(
          m_qualification,
          "EtherCATEsiDeviceGeneralWarningCount",
          Tr::tr("ESI parse warning count")))
    , m_unsupportedCount(readOnlyField(
          m_qualification,
          "EtherCATEsiDeviceGeneralUnsupportedCount",
          Tr::tr("Unsupported ESI feature count")))
    , m_warningDetails(new QLabel(m_qualification))
    , m_unsupportedDetails(new QLabel(m_qualification))
    , m_source(new QGroupBox(Tr::tr("ESI source"), m_details))
    , m_sourceFile(readOnlyField(
          m_source, "EtherCATEsiDeviceGeneralSourceFile", Tr::tr("ESI source file")))
    , m_sourceHash(readOnlyField(
          m_source, "EtherCATEsiDeviceGeneralSourceHash", Tr::tr("ESI source SHA-256")))
    , m_imported(readOnlyField(
          m_source, "EtherCATEsiDeviceGeneralImported", Tr::tr("ESI import time")))
{
    setObjectName("EtherCATEsiDeviceGeneralContent");
    setAccessibleName(Tr::tr("ESI device General page"));

    m_description->setObjectName("EtherCATEsiDeviceGeneralDescription");
    m_description->setText(
        Tr::tr("Read-only ESI catalogue entry. Identity, offline configuration coverage, and "
               "qualification come from the imported XML description; no controller is "
               "accessed."));
    m_description->setWordWrap(true);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_description->setAccessibleName(Tr::tr("ESI device General page description"));

    m_unavailable->setObjectName("EtherCATEsiDeviceGeneralUnavailable");
    m_unavailable->setAlignment(Qt::AlignCenter);
    m_unavailable->setWordWrap(true);
    m_unavailable->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_unavailable->setAccessibleName(Tr::tr("ESI device description availability"));

    m_scrollArea->setObjectName("EtherCATEsiDeviceGeneralScrollArea");
    m_scrollArea->setAccessibleName(Tr::tr("ESI device General details"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setWidget(m_details);
    m_details->setObjectName("EtherCATEsiDeviceGeneralDetails");

    m_identity->setObjectName("EtherCATEsiDeviceGeneralIdentity");
    auto identityForm = new QFormLayout(m_identity);
    configureForm(identityForm);
    identityForm->addRow(Tr::tr("Name:"), m_name);
    identityForm->addRow(Tr::tr("Type:"), m_type);
    identityForm->addRow(Tr::tr("Object Id:"), m_objectId);
    identityForm->addRow(Tr::tr("Vendor ID:"), m_vendor);
    identityForm->addRow(Tr::tr("Product Code:"), m_product);
    identityForm->addRow(Tr::tr("Revision:"), m_revision);
    identityForm->addRow(Tr::tr("Group:"), m_group);

    m_capabilities->setObjectName("EtherCATEsiDeviceGeneralCapabilities");
    m_coeDetails->setObjectName("EtherCATEsiDeviceGeneralCoeDetails");
    configureDetailsLabel(m_coeDetails, Tr::tr("ESI CoE capability details"));
    auto capabilitiesForm = new QFormLayout(m_capabilities);
    configureForm(capabilitiesForm);
    capabilitiesForm->addRow(Tr::tr("Sync Managers:"), m_syncManagers);
    capabilitiesForm->addRow(Tr::tr("RxPDOs:"), m_rxPdos);
    capabilitiesForm->addRow(Tr::tr("TxPDOs:"), m_txPdos);
    capabilitiesForm->addRow(Tr::tr("CoE:"), m_coe);
    capabilitiesForm->addRow(Tr::tr("CoE features:"), m_coeDetails);
    capabilitiesForm->addRow(Tr::tr("Startup parameters:"), m_startup);
    capabilitiesForm->addRow(Tr::tr("DC modes:"), m_dcModes);

    m_qualification->setObjectName("EtherCATEsiDeviceGeneralQualification");
    m_warningDetails->setObjectName("EtherCATEsiDeviceGeneralWarningDetails");
    m_unsupportedDetails->setObjectName("EtherCATEsiDeviceGeneralUnsupportedDetails");
    configureDetailsLabel(m_warningDetails, Tr::tr("ESI parse warning details"));
    configureDetailsLabel(
        m_unsupportedDetails, Tr::tr("Unsupported ESI feature details"));
    auto qualificationForm = new QFormLayout(m_qualification);
    configureForm(qualificationForm);
    qualificationForm->addRow(Tr::tr("Support:"), m_support);
    qualificationForm->addRow(Tr::tr("Warnings:"), m_warningCount);
    qualificationForm->addRow(Tr::tr("Warning details:"), m_warningDetails);
    qualificationForm->addRow(Tr::tr("Unsupported features:"), m_unsupportedCount);
    qualificationForm->addRow(Tr::tr("Unsupported details:"), m_unsupportedDetails);

    m_source->setObjectName("EtherCATEsiDeviceGeneralSource");
    m_sourceFile->setToolTip(Tr::tr("Original import path recorded by the local repository."));
    m_sourceHash->setToolTip(
        Tr::tr("SHA-256 of the preserved source XML used for integrity checking."));
    auto sourceForm = new QFormLayout(m_source);
    configureForm(sourceForm);
    sourceForm->addRow(Tr::tr("Source file:"), m_sourceFile);
    sourceForm->addRow(Tr::tr("SHA-256:"), m_sourceHash);
    sourceForm->addRow(Tr::tr("Imported:"), m_imported);

    auto detailsLayout = new QVBoxLayout(m_details);
    detailsLayout->setContentsMargins(QMargins());
    detailsLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    detailsLayout->addWidget(m_identity);
    detailsLayout->addWidget(m_capabilities);
    detailsLayout->addWidget(m_qualification);
    detailsLayout->addWidget(m_source);
    detailsLayout->addStretch(1);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(QMargins());
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_description);
    layout->addWidget(m_unavailable, 1);
    layout->addWidget(m_scrollArea, 1);

    clear();
}

void EsiDeviceGeneralPage::setDevice(
    const std::optional<Data::DeviceDescription> &device, const QString &displayName)
{
    clear();
    if (!device) {
        m_unavailable->setText(
            displayName.isEmpty()
                ? Tr::tr("The selected ESI device description is unavailable.")
                : Tr::tr("The ESI device description for %1 is unavailable.").arg(displayName));
        m_unavailable->show();
        return;
    }

    m_name->setText(textOrNotRecorded(device->summary.name));
    m_type->setText(textOrNotRecorded(device->summary.typeName));
    m_objectId->setText(device->summary.id.toString());
    m_vendor->setText(hexValue(device->summary.identity.vendorId, 8));
    m_product->setText(hexValue(device->summary.identity.productCode, 8));
    m_revision->setText(hexValue(device->summary.identity.revisionNumber, 8));
    m_group->setText(textOrNotRecorded(device->summary.group));

    m_syncManagers->setText(QString::number(device->syncManagers.size()));
    m_rxPdos->setText(pdoSummary(device->rxPdos));
    m_txPdos->setText(pdoSummary(device->txPdos));
    m_coe->setText(device->coe.supported ? Tr::tr("Supported") : Tr::tr("Not declared"));
    m_coeDetails->setText(
        Tr::tr("SDO Info: %1 · PDO Assignment: %2 · PDO Configuration: %3 · Complete Access: %4")
            .arg(yesNo(device->coe.sdoInfo))
            .arg(yesNo(device->coe.pdoAssign))
            .arg(yesNo(device->coe.pdoConfiguration))
            .arg(yesNo(device->coe.completeAccess)));
    m_startup->setText(QString::number(device->startupParameters.size()));
    m_dcModes->setText(QString::number(device->dcModes.size()));

    m_support->setText(device->summary.supported ? Tr::tr("Supported") : Tr::tr("Limited"));
    m_warningCount->setText(QString::number(device->warnings.size()));
    m_unsupportedCount->setText(QString::number(device->unsupportedFeatures.size()));
    m_warningDetails->setText(
        device->warnings.isEmpty() ? Tr::tr("None") : device->warnings.join('\n'));
    m_unsupportedDetails->setText(
        device->unsupportedFeatures.isEmpty() ? Tr::tr("None")
                                              : device->unsupportedFeatures.join('\n'));

    m_sourceFile->setText(textOrNotRecorded(device->sourcePath));
    m_sourceHash->setText(
        device->sourceSha256.isEmpty() ? Tr::tr("Not recorded")
                                       : QString::fromLatin1(device->sourceSha256.toHex()));
    m_imported->setText(
        device->importedAt.isValid()
            ? device->importedAt.toLocalTime().toString(Qt::ISODate)
            : Tr::tr("Not recorded"));

    m_scrollArea->show();
}

void EsiDeviceGeneralPage::clear()
{
    for (QLineEdit *field :
         {m_name,
          m_type,
          m_objectId,
          m_vendor,
          m_product,
          m_revision,
          m_group,
          m_syncManagers,
          m_rxPdos,
          m_txPdos,
          m_coe,
          m_startup,
          m_dcModes,
          m_support,
          m_warningCount,
          m_unsupportedCount,
          m_sourceFile,
          m_sourceHash,
          m_imported}) {
        field->clear();
    }
    m_coeDetails->clear();
    m_warningDetails->clear();
    m_unsupportedDetails->clear();
    m_unavailable->clear();
    m_unavailable->hide();
    m_scrollArea->hide();
}

} // namespace EtherCAT::Workbench::Internal

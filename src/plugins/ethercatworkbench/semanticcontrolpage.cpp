// Copyright (C) 2026 Embed Labs

#include "semanticcontrolpage.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <ethercatcore/semanticruntimeservice.h>
#include <ethercatcore/manualcontrolcontract.h>

#include <utils/stylehelper.h>

#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace EtherCAT::Workbench::Internal {

static QString signalAvailabilityName(Data::SemanticSignalAvailability availability)
{
    using Availability = Data::SemanticSignalAvailability;
    switch (availability) {
    case Availability::Unavailable:
        return Tr::tr("Unavailable");
    case Availability::Unverified:
        return Tr::tr("Unverified");
    case Availability::Stale:
        return Tr::tr("Stale");
    case Availability::Ready:
        return Tr::tr("Ready");
    case Availability::Rejected:
        return Tr::tr("Rejected");
    }
    return Tr::tr("Unavailable");
}

static QString qualityName(Data::RuntimeResourceQualityState quality)
{
    using Quality = Data::RuntimeResourceQualityState;
    switch (quality) {
    case Quality::Unknown:
        return Tr::tr("Unknown");
    case Quality::Good:
        return Tr::tr("Good");
    case Quality::Uncertain:
        return Tr::tr("Uncertain");
    case Quality::Bad:
        return Tr::tr("Bad");
    case Quality::Stale:
        return Tr::tr("Stale");
    case Quality::Unavailable:
        return Tr::tr("Unavailable");
    }
    return Tr::tr("Unknown");
}

static QString engineeringValueText(const Data::EngineeringValue &value)
{
    using Kind = Data::EngineeringValueKind;
    switch (value.kind) {
    case Kind::Boolean:
        return value.boolean ? Tr::tr("On") : Tr::tr("Off");
    case Kind::SignedInteger:
        return QString::number(value.signedInteger);
    case Kind::UnsignedInteger:
        return QString::number(value.unsignedInteger);
    case Kind::ExactRational:
        return value.rational.denominator == 1
                   ? QString::number(value.rational.numerator)
                   : QString("%1/%2")
                         .arg(value.rational.numerator)
                         .arg(value.rational.denominator);
    case Kind::Enumeration:
        return value.enumerationName;
    case Kind::Invalid:
        return Tr::tr("Unavailable");
    }
    return Tr::tr("Unavailable");
}

static std::optional<Data::EngineeringValue> rawEngineeringValue(
    const Data::RuntimeResourceTypedValue &typedValue)
{
    if (!typedValue.value.isValid())
        return std::nullopt;

    switch (typedValue.primitiveType) {
    case Data::RuntimeResourcePrimitiveType::Boolean:
        if (typedValue.value.metaType().id() == QMetaType::Bool)
            return Data::EngineeringValue::fromBoolean(typedValue.value.toBool());
        return std::nullopt;
    case Data::RuntimeResourcePrimitiveType::SignedInteger:
        if (typedValue.value.metaType().id() == QMetaType::LongLong
            || typedValue.value.metaType().id() == QMetaType::Int) {
            return Data::EngineeringValue::fromSignedInteger(typedValue.value.toLongLong());
        }
        return std::nullopt;
    case Data::RuntimeResourcePrimitiveType::UnsignedInteger:
        if (typedValue.value.metaType().id() == QMetaType::ULongLong
            || typedValue.value.metaType().id() == QMetaType::UInt) {
            return Data::EngineeringValue::fromUnsignedInteger(typedValue.value.toULongLong());
        }
        return std::nullopt;
    case Data::RuntimeResourcePrimitiveType::Opaque:
    case Data::RuntimeResourcePrimitiveType::FloatingPoint:
    case Data::RuntimeResourcePrimitiveType::Text:
    case Data::RuntimeResourcePrimitiveType::ByteArray:
        return std::nullopt;
    }
    return std::nullopt;
}

struct DisplayValue
{
    QString value;
    QString unit;
    bool converted = false;
};

static DisplayValue signalDisplayValue(const Data::SemanticSignalRuntimeState &state)
{
    if (state.availability != Data::SemanticSignalAvailability::Ready
        || !state.snapshotComplete || state.captureCycle == 0
        || state.controllerTimestampNs == 0
        || state.quality.state != Data::RuntimeResourceQualityState::Good || !state.value
        || !state.value->value.isValid() || !state.binding
        || state.value->primitiveType != state.binding->primitiveType
        || state.value->typeIdentity != state.binding->valueTypeIdentity
        || !Core::isSemanticRuntimeValueCompatible(
            state.value->value, *state.binding)) {
        return {Tr::tr("Unavailable"), {}, false};
    }

    if (!state.definition.engineeringTransform) {
        const QVariant &value = state.value->value;
        QString raw;
        if (value.metaType().id() == QMetaType::Bool)
            raw = value.toBool() ? Tr::tr("On") : Tr::tr("Off");
        else if (value.metaType().id() == QMetaType::QByteArray)
            raw = QString::fromLatin1(value.toByteArray().toHex());
        else
            raw = value.toString();
        return {
            raw.isEmpty() ? Tr::tr("Unavailable") : Tr::tr("Raw: %1").arg(raw),
            {},
            !raw.isEmpty(),
        };
    }

    const std::optional<Data::EngineeringValue> raw = rawEngineeringValue(*state.value);
    if (!raw)
        return {Tr::tr("Unavailable"), {}, false};
    const Core::EngineeringConversionResult conversion = Core::convertRawToEngineering(
        *raw, state.binding->bitWidth, *state.definition.engineeringTransform);
    if (!conversion.validation.accepted() || !conversion.value)
        return {Tr::tr("Unavailable"), {}, false};
    return {
        engineeringValueText(*conversion.value),
        state.definition.engineeringTransform->unit,
        true,
    };
}

static QString compactRuntimeContextReason(const Data::SemanticRuntimeContext &context)
{
    const QString detail = context.detail.trimmed();
    if (detail.startsWith("controller-provider-unavailable:"))
        return Tr::tr("Controller provider is unavailable.");
    if (detail.startsWith("controller-provider-ambiguous:"))
        return Tr::tr("Controller provider selection is ambiguous.");
    if (detail.startsWith("controller-session-unavailable:"))
        return Tr::tr("Controller session is unavailable.");
    if (detail.startsWith("semantic-binding-artifact-missing:"))
        return Tr::tr("Semantic binding artifact is missing.");
    if (detail.startsWith("semantic-binding-artifact-invalid:"))
        return Tr::tr("Semantic binding artifact is invalid.");
    if (detail.startsWith("runtime-resources-unsupported:"))
        return Tr::tr("Runtime resources are not supported.");
    if (detail.startsWith("runtime-resource-catalog-unavailable:"))
        return Tr::tr("Runtime resource catalog is unavailable.");
    if (detail.startsWith("runtime-resource-catalog-stale:"))
        return Tr::tr("Runtime resource catalog is stale.");
    if (detail.startsWith("runtime-resource-catalog-empty:"))
        return Tr::tr("Runtime resource catalog is empty.");
    if (detail.startsWith("runtime-resource-epoch-incomplete:"))
        return Tr::tr("Runtime resource epoch is incomplete.");
    if (detail.startsWith("semantic-binding-proof-unavailable:"))
        return Tr::tr("Signed semantic binding proof is unavailable.");

    return context.bindingVerification.state
                       == Data::SemanticBindingVerificationState::Verified
                   ? Tr::tr("Runtime context is incomplete.")
                   : Tr::tr("Runtime binding is not verified.");
}

static bool contextIsVerifiedAndComplete(const Data::SemanticRuntimeContext &context)
{
    return context.complete && !context.controllerId.isEmpty() && context.sessionGeneration != 0
           && Core::isCompleteRuntimeResourceCatalogEpoch(context.epoch)
           && Core::isCanonicalSha256Digest(context.mappingDigest)
           && Core::isCanonicalSha256Digest(context.controllerMappingDigest)
           && context.mappingDigest == context.controllerMappingDigest
           && context.contextHash.size() == 32
           && context.bindingVerification.state
                  == Data::SemanticBindingVerificationState::Verified
           && !context.bindingVerification.verifierId.isEmpty()
           && Core::isCanonicalSha256Digest(
               context.bindingVerification.signedManifestDigest);
}

static bool signalBindingMatchesContext(
    const Data::SemanticSignalRuntimeState &state,
    const Data::SemanticRuntimeContext &context)
{
    if (!state.binding)
        return false;
    const Data::SemanticRuntimeBinding &binding = *state.binding;
    return state.definition.id == state.target.signalId && binding.target == state.target
           && Core::validateSemanticRuntimeBinding(binding).accepted()
           && binding.verification == context.bindingVerification
           && binding.sessionGeneration == context.sessionGeneration
           && binding.epoch == context.epoch && binding.mappingDigest == context.mappingDigest
           && binding.controllerMappingDigest == context.controllerMappingDigest;
}

SemanticControlPage::SemanticControlPage(
    WorkbenchController *controller,
    Core::SemanticRuntimeService *runtimeService,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_runtimeService(runtimeService)
    , m_status(new QLabel(this))
    , m_signals(new QTreeWidget(this))
    , m_manualControl(new QGroupBox(Tr::tr("Manual control"), this))
    , m_requestedValue(new QLineEdit(m_manualControl))
    , m_apply(new QPushButton(Tr::tr("Apply"), m_manualControl))
{
    setProperty("EtherCAT.Workbench.SemanticControlPage", true);

    m_status->setObjectName("EtherCATSemanticControlStatus");
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setAccessibleName(Tr::tr("Semantic control status"));

    m_signals->setObjectName("EtherCATSemanticControlSignals");
    m_signals->setColumnCount(6);
    m_signals->setHeaderLabels(
        {Tr::tr("Signal"),
         Tr::tr("Value"),
         Tr::tr("Unit"),
         Tr::tr("Quality"),
         Tr::tr("Cycle"),
         Tr::tr("Availability")});
    m_signals->setAlternatingRowColors(true);
    m_signals->setRootIsDecorated(false);
    m_signals->setUniformRowHeights(true);
    m_signals->setSelectionMode(QAbstractItemView::NoSelection);
    m_signals->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_signals->header()->setStretchLastSection(false);
    m_signals->setAccessibleName(Tr::tr("Semantic signal values"));

    m_requestedValue->setObjectName("EtherCATSemanticControlRequestedValue");
    m_requestedValue->setPlaceholderText(Tr::tr("Output transaction is unavailable"));
    m_requestedValue->setAccessibleName(Tr::tr("Requested value"));
    m_apply->setObjectName("EtherCATSemanticControlApply");
    m_apply->setAccessibleName(Tr::tr("Apply semantic value"));

    auto controlLayout = new QFormLayout(m_manualControl);
    controlLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    controlLayout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    controlLayout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    controlLayout->addRow(Tr::tr("Requested value"), m_requestedValue);
    controlLayout->addRow(QString(), m_apply);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_status);
    layout->addWidget(m_signals, 1);
    layout->addWidget(m_manualControl);

    m_manualControl->setEnabled(false);
    m_requestedValue->setEnabled(false);
    m_apply->setEnabled(false);

    if (m_runtimeService) {
        connect(
            m_runtimeService,
            &Core::SemanticRuntimeService::contextsChanged,
            this,
            &SemanticControlPage::refresh);
        connect(m_runtimeService, &QObject::destroyed, this, &SemanticControlPage::refresh);
    }
    refresh();
}

void SemanticControlPage::setContext(const Core::PropertyPageContext &context)
{
    if (m_context == context)
        return;
    m_context = context;
    refresh();
}

void SemanticControlPage::refresh()
{
    m_signals->clear();
    m_signals->setVisible(false);
    m_manualControl->setEnabled(false);
    m_requestedValue->setEnabled(false);
    m_apply->setEnabled(false);

    const std::optional<SemanticControlSelection> selection
        = m_controller && m_controller->treeModel()
              ? m_controller->treeModel()->semanticControlSelection(m_context.nodeId)
              : std::nullopt;
    if (!selection) {
        m_status->setText(Tr::tr("Control is unavailable for this selection."));
        return;
    }
    if (!m_runtimeService) {
        m_status->setText(Tr::tr("Runtime data is unavailable."));
        return;
    }

    QList<Data::SemanticRuntimeContext> scopeContexts;
    const QList<Data::SemanticRuntimeContext> contexts = m_runtimeService->contexts();
    std::copy_if(
        contexts.cbegin(),
        contexts.cend(),
        std::back_inserter(scopeContexts),
        [&selection](const Data::SemanticRuntimeContext &context) {
            return context.scope == selection->scope;
        });
    if (scopeContexts.isEmpty()) {
        m_status->setText(Tr::tr("Runtime context is unavailable."));
        return;
    }
    if (scopeContexts.size() != 1) {
        m_status->setText(Tr::tr("Runtime context is ambiguous."));
        return;
    }

    const Data::SemanticRuntimeContext &runtimeContext = scopeContexts.constFirst();
    if (!contextIsVerifiedAndComplete(runtimeContext)) {
        m_status->setText(compactRuntimeContextReason(runtimeContext));
        return;
    }

    if (!selection->signalIds.isEmpty()) {
        m_status->setText(Tr::tr("Runtime signal binding is not verified."));
        return;
    }

    QList<Data::SemanticSignalRuntimeState> states;
    std::copy_if(
        runtimeContext.signalStates.cbegin(),
        runtimeContext.signalStates.cend(),
        std::back_inserter(states),
        [&runtimeContext, &selection](const Data::SemanticSignalRuntimeState &state) {
            return state.target.controllerId == runtimeContext.controllerId
                   && state.target.scope == selection->scope
                   && state.target.deviceId == selection->deviceId
                   && state.target.kind == Data::SemanticRuntimeTargetKind::Signal
                   && state.target.actionId.value.isEmpty();
        });
    std::sort(
        states.begin(),
        states.end(),
        [](const Data::SemanticSignalRuntimeState &left,
           const Data::SemanticSignalRuntimeState &right) {
            return left.target.signalId.value < right.target.signalId.value;
        });
    const bool invalidSignalSet
        = states.isEmpty()
          || std::any_of(
              states.cbegin(),
              states.cend(),
              [](const Data::SemanticSignalRuntimeState &state) {
                  return state.target.signalId.value.isEmpty()
                         || state.target.signalId.value != state.target.signalId.value.trimmed();
              })
          || std::adjacent_find(
                 states.cbegin(),
                 states.cend(),
                 [](const Data::SemanticSignalRuntimeState &left,
                    const Data::SemanticSignalRuntimeState &right) {
                     return left.target.signalId == right.target.signalId;
                 })
                 != states.cend();
    if (invalidSignalSet) {
        m_status->setText(Tr::tr("Runtime signal set is incomplete."));
        return;
    }

    for (const Data::SemanticSignalRuntimeState &state : std::as_const(states)) {
        if (!signalBindingMatchesContext(state, runtimeContext)) {
            m_status->setText(Tr::tr("Runtime signal binding is not verified."));
            return;
        }
    }

    bool allReady = true;
    for (const Data::SemanticSignalRuntimeState &state : std::as_const(states)) {
        const DisplayValue displayValue = signalDisplayValue(state);
        allReady = allReady && state.availability == Data::SemanticSignalAvailability::Ready
                   && state.snapshotComplete
                   && state.quality.state == Data::RuntimeResourceQualityState::Good
                   && state.captureCycle != 0 && displayValue.converted;
        const QString displayName = state.definition.displayName.trimmed().isEmpty()
                                        ? Tr::tr("Signal")
                                        : state.definition.displayName.trimmed();
        auto item = new QTreeWidgetItem(
            {displayName,
             displayValue.value,
             displayValue.unit,
             qualityName(state.quality.state),
             QString::number(state.captureCycle),
             displayValue.converted
                 ? signalAvailabilityName(state.availability)
                 : Tr::tr("Unavailable")});
        item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
        m_signals->addTopLevelItem(item);
    }

    m_signals->setVisible(true);
    m_status->setText(
        allReady
            ? Tr::tr("Live values are available. Manual output is not enabled yet.")
            : Tr::tr("Some live values are unavailable. Manual output is disabled."));
}

} // namespace EtherCAT::Workbench::Internal

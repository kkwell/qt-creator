// Copyright (C) 2026 Embed Labs

#include "semanticcontrolpage.h"

#include "ethercatworkbenchtr.h"
#include "workbenchcontroller.h"
#include "workbenchtreemodel.h"

#include <ethercatcore/manualcontrolcontract.h>
#include <ethercatcore/semanticruntimeservice.h>

#include <utils/stylehelper.h>

#include <QCheckBox>
#include <QCryptographicHash>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

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

enum class LiveSignalPresentationError { None, Incomplete, BindingUnverified };

struct LiveSignalPresentation
{
    QList<Data::SemanticSignalRuntimeState> states;
    bool requiresWholeDeviceControl = false;
    LiveSignalPresentationError error = LiveSignalPresentationError::None;
};

static LiveSignalPresentation liveSignalPresentation(
    const Data::SemanticRuntimeContext &runtimeContext, const SemanticControlSelection &selection)
{
    LiveSignalPresentation presentation;
    QSet<QString> selectedSignalIds;
    for (const Data::SemanticSignalId &signalId : selection.signalIds)
        selectedSignalIds.insert(signalId.value);

    std::copy_if(
        runtimeContext.signalStates.cbegin(),
        runtimeContext.signalStates.cend(),
        std::back_inserter(presentation.states),
        [&runtimeContext, &selection, &selectedSignalIds](
            const Data::SemanticSignalRuntimeState &state) {
            return state.target.controllerId == runtimeContext.controllerId
                   && state.target.scope == selection.scope
                   && state.target.deviceId == selection.deviceId
                   && state.target.kind == Data::SemanticRuntimeTargetKind::Signal
                   && state.target.actionId.value.isEmpty()
                   && (selectedSignalIds.isEmpty()
                       || selectedSignalIds.contains(state.target.signalId.value));
        });
    std::sort(
        presentation.states.begin(),
        presentation.states.end(),
        [](const Data::SemanticSignalRuntimeState &left,
           const Data::SemanticSignalRuntimeState &right) {
            return left.target.signalId.value < right.target.signalId.value;
        });
    const bool invalidSignalSet = presentation.states.isEmpty()
                                  || (!selectedSignalIds.isEmpty()
                                      && presentation.states.size() != selectedSignalIds.size())
                                  || std::any_of(
                                      presentation.states.cbegin(),
                                      presentation.states.cend(),
                                      [](const Data::SemanticSignalRuntimeState &state) {
                                          return state.target.signalId.value.isEmpty()
                                                 || state.target.signalId.value
                                                        != state.target.signalId.value.trimmed();
                                      })
                                  || std::adjacent_find(
                                         presentation.states.cbegin(),
                                         presentation.states.cend(),
                                         [](const Data::SemanticSignalRuntimeState &left,
                                            const Data::SemanticSignalRuntimeState &right) {
                                             return left.target.signalId == right.target.signalId;
                                         })
                                         != presentation.states.cend();
    if (invalidSignalSet) {
        presentation.error = LiveSignalPresentationError::Incomplete;
        return presentation;
    }

    if (std::any_of(
            presentation.states.cbegin(),
            presentation.states.cend(),
            [&runtimeContext](const Data::SemanticSignalRuntimeState &state) {
                return !signalBindingMatchesContext(state, runtimeContext);
            })) {
        presentation.error = LiveSignalPresentationError::BindingUnverified;
        return presentation;
    }

    presentation.requiresWholeDeviceControl
        = !selectedSignalIds.isEmpty()
          && std::any_of(
              runtimeContext.actionStates.cbegin(),
              runtimeContext.actionStates.cend(),
              [&runtimeContext, &selection, &selectedSignalIds](
                  const Data::SemanticActionRuntimeState &action) {
                  if (action.target.controllerId != runtimeContext.controllerId
                      || action.target.scope != selection.scope
                      || action.target.deviceId != selection.deviceId
                      || action.target.kind != Data::SemanticRuntimeTargetKind::Action
                      || action.bindings.isEmpty()) {
                      return false;
                  }
                  const bool includesSelectedSignal = std::any_of(
                      action.bindings.cbegin(),
                      action.bindings.cend(),
                      [&selectedSignalIds](const Data::SemanticRuntimeBinding &binding) {
                          return selectedSignalIds.contains(binding.target.signalId.value);
                      });
                  const bool extendsPastSelection = std::any_of(
                      action.bindings.cbegin(),
                      action.bindings.cend(),
                      [&selectedSignalIds](const Data::SemanticRuntimeBinding &binding) {
                          return !selectedSignalIds.contains(binding.target.signalId.value);
                      });
                  return includesSelectedSignal && extendsPastSelection;
              });
    return presentation;
}

static bool runtimeContextAllowsSignalOnlyRefresh(
    Data::SemanticRuntimeContext previous, Data::SemanticRuntimeContext current)
{
    const QList<Data::SemanticSignalRuntimeState> previousSignals = previous.signalStates;
    const QList<Data::SemanticSignalRuntimeState> currentSignals = current.signalStates;
    previous.signalStates.clear();
    current.signalStates.clear();
    if (previous != current || previousSignals.size() != currentSignals.size())
        return false;

    for (qsizetype index = 0; index < previousSignals.size(); ++index) {
        Data::SemanticSignalRuntimeState previousState = previousSignals.at(index);
        Data::SemanticSignalRuntimeState currentState = currentSignals.at(index);
        previousState.value.reset();
        currentState.value.reset();
        previousState.availability = {};
        currentState.availability = {};
        previousState.quality = {};
        currentState.quality = {};
        previousState.snapshotComplete = false;
        currentState.snapshotComplete = false;
        previousState.captureCycle = 0;
        currentState.captureCycle = 0;
        previousState.controllerTimestampNs = 0;
        currentState.controllerTimestampNs = 0;
        previousState.observedAt = {};
        currentState.observedAt = {};
        previousState.detail.clear();
        currentState.detail.clear();
        if (previousState != currentState)
            return false;
    }
    return true;
}

static QString actionAvailabilityName(const Data::SemanticActionRuntimeState &action)
{
    if (!action.definition.enabled
        || action.qualification == Data::SemanticActionQualification::Unqualified) {
        return Tr::tr("Unavailable");
    }

    using Availability = Data::SemanticActionAvailability;
    switch (action.availability) {
    case Availability::Ready:
        return Tr::tr("Ready");
    case Availability::AwaitingApproval:
        return Tr::tr("Waiting for confirmation");
    case Availability::Stale:
        return Tr::tr("Stale");
    case Availability::Unverified:
        return Tr::tr("Unverified");
    case Availability::Rejected:
        return Tr::tr("Rejected");
    case Availability::Unavailable:
        return Tr::tr("Unavailable");
    }
    return Tr::tr("Unavailable");
}

static QString compactActionReason(const Data::SemanticActionRuntimeState &action)
{
    if (action.detail == QStringLiteral("manual_adapter_not_authorized"))
        return Tr::tr("Adapter hardware authorization is missing.");
    if (action.detail == QStringLiteral("manual_adapter_contract_version_unsupported"))
        return Tr::tr("The adapter control contract is not supported.");
    if (action.detail == QStringLiteral("manual_adapter_identity_mismatch"))
        return Tr::tr("The adapter does not match this device.");
    if (action.detail == QStringLiteral("runtime_output_transactions_unavailable"))
        return Tr::tr("Manual output is not supported by this controller.");
    if (action.detail == QStringLiteral("exclusive_control_not_owned"))
        return Tr::tr("Exclusive controller access is required.");
    if (action.detail == QStringLiteral("controller_state_disallows_output_transactions"))
        return Tr::tr("The controller state does not allow manual output.");
    if (action.detail == QStringLiteral("distributed_clocks_not_active"))
        return Tr::tr("DC mode is not running.");
    if (action.disabledReason
        == QStringLiteral("reference_unit_to_rpm_conversion_not_bound")) {
        return Tr::tr("Speed unit conversion is not configured.");
    }
    if (action.qualification == Data::SemanticActionQualification::Unqualified)
        return Tr::tr("Not qualified for manual control.");
    if (!action.definition.enabled)
        return Tr::tr("Disabled by the signed action definition.");

    using Availability = Data::SemanticActionAvailability;
    switch (action.availability) {
    case Availability::Ready:
        return {};
    case Availability::AwaitingApproval:
        return Tr::tr("Waiting for local confirmation.");
    case Availability::Stale:
        return Tr::tr("Runtime context changed.");
    case Availability::Unverified:
        return Tr::tr("Signed action proof is not verified.");
    case Availability::Rejected:
        return Tr::tr("Action is not available in the current state.");
    case Availability::Unavailable:
        return Tr::tr("Action is unavailable.");
    }
    return Tr::tr("Action is unavailable.");
}

static bool parameterTypeIsSupported(
    const Data::SemanticActionParameterRuntimeDefinition &parameter)
{
    using Primitive = Data::RuntimeResourcePrimitiveType;
    const Primitive primitive = parameter.primitiveType;
    if (primitive != Primitive::Boolean && primitive != Primitive::SignedInteger
        && primitive != Primitive::UnsignedInteger) {
        return false;
    }

    if (!parameter.minimum.isValid() || !parameter.maximum.isValid())
        return false;
    if (primitive == Primitive::Boolean) {
        return parameter.minimum.metaType().id() == QMetaType::Bool
               && parameter.maximum.metaType().id() == QMetaType::Bool
               && int(parameter.minimum.toBool()) <= int(parameter.maximum.toBool());
    }
    if (primitive == Primitive::SignedInteger) {
        return parameter.minimum.metaType().id() == QMetaType::LongLong
               && parameter.maximum.metaType().id() == QMetaType::LongLong
               && parameter.minimum.toLongLong() <= parameter.maximum.toLongLong();
    }
    return parameter.minimum.metaType().id() == QMetaType::ULongLong
           && parameter.maximum.metaType().id() == QMetaType::ULongLong
           && parameter.minimum.toULongLong() <= parameter.maximum.toULongLong();
}

static bool actionBindingMatchesContext(
    const Data::SemanticRuntimeBinding &binding,
    const Data::SemanticActionRuntimeState &action,
    const Data::SemanticRuntimeContext &context)
{
    return binding.target.controllerId == context.controllerId
           && binding.target.scope == context.scope
           && binding.target.deviceId == action.target.deviceId
           && binding.target.kind == Data::SemanticRuntimeTargetKind::Signal
           && binding.target.actionId.value.isEmpty()
           && Core::validateSemanticRuntimeBinding(binding).accepted()
           && binding.verification == context.bindingVerification
           && binding.sessionGeneration == context.sessionGeneration
           && binding.epoch == context.epoch && binding.mappingDigest == context.mappingDigest
           && binding.controllerMappingDigest == context.controllerMappingDigest;
}

static bool actionIsOperable(
    const Data::SemanticActionRuntimeState &action,
    const Data::SemanticRuntimeContext &context)
{
    if (action.target.controllerId != context.controllerId || action.target.scope != context.scope
        || action.target.kind != Data::SemanticRuntimeTargetKind::Action
        || action.target.deviceId.isNull() || !action.target.signalId.value.isEmpty()
        || action.target.actionId.value.isEmpty() || action.definition.id != action.target.actionId
        || action.actionBindingId != action.target.actionId.value
        || action.actionDefinitionId.isEmpty()
        || !Core::isCanonicalSha256Digest(action.actionDefinitionDigest)
        || !Core::isCanonicalSha256Digest(context.actionDefinitionsDigest)
        || context.cyclePeriodNs == 0 || !action.definition.enabled || action.definition.holdToRun
        || action.qualification != Data::SemanticActionQualification::Qualified
        || !action.disabledReason.isEmpty()
        || action.availability != Data::SemanticActionAvailability::Ready
        || !action.requiresApproval || action.bindings.isEmpty() || action.maximumTtlCycles == 0
        || action.maximumTtlCycles > 65535
        || std::any_of(
            action.parameters.cbegin(),
            action.parameters.cend(),
            [](const Data::SemanticActionParameterRuntimeDefinition &parameter) {
                return parameter.id.isEmpty()
                       || parameter.id != parameter.id.trimmed()
                       || !parameterTypeIsSupported(parameter);
            })
        || std::any_of(
            action.bindings.cbegin(),
            action.bindings.cend(),
            [&action, &context](const Data::SemanticRuntimeBinding &binding) {
                return !actionBindingMatchesContext(binding, action, context);
            })) {
        return false;
    }

    QList<QString> parameterIds;
    parameterIds.reserve(action.parameters.size());
    for (const Data::SemanticActionParameterRuntimeDefinition &parameter : action.parameters)
        parameterIds.append(parameter.id);
    std::sort(parameterIds.begin(), parameterIds.end());
    return std::adjacent_find(parameterIds.cbegin(), parameterIds.cend())
           == parameterIds.cend();
}

static QString actionDisplayName(const Data::SemanticActionRuntimeState &action)
{
    QString displayName = action.definition.displayName.trimmed();
    if (displayName.isEmpty())
        return Tr::tr("Signed action");
    if (displayName == action.actionDefinitionId || displayName.contains(':')) {
        displayName = displayName.section(':', -1);
        if (displayName.startsWith("action."))
            displayName.remove(0, qsizetype(QStringLiteral("action.").size()));
        displayName.replace('-', ' ');
        displayName.replace('_', ' ');
        displayName.replace('.', ' ');
        if (!displayName.isEmpty())
            displayName[0] = displayName.at(0).toUpper();
    }
    return displayName;
}

static QString parameterDisplayName(
    const Data::SemanticActionRuntimeState &action,
    const Data::SemanticActionParameterRuntimeDefinition &parameter)
{
    const auto definition = std::find_if(
        action.definition.parameters.cbegin(),
        action.definition.parameters.cend(),
        [&parameter](const Data::DeviceControlActionParameter &candidate) {
            return candidate.id == parameter.id;
        });
    const QString displayName
        = definition == action.definition.parameters.cend()
              ? QString()
              : definition->displayName.trimmed();
    QString base = displayName.isEmpty() ? Tr::tr("Parameter") : displayName;
    if (base == parameter.id) {
        base.replace('-', ' ');
        base.replace('_', ' ');
        base.replace('.', ' ');
        if (!base.isEmpty())
            base[0] = base.at(0).toUpper();
    }
    return parameter.unit.trimmed().isEmpty()
               ? base
               : Tr::tr("%1 (%2)").arg(base, parameter.unit.trimmed());
}

static QVariant defaultParameterValue(
    const Data::SemanticActionRuntimeState &action,
    const Data::SemanticActionParameterRuntimeDefinition &parameter)
{
    const auto definition = std::find_if(
        action.definition.parameters.cbegin(),
        action.definition.parameters.cend(),
        [&parameter](const Data::DeviceControlActionParameter &candidate) {
            return candidate.id == parameter.id;
        });
    const QVariant configuredDefault
        = definition != action.definition.parameters.cend() && definition->hasDefaultValue
              ? definition->defaultValue
              : QVariant();

    using Primitive = Data::RuntimeResourcePrimitiveType;
    switch (parameter.primitiveType) {
    case Primitive::Boolean:
        return configuredDefault.isValid() ? QVariant(configuredDefault.toBool())
                                           : QVariant(parameter.minimum.toBool());
    case Primitive::SignedInteger:
        return QVariant::fromValue<qlonglong>(
            configuredDefault.isValid() ? configuredDefault.toLongLong()
                                        : parameter.minimum.toLongLong());
    case Primitive::UnsignedInteger:
        return QVariant::fromValue<qulonglong>(
            configuredDefault.isValid() ? configuredDefault.toULongLong()
                                        : parameter.minimum.toULongLong());
    case Primitive::Opaque:
    case Primitive::FloatingPoint:
    case Primitive::Text:
    case Primitive::ByteArray:
        return {};
    }
    return {};
}

static QString operationStateText(Data::SemanticOperationState state)
{
    using State = Data::SemanticOperationState;
    switch (state) {
    case State::ApprovalRequired:
        return Tr::tr("Waiting for confirmation.");
    case State::Submitted:
    case State::Approved:
        return Tr::tr("Queued.");
    case State::Executing:
        return Tr::tr("Running.");
    case State::Succeeded:
        return Tr::tr("Completed.");
    case State::OutcomeUnknown:
        return Tr::tr("Result unknown.");
    case State::Canceled:
        return Tr::tr("Stopped safely.");
    case State::Rejected:
    case State::Failed:
    case State::TimedOut:
    case State::Expired:
        return Tr::tr("Failed.");
    }
    return Tr::tr("Failed.");
}

static QString operationResultIdentity(const Data::SemanticOperationRecord &record)
{
    QStringList identities;
    if (!record.resultCode.trimmed().isEmpty())
        identities.append(record.resultCode.trimmed());
    if (record.controllerError) {
        if (!record.controllerError->codeName.trimmed().isEmpty()) {
            identities.append(record.controllerError->codeName.trimmed());
        } else if (record.controllerError->code) {
            identities.append(QString::number(*record.controllerError->code));
        }
        if (record.controllerError->operationResult) {
            identities.append(
                Tr::tr("result %1").arg(*record.controllerError->operationResult));
        }
    }
    identities.removeDuplicates();
    return identities.join(QStringLiteral(", "));
}

static bool operationCanBeStopped(Data::SemanticOperationState state)
{
    using State = Data::SemanticOperationState;
    return state == State::ApprovalRequired || state == State::Approved || state == State::Executing
           || state == State::OutcomeUnknown;
}

SemanticControlPage::SemanticControlPage(
    WorkbenchController *controller, Core::SemanticRuntimeService *runtimeService, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_runtimeService(runtimeService)
    , m_selectionScope(new QLabel(this))
    , m_status(new QLabel(this))
    , m_signals(new QTreeWidget(this))
    , m_manualControl(new QGroupBox(Tr::tr("Signed actions"), this))
    , m_actions(new QTreeWidget(m_manualControl))
    , m_actionDetail(new QLabel(m_manualControl))
    , m_parameterHost(new QWidget(m_manualControl))
    , m_parameterLayout(new QFormLayout(m_parameterHost))
    , m_ttlCycles(new QSpinBox(m_parameterHost))
    , m_operationStatus(new QLabel(m_manualControl))
    , m_requestedValue(new QLineEdit(m_manualControl))
    , m_apply(new QPushButton(Tr::tr("Run action"), m_manualControl))
    , m_stop(new QPushButton(Tr::tr("Stop"), m_manualControl))
    , m_liveRefreshTimer(new QTimer(this))
{
    setProperty("EtherCAT.Workbench.SemanticControlPage", true);

    m_selectionScope->setObjectName("EtherCATSemanticControlScope");
    m_selectionScope->setWordWrap(true);
    m_selectionScope->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_selectionScope->setAccessibleName(Tr::tr("Semantic control selection scope"));

    m_status->setObjectName("EtherCATSemanticControlStatus");
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setAccessibleName(Tr::tr("Semantic control status"));

    m_manualControl->setObjectName("EtherCATSemanticManualControl");
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

    m_actions->setObjectName("EtherCATSemanticControlActions");
    m_actions->setColumnCount(3);
    m_actions->setHeaderLabels(
        {Tr::tr("Action"), Tr::tr("State"), Tr::tr("Details")});
    m_actions->setAlternatingRowColors(true);
    m_actions->setRootIsDecorated(false);
    m_actions->setUniformRowHeights(true);
    m_actions->setSelectionMode(QAbstractItemView::SingleSelection);
    m_actions->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_actions->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_actions->header()->setStretchLastSection(false);
    m_actions->setAccessibleName(Tr::tr("Signed semantic actions"));

    m_actionDetail->setObjectName("EtherCATSemanticActionDetail");
    m_actionDetail->setWordWrap(true);
    m_actionDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_actionDetail->setAccessibleName(Tr::tr("Selected action status"));

    m_parameterHost->setObjectName("EtherCATSemanticActionParameters");
    m_parameterLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    m_parameterLayout->setHorizontalSpacing(Utils::StyleHelper::SpacingTokens::GapHM);
    m_parameterLayout->setVerticalSpacing(Utils::StyleHelper::SpacingTokens::GapVM);

    m_ttlCycles->setObjectName("EtherCATSemanticActionTtlCycles");
    m_ttlCycles->setRange(1, 65535);
    m_ttlCycles->setSuffix(Tr::tr(" cycles"));
    m_ttlCycles->setAccessibleName(Tr::tr("Action lifetime in controller cycles"));

    m_operationStatus->setObjectName("EtherCATSemanticActionOperationStatus");
    m_operationStatus->setWordWrap(true);
    m_operationStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_operationStatus->setAccessibleName(Tr::tr("Manual action operation status"));

    // Retain this hidden child for compatibility with older property-page automation. Signed
    // actions use typed parameter editors below and never accept an untyped raw value.
    m_requestedValue->setObjectName("EtherCATSemanticControlRequestedValue");
    m_requestedValue->setPlaceholderText(Tr::tr("Output transaction is unavailable"));
    m_requestedValue->setAccessibleName(Tr::tr("Requested value"));
    m_requestedValue->hide();
    m_apply->setObjectName("EtherCATSemanticControlApply");
    m_apply->setAccessibleName(Tr::tr("Run selected signed action"));
    m_stop->setObjectName("EtherCATSemanticControlStop");
    m_stop->setAccessibleName(Tr::tr("Stop the current signed action safely"));
    m_liveRefreshTimer->setObjectName("EtherCATSemanticLiveRefreshTimer");
    m_liveRefreshTimer->setSingleShot(true);

    auto controlLayout = new QVBoxLayout(m_manualControl);
    controlLayout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    controlLayout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    controlLayout->addWidget(m_actions);
    controlLayout->addWidget(m_actionDetail);
    controlLayout->addWidget(m_parameterHost);
    controlLayout->addWidget(m_apply);
    controlLayout->addWidget(m_stop);
    controlLayout->addWidget(m_operationStatus);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM,
        Utils::StyleHelper::SpacingTokens::PaddingHM,
        Utils::StyleHelper::SpacingTokens::PaddingVM);
    layout->setSpacing(Utils::StyleHelper::SpacingTokens::GapVM);
    layout->addWidget(m_selectionScope);
    layout->addWidget(m_status);
    layout->addWidget(m_signals, 1);
    layout->addWidget(m_manualControl);

    m_actions->setVisible(false);
    m_selectionScope->setVisible(false);
    m_actionDetail->setText(Tr::tr("No signed action is selected."));
    m_parameterHost->setVisible(false);
    m_operationStatus->setText(Tr::tr("No manual action is active."));
    m_requestedValue->setEnabled(false);
    m_apply->setEnabled(false);
    m_stop->setEnabled(false);
    m_ttlCycles->setEnabled(false);

    QByteArray localConfirmationNonce
        = QByteArrayLiteral("embed-labs-workbench-local-confirmation-v1");
    localConfirmationNonce.append('\0');
    localConfirmationNonce.append(QUuid::createUuid().toRfc4122());
    m_localAuthenticationDigest
        = QCryptographicHash::hash(localConfirmationNonce, QCryptographicHash::Sha256);

    connect(
        m_actions,
        &QTreeWidget::currentItemChanged,
        this,
        [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
            m_selectedActionId = current
                                     ? Data::SemanticActionId{
                                           current->data(0, Qt::UserRole).toString()}
                                     : Data::SemanticActionId{};
            refreshActionEditor();
        });
    connect(m_apply, &QPushButton::clicked, this, &SemanticControlPage::requestSelectedAction);
    connect(m_stop, &QPushButton::clicked, this, &SemanticControlPage::requestStop);
    connect(m_liveRefreshTimer, &QTimer::timeout, this, &SemanticControlPage::requestLiveRefresh);
    connect(
        m_ttlCycles,
        &QSpinBox::valueChanged,
        this,
        [this](int) { updateApplyEnabled(); });

    if (m_runtimeService) {
        connect(
            m_runtimeService,
            &Core::SemanticRuntimeService::contextsChanged,
            this,
            &SemanticControlPage::refresh);
        connect(
            m_runtimeService,
            &Core::SemanticRuntimeService::operationChanged,
            this,
            [this](const Data::SemanticOperationId &operationId) {
                if (operationId == m_activeOperationId)
                    refreshOperation();
            });
        connect(
            m_runtimeService,
            &Core::SemanticRuntimeService::liveRefreshCompleted,
            this,
            [this](const Data::SemanticLiveRefreshResult &result) {
                const std::optional<Data::SemanticLiveRefreshResult> normalized
                    = normalizeLiveRefreshResult(result, true);
                if (normalized)
                    handleLiveRefreshResult(*normalized);
            });
        connect(m_runtimeService, &QObject::destroyed, this, [this] {
            stopLiveRefreshScheduler();
            m_liveRefreshCorrelationId.clear();
            m_discardLiveRefreshCompletion = false;
            m_runtimeService = nullptr;
            updateStopEnabled();
            refresh();
        });
    }
    refresh();
}

void SemanticControlPage::setContext(const Core::PropertyPageContext &context)
{
    if (m_context == context)
        return;
    stopLiveRefreshScheduler();
    m_context = context;
    refresh();
}

void SemanticControlPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateLiveRefreshScheduler();
}

void SemanticControlPage::hideEvent(QHideEvent *event)
{
    stopLiveRefreshScheduler();
    QWidget::hideEvent(event);
}

std::optional<Data::SemanticRuntimeContext> SemanticControlPage::selectedRuntimeContext() const
{
    if (!m_runtimeService || !m_controller || !m_controller->treeModel())
        return std::nullopt;
    const std::optional<SemanticControlSelection> selection
        = m_controller->treeModel()->semanticControlSelection(m_context.nodeId);
    if (!selection)
        return std::nullopt;

    QList<Data::SemanticRuntimeContext> matching;
    const QList<Data::SemanticRuntimeContext> contexts = m_runtimeService->contexts();
    std::copy_if(
        contexts.cbegin(),
        contexts.cend(),
        std::back_inserter(matching),
        [&selection](const Data::SemanticRuntimeContext &context) {
            return context.scope == selection->scope;
        });
    if (matching.size() != 1 || !contextIsVerifiedAndComplete(matching.constFirst()))
        return std::nullopt;
    return matching.constFirst();
}

std::optional<Data::SemanticActionRuntimeState> SemanticControlPage::selectedAction() const
{
    if (m_selectedActionId.value.isEmpty())
        return std::nullopt;
    const auto action = std::find_if(
        m_actionStates.cbegin(),
        m_actionStates.cend(),
        [this](const Data::SemanticActionRuntimeState &candidate) {
            return candidate.target.actionId == m_selectedActionId;
        });
    if (action == m_actionStates.cend())
        return std::nullopt;
    return *action;
}

std::optional<QMap<QString, QVariant>> SemanticControlPage::parameterValues() const
{
    const std::optional<Data::SemanticActionRuntimeState> action = selectedAction();
    if (!action)
        return std::nullopt;

    QMap<QString, QVariant> values;
    for (const Data::SemanticActionParameterRuntimeDefinition &parameter :
         action->parameters) {
        const QPointer<QWidget> editor = m_parameterEditors.value(parameter.id);
        if (!editor)
            return std::nullopt;

        using Primitive = Data::RuntimeResourcePrimitiveType;
        if (parameter.primitiveType == Primitive::Boolean) {
            const QCheckBox *checkBox = qobject_cast<QCheckBox *>(editor.data());
            if (!checkBox)
                return std::nullopt;
            const bool value = checkBox->isChecked();
            if (int(value) < int(parameter.minimum.toBool())
                || int(value) > int(parameter.maximum.toBool())) {
                return std::nullopt;
            }
            values.insert(parameter.id, value);
            continue;
        }

        const QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor.data());
        if (!lineEdit)
            return std::nullopt;
        bool ok = false;
        if (parameter.primitiveType == Primitive::SignedInteger) {
            const qlonglong value = lineEdit->text().trimmed().toLongLong(&ok, 10);
            if (!ok || value < parameter.minimum.toLongLong()
                || value > parameter.maximum.toLongLong()) {
                return std::nullopt;
            }
            values.insert(parameter.id, QVariant::fromValue<qlonglong>(value));
        } else if (parameter.primitiveType == Primitive::UnsignedInteger) {
            const QString text = lineEdit->text().trimmed();
            if (text.startsWith('-'))
                return std::nullopt;
            const qulonglong value = text.toULongLong(&ok, 10);
            if (!ok || value < parameter.minimum.toULongLong()
                || value > parameter.maximum.toULongLong()) {
                return std::nullopt;
            }
            values.insert(parameter.id, QVariant::fromValue<qulonglong>(value));
        } else {
            return std::nullopt;
        }
    }
    return values;
}

Data::SemanticRuntimeActor SemanticControlPage::localUserActor() const
{
    Data::SemanticRuntimeActor actor;
    actor.id = QStringLiteral("ethercat-workbench-local-user");
    actor.displayName = Tr::tr("Local user");
    actor.kind = Data::SemanticRuntimeActorKind::User;
    actor.origin = QStringLiteral("ethercat-workbench-local-confirmation");
    actor.authenticationDigest = m_localAuthenticationDigest;
    return actor;
}

void SemanticControlPage::refreshActionEditor()
{
    m_parameterEditors.clear();
    while (m_parameterLayout->count() > 0) {
        QLayoutItem *item = m_parameterLayout->takeAt(0);
        if (QWidget *widget = item->widget(); widget && widget != m_ttlCycles)
            delete widget;
        delete item;
    }

    const std::optional<Data::SemanticRuntimeContext> context = selectedRuntimeContext();
    const std::optional<Data::SemanticActionRuntimeState> action = selectedAction();
    if (!context || !action) {
        m_actionDetail->setText(Tr::tr("No signed action is selected."));
        m_parameterHost->setVisible(false);
        m_ttlCycles->setEnabled(false);
        m_apply->setEnabled(false);
        return;
    }

    const bool operable = actionIsOperable(*action, *context);
    const QString reason = operable ? Tr::tr("Ready for local confirmation.")
                                    : compactActionReason(*action);
    m_actionDetail->setText(
        reason.isEmpty() ? Tr::tr("Signed action proof is incomplete.") : reason);

    for (const Data::SemanticActionParameterRuntimeDefinition &parameter :
         action->parameters) {
        QWidget *editor = nullptr;
        const QVariant defaultValue = defaultParameterValue(*action, parameter);
        if (parameter.primitiveType == Data::RuntimeResourcePrimitiveType::Boolean) {
            auto checkBox = new QCheckBox(m_parameterHost);
            checkBox->setObjectName("EtherCATSemanticActionBooleanParameter");
            checkBox->setChecked(defaultValue.toBool());
            connect(
                checkBox,
                &QCheckBox::toggled,
                this,
                [this](bool) { updateApplyEnabled(); });
            editor = checkBox;
        } else {
            auto lineEdit = new QLineEdit(m_parameterHost);
            lineEdit->setObjectName(
                parameter.primitiveType
                        == Data::RuntimeResourcePrimitiveType::SignedInteger
                    ? "EtherCATSemanticActionSignedParameter"
                    : "EtherCATSemanticActionUnsignedParameter");
            if (parameter.primitiveType
                == Data::RuntimeResourcePrimitiveType::SignedInteger) {
                lineEdit->setText(QString::number(defaultValue.toLongLong()));
                lineEdit->setPlaceholderText(
                    Tr::tr("%1 to %2")
                        .arg(parameter.minimum.toLongLong())
                        .arg(parameter.maximum.toLongLong()));
            } else {
                lineEdit->setText(QString::number(defaultValue.toULongLong()));
                lineEdit->setPlaceholderText(
                    Tr::tr("%1 to %2")
                        .arg(parameter.minimum.toULongLong())
                        .arg(parameter.maximum.toULongLong()));
            }
            connect(
                lineEdit,
                &QLineEdit::textChanged,
                this,
                [this](const QString &) { updateApplyEnabled(); });
            editor = lineEdit;
        }

        editor->setProperty("EtherCAT.SemanticParameterId", parameter.id);
        editor->setAccessibleName(parameterDisplayName(*action, parameter));
        editor->setEnabled(operable);
        m_parameterEditors.insert(parameter.id, editor);
        m_parameterLayout->addRow(parameterDisplayName(*action, parameter), editor);
    }

    {
        const QSignalBlocker blocker(m_ttlCycles);
        m_ttlCycles->setMaximum(int(std::min<quint32>(action->maximumTtlCycles, 65535)));
        m_ttlCycles->setValue(m_ttlCycles->maximum());
    }
    m_ttlCycles->setEnabled(operable);
    m_parameterLayout->addRow(Tr::tr("Lifetime"), m_ttlCycles);
    m_parameterHost->setVisible(true);
    updateApplyEnabled();
}

void SemanticControlPage::updateApplyEnabled()
{
    const std::optional<Data::SemanticRuntimeContext> context = selectedRuntimeContext();
    const std::optional<Data::SemanticActionRuntimeState> action = selectedAction();
    const bool activeOperationBlocksSubmission
        = m_activeOperationState
          && *m_activeOperationState != Data::SemanticOperationState::Rejected
          && *m_activeOperationState != Data::SemanticOperationState::Succeeded
          && *m_activeOperationState != Data::SemanticOperationState::Failed
          && *m_activeOperationState != Data::SemanticOperationState::TimedOut
          && *m_activeOperationState != Data::SemanticOperationState::Canceled
          && *m_activeOperationState != Data::SemanticOperationState::Expired;
    const bool enabled
        = !m_confirmationOpen && !activeOperationBlocksSubmission && context && action
          && actionIsOperable(*action, *context) && parameterValues().has_value()
          && m_ttlCycles->value() > 0
          && quint32(m_ttlCycles->value()) <= action->maximumTtlCycles;
    m_apply->setEnabled(enabled);
}

void SemanticControlPage::updateStopEnabled()
{
    const bool enabled = m_runtimeService && m_activeOperationCancelable && m_activeOperationRecord
                         && recordMatchesActiveOperation(*m_activeOperationRecord)
                         && operationCanBeStopped(m_activeOperationRecord->state)
                         && !m_activeOperationRecord->cancellation;
    m_stop->setEnabled(enabled);
    if (enabled)
        m_manualControl->setVisible(true);
}

void SemanticControlPage::requestSelectedAction()
{
    if (m_confirmationOpen || !m_runtimeService)
        return;

    const std::optional<Data::SemanticRuntimeContext> context = selectedRuntimeContext();
    const std::optional<Data::SemanticActionRuntimeState> action = selectedAction();
    const std::optional<QMap<QString, QVariant>> values = parameterValues();
    if (!context || !action || !values || !actionIsOperable(*action, *context)) {
        m_operationStatus->setText(Tr::tr("Action is unavailable."));
        updateApplyEnabled();
        return;
    }

    Data::SemanticOperationRequest request;
    request.operationId = {
        QStringLiteral("workbench-ui-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))};
    request.kind = Data::SemanticOperationKind::InvokeAction;
    request.target = action->target;
    request.expectedEpoch = context->epoch;
    request.expectedMappingDigest = context->mappingDigest;
    request.expectedControllerMappingDigest = context->controllerMappingDigest;
    request.expectedActionDefinitionDigest = action->actionDefinitionDigest;
    request.expectedContextHash = context->contextHash;
    request.parameters = *values;
    request.ttlCycles = quint32(m_ttlCycles->value());
    request.reason = QStringLiteral("EtherCAT Workbench manual action");

    const Core::SemanticRuntimeValidation validation
        = Core::validateSemanticOperationRequest(request, *context);
    if (!validation.accepted() || !Core::isCanonicalSemanticOperationId(request.operationId)) {
        m_operationStatus->setText(Tr::tr("Action parameters are invalid."));
        updateApplyEnabled();
        return;
    }

    const QString displayName = actionDisplayName(*action);
    auto confirmation = new QMessageBox(
        QMessageBox::Question,
        Tr::tr("Confirm action"),
        Tr::tr("Run “%1” with the entered values?").arg(displayName),
        QMessageBox::Yes | QMessageBox::No,
        this);
    confirmation->setInformativeText(
        Tr::tr("This signed action requires confirmation by the local user."));
    confirmation->setDefaultButton(QMessageBox::No);
    confirmation->setEscapeButton(QMessageBox::No);
    confirmation->setAttribute(Qt::WA_DeleteOnClose);

    m_confirmationOpen = true;
    m_operationStatus->setText(Tr::tr("Waiting for confirmation."));
    updateApplyEnabled();
    updateLiveRefreshScheduler();
    connect(
        confirmation,
        &QMessageBox::finished,
        this,
        [this, request, displayName](int result) {
            m_confirmationOpen = false;
            if (result == QMessageBox::Yes)
                submitConfirmedAction(request, displayName);
            else {
                m_operationStatus->setText(Tr::tr("Confirmation canceled."));
                updateLiveRefreshScheduler();
            }
            updateApplyEnabled();
        });
    confirmation->open();
}

void SemanticControlPage::submitConfirmedAction(
    const Data::SemanticOperationRequest &request, const QString &actionName)
{
    if (!m_runtimeService)
        return;
    const std::optional<Data::SemanticRuntimeContext> context = selectedRuntimeContext();
    const std::optional<Data::SemanticActionRuntimeState> action = selectedAction();
    if (!context || !action || action->target != request.target
        || !Core::validateSemanticOperationRequest(request, *context).accepted()) {
        m_operationStatus->setText(Tr::tr("Runtime context changed. Try again."));
        return;
    }

    const Data::SemanticRuntimeActor actor = localUserActor();
    m_activeOperationId = request.operationId;
    m_activeActionName = actionName;
    m_activeOperationRequest = request;
    m_activeOperationRecord.reset();
    m_activeCancelRequest.reset();
    m_activeOperationCancelable = !action->definition.holdToRun;
    QPointer<SemanticControlPage> guardedPage(this);
    QPointer<Core::SemanticRuntimeService> service = m_runtimeService;
    Data::SemanticOperationRecord record = service->submit(request, actor);
    if (!guardedPage || !service)
        return;
    presentOperation(record);
    if (!guardedPage || !service)
        return;

    if (record.state != Data::SemanticOperationState::ApprovalRequired)
        return;
    const QByteArray canonicalRequest = Core::canonicalSemanticOperationRequest(request);
    const QByteArray expectedRequestDigest
        = canonicalRequest.isEmpty()
              ? QByteArray()
              : QCryptographicHash::hash(canonicalRequest, QCryptographicHash::Sha256);
    if (!Core::semanticOperationRequestsCanonicallyEqual(record.request, request)
        || record.canonicalRequestDigest != expectedRequestDigest
        || record.canonicalRequestDigest.size() != 32
        || record.approvalChallenge.size() != 32) {
        m_activeOperationState = Data::SemanticOperationState::OutcomeUnknown;
        m_operationStatus->setText(Tr::tr("Result unknown."));
        if (m_controller) {
            QPointer<WorkbenchController> controller = m_controller;
            controller->writeControllerOutput(
                Tr::tr("Action result is unknown: %1 (approval evidence invalid)")
                    .arg(m_activeActionName),
                ControllerOutputLevel::Error);
            if (!guardedPage || !service || !controller)
                return;
        }
        updateApplyEnabled();
        return;
    }

    Data::SemanticOperationApprovalRequest approval;
    approval.operationId = request.operationId;
    approval.decision = Data::SemanticApprovalDecision::Approved;
    approval.challenge = record.approvalChallenge;
    approval.expectedRequestDigest = record.canonicalRequestDigest;
    approval.expectedContextHash = request.expectedContextHash;
    approval.detail = QStringLiteral("Confirmed by the local Workbench user");
    record = service->approve(approval, actor);
    if (!guardedPage || !service)
        return;
    presentOperation(record);
}

bool SemanticControlPage::recordMatchesActiveOperation(
    const Data::SemanticOperationRecord &record) const
{
    if (!m_activeOperationRequest || !m_activeOperationCancelable || !record.revision
        || record.request.operationId != m_activeOperationId
        || record.request.operationId != m_activeOperationRequest->operationId
        || record.request.kind != Data::SemanticOperationKind::InvokeAction
        || record.request.target.kind != Data::SemanticRuntimeTargetKind::Action
        || record.request != *m_activeOperationRequest || record.actor != localUserActor()) {
        return false;
    }
    const bool cancellationCompleted = record.cancellation
                                       && record.cancellation->phase
                                              == Data::SemanticOperationCancellationPhase::Completed;
    if ((record.state == Data::SemanticOperationState::Canceled) != cancellationCompleted)
        return false;
    if (record.cancellation) {
        if ((m_activeCancelRequest && record.cancellation->request != *m_activeCancelRequest)
            || !Core::validateSemanticOperationCancellationEvidence(
                    record.cancellation->request, localUserActor(), record)
                    .accepted()) {
            return false;
        }
    }
    if (m_activeOperationRecord
        && m_activeOperationRecord->request.operationId == record.request.operationId) {
        const Data::SemanticOperationRecord &previous = *m_activeOperationRecord;
        if (record.revision < previous.revision
            || (record.revision == previous.revision && record != previous)) {
            return false;
        }
        if (previous.cancellation) {
            if (!record.cancellation
                || previous.cancellation->request != record.cancellation->request
                || previous.cancellation->actor != record.cancellation->actor
                || previous.cancellation->canonicalCancelDigest
                       != record.cancellation->canonicalCancelDigest
                || previous.cancellation->requestedAt != record.cancellation->requestedAt
                || quint32(record.cancellation->phase) < quint32(previous.cancellation->phase)) {
                return false;
            }
            if (record.cancellation->phase == previous.cancellation->phase
                && *record.cancellation != *previous.cancellation) {
                return false;
            }
            const auto preservesEvidence = [](const auto &observed, const auto &current) {
                return !observed || (current && *current == *observed);
            };
            if (!preservesEvidence(
                    previous.cancellation->safeHoldState, record.cancellation->safeHoldState)
                || !preservesEvidence(
                    previous.cancellation->pendingApplyRequest,
                    record.cancellation->pendingApplyRequest)
                || !preservesEvidence(
                    previous.cancellation->priorAppliedRequest,
                    record.cancellation->priorAppliedRequest)
                || !preservesEvidence(
                    previous.cancellation->safeHoldRequest, record.cancellation->safeHoldRequest)
                || !preservesEvidence(
                    previous.cancellation->terminalApplyResult,
                    record.cancellation->terminalApplyResult)) {
                return false;
            }
        }
    }
    const QByteArray canonicalRequest = Core::canonicalSemanticOperationRequest(record.request);
    const QByteArray requestDigest
        = canonicalRequest.isEmpty()
              ? QByteArray()
              : QCryptographicHash::hash(canonicalRequest, QCryptographicHash::Sha256);
    return requestDigest.size() == 32 && record.canonicalRequestDigest == requestDigest
           && record.request.expectedContextHash == m_activeOperationRequest->expectedContextHash;
}

void SemanticControlPage::requestStop()
{
    if (!m_runtimeService || !m_activeOperationCancelable || !m_activeOperationRequest)
        return;
    QPointer<SemanticControlPage> guardedPage(this);
    QPointer<Core::SemanticRuntimeService> service = m_runtimeService;
    const std::optional<Data::SemanticOperationRecord> liveRecord = service->operation(
        m_activeOperationId);
    if (!guardedPage || !service)
        return;
    if (!liveRecord || !recordMatchesActiveOperation(*liveRecord)
        || !operationCanBeStopped(liveRecord->state)) {
        m_operationStatus->setText(Tr::tr("Stop is unavailable for this operation."));
        updateStopEnabled();
        return;
    }
    m_activeOperationRecord = *liveRecord;
    if (liveRecord->cancellation) {
        m_activeCancelRequest = liveRecord->cancellation->request;
        m_operationStatus->setText(Tr::tr("Stop request recorded; waiting for safety proof."));
        updateStopEnabled();
        return;
    }

    Data::SemanticOperationCancelRequest request;
    if (m_activeCancelRequest) {
        request = *m_activeCancelRequest;
    } else {
        request.cancelId.value = QStringLiteral("workbench-cancel-%1")
                                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        request.operationId = liveRecord->request.operationId;
        request.expectedRevision = liveRecord->revision;
        request.expectedRequestDigest = liveRecord->canonicalRequestDigest;
        request.expectedContextHash = liveRecord->request.expectedContextHash;
        request.reason = QStringLiteral("EtherCAT Workbench immediate safe stop");
        m_activeCancelRequest = request;
    }
    if (Core::canonicalSemanticOperationCancelRequest(request).isEmpty()) {
        m_activeCancelRequest.reset();
        m_operationStatus->setText(Tr::tr("Stop request evidence is invalid."));
        updateStopEnabled();
        return;
    }

    const Data::SemanticRuntimeActor actor = localUserActor();
    const Data::SemanticOperationCancelResult result = service->cancel(request, actor);
    if (!guardedPage || !service)
        return;
    const bool resultRecordValid = !result.record || recordMatchesActiveOperation(*result.record);
    const bool acceptedEvidenceValid
        = !result.accepted()
          || (result.record && result.record->cancellation
              && result.record->cancellation->request == request
              && result.record->cancellation->actor == actor
              && Core::validateSemanticOperationCancellationEvidence(request, actor, *result.record)
                     .accepted());
    if (result.request != request || !resultRecordValid || !acceptedEvidenceValid) {
        m_activeOperationState = Data::SemanticOperationState::OutcomeUnknown;
        m_operationStatus->setText(Tr::tr("Operation evidence is invalid."));
        updateApplyEnabled();
        updateStopEnabled();
        updateLiveRefreshScheduler();
        return;
    }
    if (result.record)
        presentOperation(*result.record);
    if (!guardedPage || !service)
        return;
    if (result.accepted()) {
        if (!result.record || result.record->state != Data::SemanticOperationState::Canceled)
            m_operationStatus->setText(Tr::tr("Stop request recorded; waiting for safety proof."));
    } else {
        m_activeCancelRequest.reset();
        const QString detail = result.detail.trimmed();
        m_operationStatus->setText(detail.isEmpty() ? Tr::tr("Stop request was rejected.") : detail);
    }
    updateStopEnabled();
}

void SemanticControlPage::presentOperation(const Data::SemanticOperationRecord &record)
{
    if (record.request.operationId != m_activeOperationId)
        return;
    if (!recordMatchesActiveOperation(record)) {
        m_activeOperationState = Data::SemanticOperationState::OutcomeUnknown;
        m_operationStatus->setText(Tr::tr("Operation evidence is invalid."));
        updateApplyEnabled();
        updateStopEnabled();
        return;
    }
    m_activeOperationRecord = record;
    if (record.cancellation)
        m_activeCancelRequest = record.cancellation->request;
    m_activeOperationState = record.state;
    m_operationStatus->setText(
        record.cancellation
                && record.cancellation->phase != Data::SemanticOperationCancellationPhase::Completed
            ? Tr::tr("Stop request recorded; waiting for safety proof.")
            : operationStateText(record.state));
    updateApplyEnabled();
    updateStopEnabled();
    updateLiveRefreshScheduler();

    const bool sameOperation
        = m_hasReportedOperation && m_lastReportedOperationId == record.request.operationId;
    if (sameOperation && m_lastReportedOperationState == record.state) {
        return;
    }
    if (!sameOperation)
        m_lastReportedOperationMessage.clear();
    m_hasReportedOperation = true;
    m_lastReportedOperationId = record.request.operationId;
    m_lastReportedOperationState = record.state;

    using State = Data::SemanticOperationState;
    QString message;
    const QString resultIdentity = operationResultIdentity(record);
    switch (record.state) {
    case State::Submitted:
    case State::Approved:
        message = Tr::tr("Action queued: %1").arg(m_activeActionName);
        break;
    case State::Succeeded:
        message = Tr::tr("Action completed: %1").arg(m_activeActionName);
        break;
    case State::OutcomeUnknown:
        message = resultIdentity.isEmpty()
                      ? Tr::tr("Action result is unknown: %1").arg(m_activeActionName)
                      : Tr::tr("Action result is unknown: %1 (%2)")
                            .arg(m_activeActionName, resultIdentity);
        break;
    case State::Canceled:
        message = Tr::tr("Action stopped safely: %1").arg(m_activeActionName);
        break;
    case State::Rejected:
    case State::Failed:
    case State::TimedOut:
    case State::Expired:
        message = resultIdentity.isEmpty()
                      ? Tr::tr("Action failed: %1").arg(m_activeActionName)
                      : Tr::tr("Action failed: %1 (%2)")
                            .arg(m_activeActionName, resultIdentity);
        break;
    case State::ApprovalRequired:
    case State::Executing:
        break;
    }
    if (!message.isEmpty() && message != m_lastReportedOperationMessage && m_controller) {
        const bool failed = record.state == State::Rejected || record.state == State::Failed
                            || record.state == State::TimedOut
                            || record.state == State::Expired
                            || record.state == State::OutcomeUnknown;
        QPointer<SemanticControlPage> guardedPage(this);
        QPointer<WorkbenchController> controller = m_controller;
        controller->writeControllerOutput(
            message, failed ? ControllerOutputLevel::Error : ControllerOutputLevel::Information);
        if (!guardedPage || !controller)
            return;
    }
    if (!message.isEmpty())
        m_lastReportedOperationMessage = message;
}

void SemanticControlPage::refreshOperation()
{
    if (!m_runtimeService || m_activeOperationId.value.isEmpty())
        return;
    QPointer<SemanticControlPage> guardedPage(this);
    QPointer<Core::SemanticRuntimeService> service = m_runtimeService;
    const std::optional<Data::SemanticOperationRecord> record = service->operation(
        m_activeOperationId);
    if (!guardedPage || !service)
        return;
    if (record)
        presentOperation(*record);
}

bool SemanticControlPage::liveRefreshBlockedByOperation() const
{
    if (!m_activeOperationState)
        return false;

    using State = Data::SemanticOperationState;
    switch (*m_activeOperationState) {
    case State::Rejected:
    case State::Succeeded:
    case State::Failed:
    case State::TimedOut:
    case State::Canceled:
    case State::Expired:
        return false;
    case State::Submitted:
    case State::ApprovalRequired:
    case State::Approved:
    case State::Executing:
    case State::OutcomeUnknown:
        return true;
    }
    return true;
}

void SemanticControlPage::stopLiveRefreshScheduler()
{
    m_liveRefreshTimer->stop();
    // There is no public cancellation operation. Retain the accepted correlation so a hidden,
    // retargeted, or operation-blocked page cannot admit a second provider request before the
    // first one reaches a terminal result. Its eventual completion is consumed without applying
    // values, retry counters, or batch progress to the new presentation.
    if (!m_liveRefreshCorrelationId.isEmpty())
        m_discardLiveRefreshCompletion = true;
}

void SemanticControlPage::scheduleLiveRefresh(int delayMs)
{
    if (!isVisible() || !m_runtimeService || !m_runtimePresentation || m_liveRefreshPaused
        || m_confirmationOpen || liveRefreshBlockedByOperation()
        || !m_liveRefreshCorrelationId.isEmpty() || m_liveRefreshTargets.isEmpty()) {
        return;
    }
    m_liveRefreshTimer->start(std::max(0, delayMs));
}

void SemanticControlPage::updateLiveRefreshScheduler()
{
    m_liveRefreshTimer->stop();
    if (!isVisible() || !m_runtimeService || !m_runtimePresentation || m_confirmationOpen
        || liveRefreshBlockedByOperation() || !m_controller || !m_controller->treeModel()) {
        if (!m_liveRefreshCorrelationId.isEmpty())
            m_discardLiveRefreshCompletion = true;
        return;
    }

    const std::optional<SemanticControlSelection> selection
        = m_controller->treeModel()->semanticControlSelection(m_context.nodeId);
    const std::optional<Data::SemanticRuntimeContext> context = selectedRuntimeContext();
    if (!selection || !context || m_context != m_runtimePresentation->pageContext
        || selection->scope != m_runtimePresentation->scope
        || selection->deviceId != m_runtimePresentation->deviceId
        || selection->signalIds != m_runtimePresentation->signalIds
        || selection->wholeDevice != m_runtimePresentation->wholeDevice) {
        if (!m_liveRefreshCorrelationId.isEmpty())
            m_discardLiveRefreshCompletion = true;
        return;
    }

    const LiveSignalPresentation presentation = liveSignalPresentation(*context, *selection);
    if (presentation.error != LiveSignalPresentationError::None) {
        if (!m_liveRefreshCorrelationId.isEmpty())
            m_discardLiveRefreshCompletion = true;
        return;
    }

    QList<Data::SemanticLiveRefreshSignal> targets;
    targets.reserve(presentation.states.size());
    for (const Data::SemanticSignalRuntimeState &state : presentation.states)
        targets.append({state.target.deviceId, state.target.signalId});
    std::sort(
        targets.begin(),
        targets.end(),
        [](const Data::SemanticLiveRefreshSignal &left,
           const Data::SemanticLiveRefreshSignal &right) {
            const QString leftDevice = left.deviceId.toString();
            const QString rightDevice = right.deviceId.toString();
            return leftDevice == rightDevice ? left.signalId.value < right.signalId.value
                                             : leftDevice < rightDevice;
        });
    if (targets.isEmpty()
        || std::adjacent_find(targets.cbegin(), targets.cend()) != targets.cend()) {
        if (!m_liveRefreshCorrelationId.isEmpty())
            m_discardLiveRefreshCompletion = true;
        return;
    }

    const bool runtimeIdentityChanged = m_liveRefreshControllerId != context->controllerId
                                        || m_liveRefreshScope != context->scope
                                        || m_liveRefreshSessionGeneration
                                               != context->sessionGeneration
                                        || m_liveRefreshContextHash != context->contextHash;
    const bool targetsChanged = m_liveRefreshTargets != targets;
    if ((runtimeIdentityChanged || targetsChanged) && !m_liveRefreshCorrelationId.isEmpty())
        m_discardLiveRefreshCompletion = true;
    if (runtimeIdentityChanged) {
        m_liveRefreshDeferredCount = 0;
        m_liveRefreshFailureCount = 0;
        m_liveRefreshPaused = false;
        clearLiveRefreshPauseIndication();
        m_liveRefreshBatchOffset = 0;
    } else if (targetsChanged) {
        m_liveRefreshDeferredCount = 0;
        m_liveRefreshBatchOffset = 0;
    }

    m_liveRefreshControllerId = context->controllerId;
    m_liveRefreshScope = context->scope;
    m_liveRefreshSessionGeneration = context->sessionGeneration;
    m_liveRefreshContextHash = context->contextHash;
    m_liveRefreshTargets = std::move(targets);
    if (m_liveRefreshBatchOffset >= m_liveRefreshTargets.size())
        m_liveRefreshBatchOffset = 0;
    scheduleLiveRefresh(0);
}

void SemanticControlPage::requestLiveRefresh()
{
    m_liveRefreshTimer->stop();
    if (!isVisible() || !m_runtimeService || !m_runtimePresentation || m_liveRefreshPaused
        || m_confirmationOpen || liveRefreshBlockedByOperation()
        || !m_liveRefreshCorrelationId.isEmpty() || m_liveRefreshTargets.isEmpty()) {
        return;
    }

    const std::optional<Data::SemanticRuntimeContext> context = selectedRuntimeContext();
    if (!context || context->controllerId != m_liveRefreshControllerId
        || context->scope != m_liveRefreshScope
        || context->sessionGeneration != m_liveRefreshSessionGeneration
        || context->contextHash != m_liveRefreshContextHash) {
        stopLiveRefreshScheduler();
        return;
    }

    const qsizetype count
        = std::min<qsizetype>(64, m_liveRefreshTargets.size() - m_liveRefreshBatchOffset);
    if (count <= 0) {
        m_liveRefreshBatchOffset = 0;
        scheduleLiveRefresh(0);
        return;
    }

    Data::SemanticLiveRefreshRequest request;
    request.controllerId = m_liveRefreshControllerId;
    request.scope = m_liveRefreshScope;
    request.targets = m_liveRefreshTargets.mid(m_liveRefreshBatchOffset, count);
    request.expectedContextHash = m_liveRefreshContextHash;
    request.correlationId = QStringLiteral("workbench-live-%1-%2")
                                .arg(++m_liveRefreshRequestSequence)
                                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!request.isValid()) {
        pauseLiveRefresh(Data::SemanticLiveRefreshOutcome::Failed);
        return;
    }

    m_liveRefreshCorrelationId = request.correlationId;
    m_discardLiveRefreshCompletion = false;
    const Data::SemanticLiveRefreshResult directResult = m_runtimeService->requestLiveRefresh(
        request);
    const std::optional<Data::SemanticLiveRefreshResult> normalized
        = normalizeLiveRefreshResult(directResult, false);
    if (normalized && normalized->outcome != Data::SemanticLiveRefreshOutcome::Accepted) {
        handleLiveRefreshResult(*normalized);
    }
}

std::optional<Data::SemanticLiveRefreshResult> SemanticControlPage::normalizeLiveRefreshResult(
    const Data::SemanticLiveRefreshResult &result, bool completionSignal) const
{
    if (m_liveRefreshCorrelationId.isEmpty())
        return std::nullopt;
    if (result.correlationId != m_liveRefreshCorrelationId) {
        if (completionSignal)
            return std::nullopt;
        return Data::SemanticLiveRefreshResult{
            m_liveRefreshCorrelationId,
            Data::SemanticLiveRefreshOutcome::Failed,
            QStringLiteral("semantic-live-refresh-invalid-result"),
            QStringLiteral("The semantic runtime returned an invalid refresh result."),
            0,
        };
    }
    if (result.isValid()
        && (!completionSignal || result.outcome != Data::SemanticLiveRefreshOutcome::Accepted)) {
        return result;
    }
    return Data::SemanticLiveRefreshResult{
        m_liveRefreshCorrelationId,
        Data::SemanticLiveRefreshOutcome::Failed,
        QStringLiteral("semantic-live-refresh-invalid-result"),
        QStringLiteral("The semantic runtime returned an invalid refresh result."),
        0,
    };
}

void SemanticControlPage::handleLiveRefreshResult(const Data::SemanticLiveRefreshResult &result)
{
    if (m_liveRefreshCorrelationId.isEmpty() || result.correlationId != m_liveRefreshCorrelationId) {
        return;
    }

    using Outcome = Data::SemanticLiveRefreshOutcome;
    if (result.outcome == Outcome::Accepted)
        return;

    const bool discardCompletion = m_discardLiveRefreshCompletion;
    m_liveRefreshCorrelationId.clear();
    m_discardLiveRefreshCompletion = false;
    if (discardCompletion) {
        updateLiveRefreshScheduler();
        return;
    }
    switch (result.outcome) {
    case Outcome::Refreshed: {
        m_liveRefreshDeferredCount = 0;
        m_liveRefreshFailureCount = 0;
        m_liveRefreshPaused = false;
        clearLiveRefreshPauseIndication();
        const qsizetype remaining = m_liveRefreshTargets.size() - m_liveRefreshBatchOffset;
        const qsizetype completed = std::min<qsizetype>(64, remaining);
        m_liveRefreshBatchOffset += completed;
        if (m_liveRefreshBatchOffset >= m_liveRefreshTargets.size())
            m_liveRefreshBatchOffset = 0;
        scheduleLiveRefresh(500);
        break;
    }
    case Outcome::Deferred: {
        m_liveRefreshFailureCount = 0;
        const int exponent = std::min(m_liveRefreshDeferredCount, 3);
        ++m_liveRefreshDeferredCount;
        scheduleLiveRefresh(1000 * (1 << exponent));
        break;
    }
    case Outcome::Failed:
        m_liveRefreshDeferredCount = 0;
        ++m_liveRefreshFailureCount;
        if (m_liveRefreshFailureCount >= 3) {
            pauseLiveRefresh(result.outcome);
        } else {
            scheduleLiveRefresh(1000);
        }
        break;
    case Outcome::Rejected:
    case Outcome::Unsupported:
        pauseLiveRefresh(result.outcome);
        break;
    case Outcome::Accepted:
        break;
    }
}

void SemanticControlPage::pauseLiveRefresh(Data::SemanticLiveRefreshOutcome outcome)
{
    m_liveRefreshPaused = true;
    m_liveRefreshTimer->stop();
    QString message;
    switch (outcome) {
    case Data::SemanticLiveRefreshOutcome::Unsupported:
        message = Tr::tr("Live value updates are not supported.");
        break;
    case Data::SemanticLiveRefreshOutcome::Rejected:
        message = Tr::tr("Live value updates were rejected.");
        break;
    case Data::SemanticLiveRefreshOutcome::Accepted:
    case Data::SemanticLiveRefreshOutcome::Refreshed:
    case Data::SemanticLiveRefreshOutcome::Deferred:
    case Data::SemanticLiveRefreshOutcome::Failed:
        message = Tr::tr("Live value updates are paused after repeated failures.");
        break;
    }
    const bool newlyIndicated = m_liveRefreshPauseMessage != message;
    m_liveRefreshPauseMessage = message;
    m_status->setText(m_liveRefreshPauseMessage);
    if (newlyIndicated && m_controller) {
        m_controller
            ->writeControllerOutput(m_liveRefreshPauseMessage, ControllerOutputLevel::Warning);
    }
}

void SemanticControlPage::clearLiveRefreshPauseIndication()
{
    if (m_liveRefreshPauseMessage.isEmpty())
        return;
    m_liveRefreshPauseMessage.clear();
    if (!m_runtimePresentation || !m_controller || !m_controller->treeModel())
        return;
    const std::optional<SemanticControlSelection> selection
        = m_controller->treeModel()->semanticControlSelection(m_context.nodeId);
    if (!selection)
        return;
    const LiveSignalPresentation presentation
        = liveSignalPresentation(m_runtimePresentation->runtimeContext, *selection);
    if (presentation.error == LiveSignalPresentationError::None)
        presentLiveSignals(presentation.states, presentation.requiresWholeDeviceControl);
}

void SemanticControlPage::presentLiveSignals(
    const QList<Data::SemanticSignalRuntimeState> &states, bool requiresWholeDeviceControl)
{
    m_signals->clear();
    bool allReady = true;
    for (const Data::SemanticSignalRuntimeState &state : states) {
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
             displayValue.converted || state.availability != Data::SemanticSignalAvailability::Ready
                 ? signalAvailabilityName(state.availability)
                 : Tr::tr("Unavailable")});
        item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
        m_signals->addTopLevelItem(item);
    }

    m_signals->setVisible(!states.isEmpty());
    if (states.isEmpty() && m_actionStates.isEmpty()) {
        m_status->setText(Tr::tr("No live values or signed actions are available."));
    } else if (states.isEmpty()) {
        m_status->setText(Tr::tr("Signed actions are available."));
    } else if (allReady && m_actionStates.isEmpty() && requiresWholeDeviceControl) {
        m_status->setText(
            Tr::tr("This signal belongs to an atomic output group. Select Modules / Channels "
                   "to control the complete group."));
    } else if (allReady && m_actionStates.isEmpty()) {
        m_status->setText(Tr::tr("Live values are available. Manual output is not enabled yet."));
    } else if (allReady) {
        m_status->setText(Tr::tr("Live values and signed actions are available."));
    } else if (m_actionStates.isEmpty()) {
        m_status->setText(Tr::tr("Some live values are unavailable. Manual output is disabled."));
    } else {
        m_status->setText(
            Tr::tr("Some live values are unavailable. Signed actions remain state-gated."));
    }
    if (!m_liveRefreshPauseMessage.isEmpty())
        m_status->setText(m_liveRefreshPauseMessage);
}

bool SemanticControlPage::refreshLiveSignalsOnly()
{
    if (!m_runtimePresentation || !m_runtimeService || !m_controller || !m_controller->treeModel()
        || m_context != m_runtimePresentation->pageContext) {
        return false;
    }

    const std::optional<SemanticControlSelection> selection
        = m_controller->treeModel()->semanticControlSelection(m_context.nodeId);
    if (!selection || selection->scope != m_runtimePresentation->scope
        || selection->deviceId != m_runtimePresentation->deviceId
        || selection->signalIds != m_runtimePresentation->signalIds
        || selection->wholeDevice != m_runtimePresentation->wholeDevice) {
        return false;
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
    if (scopeContexts.size() != 1 || !contextIsVerifiedAndComplete(scopeContexts.constFirst()))
        return false;

    const Data::SemanticRuntimeContext &runtimeContext = scopeContexts.constFirst();
    if (!runtimeContextAllowsSignalOnlyRefresh(m_runtimePresentation->runtimeContext, runtimeContext)) {
        return false;
    }

    const LiveSignalPresentation presentation = liveSignalPresentation(runtimeContext, *selection);
    if (presentation.error != LiveSignalPresentationError::None)
        return false;

    m_runtimePresentation->runtimeContext = runtimeContext;
    presentLiveSignals(presentation.states, presentation.requiresWholeDeviceControl);
    updateApplyEnabled();
    refreshOperation();
    return true;
}

void SemanticControlPage::refresh()
{
    QPointer<SemanticControlPage> guardedPage(this);
    const bool liveSignalsOnly = refreshLiveSignalsOnly();
    if (!guardedPage)
        return;
    if (liveSignalsOnly) {
        updateLiveRefreshScheduler();
        return;
    }

    stopLiveRefreshScheduler();
    m_runtimePresentation.reset();
    const Data::SemanticActionId preferredActionId = m_selectedActionId;
    const QSignalBlocker actionTreeBlocker(m_actions);
    m_signals->clear();
    m_signals->setVisible(false);
    m_selectionScope->clear();
    m_selectionScope->setVisible(false);
    m_actions->clear();
    m_actions->setVisible(false);
    m_manualControl->setVisible(true);
    m_actionStates.clear();
    m_selectedActionId = {};
    m_actionDetail->setText(Tr::tr("No signed action is selected."));
    m_parameterHost->setVisible(false);
    m_requestedValue->setEnabled(false);
    m_apply->setEnabled(false);
    m_ttlCycles->setEnabled(false);
    updateStopEnabled();

    const bool controllerTopologyNode
        = m_context.nodeKind == Core::WorkbenchNodeKind::Module && m_controller
          && m_controller->treeModel()
          && m_controller->treeModel()->controllerTopologySlave(m_context.nodeId);
    if (controllerTopologyNode) {
        m_manualControl->setVisible(false);
        m_status->setText(
            Tr::tr(
                "Apply the current bus to the project and save it before signed, verified "
                "control can be enabled."));
        updateStopEnabled();
        return;
    }

    const std::optional<SemanticControlSelection> selection
        = m_controller && m_controller->treeModel()
              ? m_controller->treeModel()->semanticControlSelection(m_context.nodeId)
              : std::nullopt;
    if (!selection) {
        m_status->setText(Tr::tr("Control is unavailable for this selection."));
        return;
    }

    if (selection->wholeDevice) {
        const bool selectedDevice
            = m_context.nodeKind == Core::WorkbenchNodeKind::ConfiguredSlave
              || m_context.nodeKind == Core::WorkbenchNodeKind::Modules;
        m_selectionScope->setText(
            selectedDevice
                ? Tr::tr("Whole-device control")
                : Tr::tr(
                      "Whole-device control | No exact semantic signal mapping is available for "
                      "this node."));
    } else if (m_context.nodeKind == Core::WorkbenchNodeKind::Channel) {
        m_selectionScope->setText(Tr::tr("Selected channel control"));
    } else {
        m_selectionScope->setText(
            Tr::tr(
                "Selected module control | %n signal(s)",
                nullptr,
                selection->signalIds.size()));
    }
    m_selectionScope->setVisible(true);

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

    const LiveSignalPresentation signalPresentation
        = liveSignalPresentation(runtimeContext, *selection);
    if (signalPresentation.error == LiveSignalPresentationError::Incomplete) {
        m_status->setText(Tr::tr("Runtime signal set is incomplete."));
        return;
    }
    if (signalPresentation.error == LiveSignalPresentationError::BindingUnverified) {
        m_status->setText(Tr::tr("Runtime signal binding is not verified."));
        return;
    }

    QSet<QString> selectedSignalIds;
    for (const Data::SemanticSignalId &signalId : selection->signalIds)
        selectedSignalIds.insert(signalId.value);

    std::copy_if(
        runtimeContext.actionStates.cbegin(),
        runtimeContext.actionStates.cend(),
        std::back_inserter(m_actionStates),
        [&runtimeContext, &selection, &selectedSignalIds](
            const Data::SemanticActionRuntimeState &action) {
            return action.target.controllerId == runtimeContext.controllerId
                   && action.target.scope == selection->scope
                   && action.target.deviceId == selection->deviceId
                   && action.target.kind == Data::SemanticRuntimeTargetKind::Action
                   && action.target.signalId.value.isEmpty()
                   && (selectedSignalIds.isEmpty()
                       || (!action.bindings.isEmpty()
                           && std::all_of(
                               action.bindings.cbegin(),
                               action.bindings.cend(),
                               [&selection, &selectedSignalIds](
                                   const Data::SemanticRuntimeBinding &binding) {
                                   return binding.target.scope == selection->scope
                                          && binding.target.deviceId == selection->deviceId
                                          && binding.target.kind
                                                 == Data::SemanticRuntimeTargetKind::Signal
                                          && binding.target.actionId.value.isEmpty()
                                          && selectedSignalIds.contains(
                                              binding.target.signalId.value);
                               })));
        });
    std::sort(
        m_actionStates.begin(),
        m_actionStates.end(),
        [](const Data::SemanticActionRuntimeState &left,
           const Data::SemanticActionRuntimeState &right) {
            const int displayOrder = QString::localeAwareCompare(
                actionDisplayName(left), actionDisplayName(right));
            return displayOrder == 0
                       ? left.target.actionId.value < right.target.actionId.value
                       : displayOrder < 0;
        });

    QSet<QString> actionIds;
    const bool invalidActionSet = std::any_of(
        m_actionStates.cbegin(),
        m_actionStates.cend(),
        [&actionIds](const Data::SemanticActionRuntimeState &action) {
            const QString actionId = action.target.actionId.value;
            if (actionId.isEmpty() || actionId != actionId.trimmed()
                || actionIds.contains(actionId)) {
                return true;
            }
            actionIds.insert(actionId);
            return false;
        });
    if (invalidActionSet)
        m_actionStates.clear();

    QTreeWidgetItem *preferredItem = nullptr;
    QTreeWidgetItem *firstOperableItem = nullptr;
    for (const Data::SemanticActionRuntimeState &action : std::as_const(m_actionStates)) {
        const bool operable = actionIsOperable(action, runtimeContext);
        QString reason = compactActionReason(action);
        if (!operable && reason.isEmpty())
            reason = Tr::tr("Signed action proof is incomplete.");
        auto item = new QTreeWidgetItem(
            {actionDisplayName(action),
             operable ? actionAvailabilityName(action) : Tr::tr("Unavailable"),
             reason});
        item->setData(0, Qt::UserRole, action.target.actionId.value);
        if (!operable) {
            item->setFlags(
                item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        } else if (!firstOperableItem) {
            firstOperableItem = item;
        }
        if (operable && action.target.actionId == preferredActionId)
            preferredItem = item;
        m_actions->addTopLevelItem(item);
    }
    m_actions->setVisible(!m_actionStates.isEmpty());
    if (QTreeWidgetItem *selectedItem = preferredItem ? preferredItem : firstOperableItem) {
        m_actions->setCurrentItem(selectedItem);
        m_selectedActionId = {
            selectedItem->data(0, Qt::UserRole).toString()};
    }

    presentLiveSignals(signalPresentation.states, signalPresentation.requiresWholeDeviceControl);
    m_runtimePresentation = RuntimePresentation{
        m_context,
        selection->scope,
        selection->deviceId,
        selection->signalIds,
        selection->wholeDevice,
        runtimeContext,
    };
    refreshActionEditor();
    refreshOperation();
    if (!guardedPage)
        return;
    updateLiveRefreshScheduler();
}

} // namespace EtherCAT::Workbench::Internal

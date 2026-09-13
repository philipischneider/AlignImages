#include "io/SessionSerializer.h"

#include "nlohmann/json.hpp"

#include <fstream>

namespace align
{
namespace
{
const char* ToString(WorkflowPhase phase)
{
    switch (phase)
    {
    case WorkflowPhase::InitialAlignment:   return "initial_alignment";
    case WorkflowPhase::ConvergenceAnalysis: return "convergence_analysis";
    case WorkflowPhase::PriorRefinement:    return "prior_refinement";
    case WorkflowPhase::ManualRefinement:   return "manual_refinement";
    case WorkflowPhase::Export:             return "export";
    default:                                return "setup";
    }
}

WorkflowPhase ParseWorkflowPhase(const std::string& s)
{
    if (s == "initial_alignment")    return WorkflowPhase::InitialAlignment;
    if (s == "convergence_analysis") return WorkflowPhase::ConvergenceAnalysis;
    if (s == "prior_refinement")     return WorkflowPhase::PriorRefinement;
    if (s == "manual_refinement")    return WorkflowPhase::ManualRefinement;
    if (s == "export")               return WorkflowPhase::Export;
    return WorkflowPhase::Setup;
}

const char* ToString(DpiMode mode)
{
    return mode == DpiMode::Auto ? "auto" : "manual";
}

const char* ToString(PreviewMode mode)
{
    switch (mode)
    {
    case PreviewMode::Blend:
        return "blend";
    case PreviewMode::Checkerboard:
        return "checkerboard";
    case PreviewMode::Difference:
        return "difference";
    case PreviewMode::Multiply:
        return "multiply";
    default:
        return "blend";
    }
}

DpiMode ParseDpiMode(const nlohmann::json& value)
{
    return value.get<std::string>() == "manual" ? DpiMode::Manual : DpiMode::Auto;
}

PreviewMode ParsePreviewMode(const nlohmann::json& value)
{
    const std::string mode = value.get<std::string>();
    if (mode == "checkerboard")
    {
        return PreviewMode::Checkerboard;
    }
    if (mode == "difference")
    {
        return PreviewMode::Difference;
    }
    if (mode == "multiply")
    {
        return PreviewMode::Multiply;
    }
    return PreviewMode::Blend;
}

PairStatus ParsePairStatus(const nlohmann::json& value)
{
    const std::string status = value.get<std::string>();
    if (status == "candidate")
    {
        return PairStatus::Candidate;
    }
    if (status == "aligned")
    {
        return PairStatus::Aligned;
    }
    if (status == "suspect")
    {
        return PairStatus::Suspect;
    }
    if (status == "manual")
    {
        return PairStatus::Manual;
    }
    return PairStatus::Unmatched;
}

const char* ToString(OperationKind kind)
{
    switch (kind)
    {
    case OperationKind::BatchAutoAlignment:   return "batch_auto_alignment";
    case OperationKind::CurrentAutoAlignment: return "current_auto_alignment";
    case OperationKind::ManualLandmarks:      return "manual_landmarks";
    case OperationKind::PriorRefinement:      return "prior_refinement";
    case OperationKind::BatchExport:          return "batch_export";
    case OperationKind::ConvergenceAnalysis:  return "convergence_analysis";
    default:                                  return "current_auto_alignment";
    }
}

OperationKind ParseOperationKind(const std::string& value)
{
    if (value == "batch_auto_alignment")   return OperationKind::BatchAutoAlignment;
    if (value == "manual_landmarks")       return OperationKind::ManualLandmarks;
    if (value == "prior_refinement")       return OperationKind::PriorRefinement;
    if (value == "batch_export")           return OperationKind::BatchExport;
    if (value == "convergence_analysis")   return OperationKind::ConvergenceAnalysis;
    return OperationKind::CurrentAutoAlignment;
}

const char* ToString(OperationScope scope)
{
    switch (scope)
    {
    case OperationScope::Global:     return "global";
    case OperationScope::Selection:  return "selection";
    case OperationScope::SinglePair: return "single_pair";
    default:                         return "single_pair";
    }
}

OperationScope ParseOperationScope(const std::string& value)
{
    if (value == "global")    return OperationScope::Global;
    if (value == "selection") return OperationScope::Selection;
    return OperationScope::SinglePair;
}
} // namespace

Result SessionSerializer::Save(const SessionModel& session, const std::filesystem::path& filePath) const
{
    nlohmann::json root;
    root["project"] = {
        {"name", session.projectName},
        {"version", session.version},
        {"workflow_phase", ToString(session.workflowPhase)},
        {"next_operation_id", session.nextOperationId},
        {"registration_preset", session.projectPreferences.registrationPreset},
        {"auto_alignment_method", session.projectPreferences.autoAlignmentMethod},
        {"mask_method", session.projectPreferences.maskMethod},
        {"score_method", session.projectPreferences.scoreMethod},
        {"refinement_method", session.projectPreferences.refinementMethod},
        {"transform_type", session.projectPreferences.transformType},
        {"max_iterations", session.projectPreferences.maxIterations},
        {"coarse_levels", session.projectPreferences.coarseLevels},
        {"use_alignment_preview", session.projectPreferences.useAlignmentPreview},
        {"use_sigma_restricted_prior_refinement", session.projectPreferences.useSigmaRestrictedPriorRefinement},
        {"sigma_multiplier", session.projectPreferences.sigmaMultiplier},
        {"prefer_manual_priors", session.projectPreferences.preferManualPriors}
    };
    root["operations"] = nlohmann::json::array();

    root["ui"] = {
        {"dpi_mode", ToString(session.uiPreferences.dpiMode)},
        {"dpi_override", session.uiPreferences.dpiOverride},
        {"preview_mode", ToString(session.uiPreferences.previewMode)},
        {"blend_alpha", session.uiPreferences.blendAlpha},
        {"checker_size", session.uiPreferences.checkerSize},
        {"sync_viewports", session.uiPreferences.syncViewports},
        {"timeline_height", session.uiPreferences.timelineHeight}
    };

    auto serializeStack = [](const StackModel& stack)
    {
        nlohmann::json result;
        result["id"] = stack.id;
        result["name"] = stack.name;
        result["modality"] = stack.modality;
        result["directory"] = stack.directory;
        result["is_dicom"] = stack.isDicom;
        result["window_center"] = stack.windowCenter;
        result["window_width"] = stack.windowWidth;
        result["default_window_center"] = stack.defaultWindowCenter;
        result["default_window_width"] = stack.defaultWindowWidth;
        result["rescale_slope"] = stack.rescaleSlope;
        result["rescale_intercept"] = stack.rescaleIntercept;
        result["slices"] = nlohmann::json::array();
        for (const SliceRecord& slice : stack.slices)
        {
            result["slices"].push_back({
                {"index", slice.stackIndex},
                {"file_path", slice.filePath},
                {"file_name", slice.fileName},
                {"width", slice.width},
                {"height", slice.height},
                {"instance_number", slice.instanceNumber},
                {"slice_location", slice.sliceLocation},
                {"flip_horizontal", slice.flipHorizontal},
                {"flip_vertical", slice.flipVertical},
                {"rotation_degrees", slice.rotationDegrees}
            });
        }
        return result;
    };

    root["stacks"] = nlohmann::json::array();
    for (const StackModel& stack : session.stacks)
    {
        root["stacks"].push_back(serializeStack(stack));
    }

    root["active_pairing_id"] = session.activePairingId;
    root["pairings"] = nlohmann::json::array();
    for (const PairingModel& pairing : session.pairings)
    {
        nlohmann::json pairingJson = {
            {"id", pairing.id},
            {"label", pairing.label},
            {"fixed_stack_id", pairing.fixedStackId},
            {"moving_stack_id", pairing.movingStackId},
            {"global_offset", pairing.globalOffset},
            {"fixed_timeline_offset", pairing.fixedTimelineOffset},
            {"moving_timeline_offset", pairing.movingTimelineOffset},
            {"active_fixed_index", pairing.activeFixedIndex},
            {"active_moving_index", pairing.activeMovingIndex},
            {"pairs", nlohmann::json::array()}
        };
        for (const PairRecord& pair : pairing.pairs)
        {
            pairingJson["pairs"].push_back({
                {"fixed_index", pair.fixedIndex},
                {"moving_index", pair.movingIndex},
                {"valid", pair.valid},
                {"status", align::ToString(pair.status)}
            });
        }
        root["pairings"].push_back(std::move(pairingJson));
    }

    root["registrations"] = nlohmann::json::array();
    for (const RegistrationResult& registration : session.registrations)
    {
        root["registrations"].push_back({
            {"fixed_stack_id", registration.fixedStackId},
            {"moving_stack_id", registration.movingStackId},
            {"fixed_index", registration.fixedIndex},
            {"moving_index", registration.movingIndex},
            {"transform_type", registration.transformType},
            {"forward_matrix_3x3", registration.forward.matrix},
            {"inverse_matrix_3x3", registration.inverse.matrix},
            {"score", registration.score},
            {"manual_rms_error", registration.manualRmsError},
            {"converged", registration.converged},
            {"is_manual", registration.isManual},
            {"is_interpolated", registration.isInterpolated},
            {"has_convergence_prior", registration.hasConvergencePrior},
            {"convergence_outlier", registration.convergenceOutlier},
            {"refined_with_prior", registration.refinedWithPrior},
            {"prior_tx", registration.priorTx},
            {"prior_ty", registration.priorTy},
            {"prior_theta", registration.priorTheta},
            {"prior_scale", registration.priorScale},
            {"prior_tx_mean", registration.priorTxMean},
            {"prior_ty_mean", registration.priorTyMean},
            {"prior_theta_mean", registration.priorThetaMean},
            {"prior_scale_mean", registration.priorScaleMean},
            {"prior_tx_stddev", registration.priorTxStdDev},
            {"prior_ty_stddev", registration.priorTyStdDev},
            {"prior_theta_stddev", registration.priorThetaStdDev},
            {"prior_scale_stddev", registration.priorScaleStdDev},
            {"prior_sx", registration.priorSx},
            {"prior_sy", registration.priorSy},
            {"prior_sx_mean", registration.priorSxMean},
            {"prior_sy_mean", registration.priorSyMean},
            {"prior_sx_stddev", registration.priorSxStdDev},
            {"prior_sy_stddev", registration.priorSyStdDev},
            {"timestamp", registration.timestamp},
            {"algorithm_version", registration.algorithmVersion},
            {"operation_log", registration.operationLog},
            {"history", nlohmann::json::array()},
            {"iterations", nlohmann::json::array()},
            {"landmarks", nlohmann::json::array()}
        });

        for (const RegistrationSnapshot& snapshot : registration.history)
        {
            root["registrations"].back()["history"].push_back({
                {"label", snapshot.label},
                {"operation_id", snapshot.operationId},
                {"timestamp", snapshot.timestamp},
                {"transform_type", snapshot.transformType},
                {"forward_matrix_3x3", snapshot.forward.matrix},
                {"inverse_matrix_3x3", snapshot.inverse.matrix},
                {"score", snapshot.score},
                {"manual_rms_error", snapshot.manualRmsError},
                {"is_manual", snapshot.isManual}
            });
        }

        for (const IterationRecord& iteration : registration.iterations)
        {
            root["registrations"].back()["iterations"].push_back({
                {"index", iteration.index},
                {"score", iteration.score},
                {"tx", iteration.tx},
                {"ty", iteration.ty},
                {"theta", iteration.theta},
                {"scale", iteration.scale},
                {"sx", iteration.sx},
                {"sy", iteration.sy},
                {"converged", iteration.converged}
            });
        }

        for (const LandmarkPair& landmark : registration.landmarks)
        {
            root["registrations"].back()["landmarks"].push_back({
                {"moving_x", landmark.movingX},
                {"moving_y", landmark.movingY},
                {"fixed_x", landmark.fixedX},
                {"fixed_y", landmark.fixedY}
            });
        }
    }

    for (const AlignmentOperation& operation : session.operations)
    {
        nlohmann::json op = {
            {"id", operation.id},
            {"label", operation.label},
            {"timestamp", operation.timestamp},
            {"kind", ToString(operation.kind)},
            {"scope", ToString(operation.scope)},
            {"method", operation.method},
            {"affected_pairs", operation.affectedPairs},
            {"improved_pairs", operation.improvedPairs},
            {"worsened_pairs", operation.worsenedPairs},
            {"average_score", operation.averageScore},
            {"pairs", nlohmann::json::array()}
        };
        for (const OperationPairRef& pairRef : operation.pairs)
        {
            op["pairs"].push_back({
                {"fixed_index", pairRef.fixedIndex},
                {"moving_index", pairRef.movingIndex}
            });
        }
        root["operations"].push_back(std::move(op));
    }

    std::filesystem::create_directories(filePath.parent_path());

    std::ofstream output(filePath);
    if (!output)
    {
        return Result{false, "Could not open session file for writing."};
    }

    output << root.dump(2);
    return Result{};
}

Result SessionSerializer::Load(const std::filesystem::path& filePath, SessionModel& session) const
{
    std::ifstream input(filePath);
    if (!input)
    {
        return Result{false, "Could not open session file for reading."};
    }

    nlohmann::json root;
    input >> root;

    SessionModel loaded = CreateDefaultSession();
    loaded.projectName = root["project"].value("name", loaded.projectName);
    loaded.version = root["project"].value("version", loaded.version);
    loaded.workflowPhase = ParseWorkflowPhase(root["project"].value("workflow_phase", "setup"));
    loaded.nextOperationId = root["project"].value("next_operation_id", loaded.nextOperationId);
    loaded.projectPreferences.registrationPreset =
        root["project"].value("registration_preset", loaded.projectPreferences.registrationPreset);
    loaded.projectPreferences.autoAlignmentMethod =
        root["project"].value("auto_alignment_method", loaded.projectPreferences.autoAlignmentMethod);
    loaded.projectPreferences.maskMethod =
        root["project"].value("mask_method", loaded.projectPreferences.maskMethod);
    loaded.projectPreferences.scoreMethod =
        root["project"].value("score_method", loaded.projectPreferences.scoreMethod);
    loaded.projectPreferences.refinementMethod =
        root["project"].value("refinement_method", loaded.projectPreferences.refinementMethod);
    loaded.projectPreferences.transformType =
        root["project"].value("transform_type", loaded.projectPreferences.transformType);
    loaded.projectPreferences.maxIterations =
        root["project"].value("max_iterations", loaded.projectPreferences.maxIterations);
    loaded.projectPreferences.coarseLevels =
        root["project"].value("coarse_levels", loaded.projectPreferences.coarseLevels);
    loaded.projectPreferences.useAlignmentPreview =
        root["project"].value("use_alignment_preview", loaded.projectPreferences.useAlignmentPreview);
    loaded.projectPreferences.useSigmaRestrictedPriorRefinement =
        root["project"].value("use_sigma_restricted_prior_refinement",
                              loaded.projectPreferences.useSigmaRestrictedPriorRefinement);
    loaded.projectPreferences.sigmaMultiplier =
        root["project"].value("sigma_multiplier", loaded.projectPreferences.sigmaMultiplier);
    loaded.projectPreferences.preferManualPriors =
        root["project"].value("prefer_manual_priors", loaded.projectPreferences.preferManualPriors);

    if (root.contains("ui"))
    {
        loaded.uiPreferences.dpiMode = ParseDpiMode(root["ui"].value("dpi_mode", "auto"));
        loaded.uiPreferences.dpiOverride = root["ui"].value("dpi_override", loaded.uiPreferences.dpiOverride);
        loaded.uiPreferences.previewMode = ParsePreviewMode(root["ui"].value("preview_mode", "blend"));
        loaded.uiPreferences.blendAlpha = root["ui"].value("blend_alpha", loaded.uiPreferences.blendAlpha);
        loaded.uiPreferences.checkerSize = root["ui"].value("checker_size", loaded.uiPreferences.checkerSize);
        loaded.uiPreferences.syncViewports = root["ui"].value("sync_viewports", loaded.uiPreferences.syncViewports);
        loaded.uiPreferences.timelineHeight = root["ui"].value("timeline_height", loaded.uiPreferences.timelineHeight);
    }

    auto parseStack = [](const nlohmann::json& node, StackModel& stack)
    {
        stack.id = node.value("id", stack.id);
        stack.name = node.value("name", stack.name);
        stack.modality = node.value("modality", stack.modality);
        stack.directory = node.value("directory", stack.directory);
        stack.isDicom = node.value("is_dicom", stack.isDicom);
        stack.windowCenter = node.value("window_center", stack.windowCenter);
        stack.windowWidth = node.value("window_width", stack.windowWidth);
        stack.defaultWindowCenter = node.value("default_window_center", stack.defaultWindowCenter);
        stack.defaultWindowWidth = node.value("default_window_width", stack.defaultWindowWidth);
        stack.rescaleSlope = node.value("rescale_slope", stack.rescaleSlope);
        stack.rescaleIntercept = node.value("rescale_intercept", stack.rescaleIntercept);
        stack.slices.clear();
        if (node.contains("slices"))
        {
            for (const auto& item : node["slices"])
            {
                SliceRecord slice;
                slice.stackIndex = item.value("index", -1);
                slice.filePath = item.value("file_path", "");
                slice.fileName = item.value("file_name", "");
                slice.width = item.value("width", 0);
                slice.height = item.value("height", 0);
                slice.instanceNumber = item.value("instance_number", -1);
                slice.sliceLocation = item.value("slice_location", 0.0);
                slice.flipHorizontal = item.value("flip_horizontal", false);
                slice.flipVertical = item.value("flip_vertical", false);
                slice.rotationDegrees = item.value("rotation_degrees", 0);
                stack.slices.push_back(slice);
            }
        }
    };

    auto parsePairs = [](const nlohmann::json& pairsNode)
    {
        std::vector<PairRecord> pairs;
        for (const auto& item : pairsNode)
        {
            PairRecord pair;
            pair.fixedIndex = item.value("fixed_index", -1);
            pair.movingIndex = item.value("moving_index", -1);
            pair.valid = item.value("valid", false);
            pair.status = ParsePairStatus(item.value("status", "unmatched"));
            pairs.push_back(pair);
        }
        return pairs;
    };

    loaded.stacks.clear();
    if (root.contains("stacks") && root["stacks"].is_array())
    {
        for (const auto& node : root["stacks"])
        {
            StackModel stack;
            parseStack(node, stack);
            loaded.stacks.push_back(std::move(stack));
        }
    }

    loaded.pairings.clear();
    if (root.contains("pairings") && root["pairings"].is_array())
    {
        // Current (N-stack) schema.
        for (const auto& node : root["pairings"])
        {
            PairingModel pairing;
            pairing.fixedStackId = node.value("fixed_stack_id", pairing.fixedStackId);
            pairing.movingStackId = node.value("moving_stack_id", pairing.movingStackId);
            pairing.id = node.value("id", MakePairingId(pairing.fixedStackId, pairing.movingStackId));
            pairing.label = node.value("label", pairing.id);
            pairing.globalOffset = node.value("global_offset", pairing.globalOffset);
            pairing.fixedTimelineOffset = node.value("fixed_timeline_offset", pairing.fixedTimelineOffset);
            pairing.movingTimelineOffset = node.value("moving_timeline_offset", pairing.movingTimelineOffset);
            pairing.activeFixedIndex = node.value("active_fixed_index", pairing.activeFixedIndex);
            pairing.activeMovingIndex = node.value("active_moving_index", pairing.activeMovingIndex);
            if (node.contains("pairs"))
            {
                pairing.pairs = parsePairs(node["pairs"]);
            }
            loaded.pairings.push_back(std::move(pairing));
        }
        loaded.activePairingId = root.value("active_pairing_id", loaded.pairings.empty() ? std::string{} : loaded.pairings.front().id);
    }
    else if (root.contains("pairing"))
    {
        // Legacy (2-stack) schema: migrate the single fixed/moving pairing into the new list.
        const auto& node = root["pairing"];
        PairingModel pairing;
        pairing.fixedStackId = node.value("fixed_stack_id", pairing.fixedStackId);
        pairing.movingStackId = node.value("moving_stack_id", pairing.movingStackId);
        pairing.id = MakePairingId(pairing.fixedStackId, pairing.movingStackId);
        pairing.label = pairing.id;
        pairing.globalOffset = node.value("global_offset", pairing.globalOffset);
        pairing.fixedTimelineOffset = node.value("fixed_timeline_offset", pairing.fixedTimelineOffset);
        pairing.movingTimelineOffset = node.value("moving_timeline_offset", pairing.movingTimelineOffset);
        if (!node.contains("fixed_timeline_offset") && !node.contains("moving_timeline_offset"))
        {
            pairing.fixedTimelineOffset = 0;
            pairing.movingTimelineOffset = -pairing.globalOffset;
        }
        pairing.activeFixedIndex = root["project"].value("active_slice_a", 0);
        pairing.activeMovingIndex = root["project"].value("active_slice_b", 0);
        if (node.contains("pairs"))
        {
            pairing.pairs = parsePairs(node["pairs"]);
        }
        loaded.pairings.push_back(pairing);
        loaded.activePairingId = pairing.id;
    }

    if (loaded.pairings.empty() && loaded.stacks.size() >= 2)
    {
        // Sessions saved without any pairing info at all: default to pairing the first two stacks.
        PairingModel pairing;
        pairing.fixedStackId = loaded.stacks[0].id;
        pairing.movingStackId = loaded.stacks[1].id;
        pairing.id = MakePairingId(pairing.fixedStackId, pairing.movingStackId);
        pairing.label = pairing.id;
        loaded.pairings.push_back(pairing);
        loaded.activePairingId = pairing.id;
    }

    loaded.registrations.clear();
    if (root.contains("registrations"))
    {
        const std::string fallbackFixedStackId = loaded.pairings.empty() ? std::string{} : loaded.pairings.front().fixedStackId;
        const std::string fallbackMovingStackId = loaded.pairings.empty() ? std::string{} : loaded.pairings.front().movingStackId;
        for (const auto& item : root["registrations"])
        {
            RegistrationResult registration;
            registration.fixedStackId = item.value("fixed_stack_id", fallbackFixedStackId);
            registration.movingStackId = item.value("moving_stack_id", fallbackMovingStackId);
            registration.fixedIndex = item.value("fixed_index", -1);
            registration.movingIndex = item.value("moving_index", -1);
            registration.transformType = item.value("transform_type", "similarity");
            registration.score = item.value("score", 0.0);
            registration.manualRmsError = item.value("manual_rms_error", 0.0);
            registration.converged = item.value("converged", false);
            registration.isManual = item.value("is_manual", false);
            registration.isInterpolated = item.value("is_interpolated", false);
            registration.hasConvergencePrior = item.value("has_convergence_prior", false);
            registration.convergenceOutlier = item.value("convergence_outlier", false);
            registration.refinedWithPrior = item.value("refined_with_prior", false);
            registration.priorTx    = item.value("prior_tx", 0.0);
            registration.priorTy    = item.value("prior_ty", 0.0);
            registration.priorTheta = item.value("prior_theta", 0.0);
            registration.priorScale = item.value("prior_scale", 1.0);
            registration.priorTxMean = item.value("prior_tx_mean", registration.priorTx);
            registration.priorTyMean = item.value("prior_ty_mean", registration.priorTy);
            registration.priorThetaMean = item.value("prior_theta_mean", registration.priorTheta);
            registration.priorScaleMean = item.value("prior_scale_mean", registration.priorScale);
            registration.priorTxStdDev = item.value("prior_tx_stddev", -1.0);
            registration.priorTyStdDev = item.value("prior_ty_stddev", -1.0);
            registration.priorThetaStdDev = item.value("prior_theta_stddev", -1.0);
            registration.priorScaleStdDev = item.value("prior_scale_stddev", -1.0);
            registration.priorSx    = item.value("prior_sx", -1.0);
            registration.priorSy    = item.value("prior_sy", -1.0);
            registration.priorSxMean = item.value("prior_sx_mean", registration.priorSx);
            registration.priorSyMean = item.value("prior_sy_mean", registration.priorSy);
            registration.priorSxStdDev = item.value("prior_sx_stddev", -1.0);
            registration.priorSyStdDev = item.value("prior_sy_stddev", -1.0);
            registration.timestamp       = item.value("timestamp", "");
            registration.algorithmVersion = item.value("algorithm_version", "");
            if (item.contains("operation_log") && item["operation_log"].is_array())
            {
                for (const auto& entry : item["operation_log"])
                    registration.operationLog.push_back(entry.get<std::string>());
            }
            if (item.contains("history") && item["history"].is_array())
            {
                for (const auto& historyItem : item["history"])
                {
                    RegistrationSnapshot snapshot;
                    snapshot.label = historyItem.value("label", "");
                    snapshot.operationId = historyItem.value("operation_id", 0);
                    snapshot.timestamp = historyItem.value("timestamp", "");
                    snapshot.transformType = historyItem.value("transform_type", "similarity");
                    snapshot.score = historyItem.value("score", 0.0);
                    snapshot.manualRmsError = historyItem.value("manual_rms_error", 0.0);
                    snapshot.isManual = historyItem.value("is_manual", false);
                    if (historyItem.contains("forward_matrix_3x3") && historyItem["forward_matrix_3x3"].is_array() &&
                        historyItem["forward_matrix_3x3"].size() == 9)
                    {
                        for (size_t i = 0; i < 9; ++i)
                        {
                            snapshot.forward.matrix[i] = historyItem["forward_matrix_3x3"][i].get<double>();
                        }
                    }
                    if (historyItem.contains("inverse_matrix_3x3") && historyItem["inverse_matrix_3x3"].is_array() &&
                        historyItem["inverse_matrix_3x3"].size() == 9)
                    {
                        for (size_t i = 0; i < 9; ++i)
                        {
                            snapshot.inverse.matrix[i] = historyItem["inverse_matrix_3x3"][i].get<double>();
                        }
                    }
                    registration.history.push_back(std::move(snapshot));
                }
            }

            if (item.contains("forward_matrix_3x3") && item["forward_matrix_3x3"].is_array() &&
                item["forward_matrix_3x3"].size() == 9)
            {
                for (size_t i = 0; i < 9; ++i)
                {
                    registration.forward.matrix[i] = item["forward_matrix_3x3"][i].get<double>();
                }
            }

            if (item.contains("inverse_matrix_3x3") && item["inverse_matrix_3x3"].is_array() &&
                item["inverse_matrix_3x3"].size() == 9)
            {
                for (size_t i = 0; i < 9; ++i)
                {
                    registration.inverse.matrix[i] = item["inverse_matrix_3x3"][i].get<double>();
                }
            }

            if (item.contains("iterations"))
            {
                for (const auto& iterationItem : item["iterations"])
                {
                    IterationRecord iteration;
                    iteration.index = iterationItem.value("index", 0);
                    iteration.score     = iterationItem.value("score", 0.0);
                    iteration.tx        = iterationItem.value("tx", 0.0);
                    iteration.ty        = iterationItem.value("ty", 0.0);
                    iteration.theta     = iterationItem.value("theta", 0.0);
                    iteration.scale     = iterationItem.value("scale", 1.0);
                    iteration.sx        = iterationItem.value("sx", -1.0);
                    iteration.sy        = iterationItem.value("sy", -1.0);
                    iteration.converged = iterationItem.value("converged", false);
                    registration.iterations.push_back(iteration);
                }
            }

            if (item.contains("landmarks"))
            {
                for (const auto& landmarkItem : item["landmarks"])
                {
                    LandmarkPair landmark;
                    landmark.movingX = landmarkItem.value("moving_x", 0.0);
                    landmark.movingY = landmarkItem.value("moving_y", 0.0);
                    landmark.fixedX = landmarkItem.value("fixed_x", 0.0);
                    landmark.fixedY = landmarkItem.value("fixed_y", 0.0);
                    registration.landmarks.push_back(landmark);
                }
            }

            loaded.registrations.push_back(registration);
        }
    }

    if (root.contains("operations") && root["operations"].is_array())
    {
        for (const auto& item : root["operations"])
        {
            AlignmentOperation operation;
            operation.id = item.value("id", 0);
            operation.label = item.value("label", "");
            operation.timestamp = item.value("timestamp", "");
            operation.kind = ParseOperationKind(item.value("kind", "current_auto_alignment"));
            operation.scope = ParseOperationScope(item.value("scope", "single_pair"));
            operation.method = item.value("method", "");
            operation.affectedPairs = item.value("affected_pairs", 0);
            operation.improvedPairs = item.value("improved_pairs", 0);
            operation.worsenedPairs = item.value("worsened_pairs", 0);
            operation.averageScore = item.value("average_score", 0.0);
            if (item.contains("pairs") && item["pairs"].is_array())
            {
                for (const auto& pairItem : item["pairs"])
                {
                    OperationPairRef pairRef;
                    pairRef.fixedIndex = pairItem.value("fixed_index", -1);
                    pairRef.movingIndex = pairItem.value("moving_index", -1);
                    operation.pairs.push_back(pairRef);
                }
            }
            loaded.operations.push_back(std::move(operation));
        }
    }

    session = std::move(loaded);
    return Result{};
}
} // namespace align

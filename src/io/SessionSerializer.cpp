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
} // namespace

Result SessionSerializer::Save(const SessionModel& session, const std::filesystem::path& filePath) const
{
    nlohmann::json root;
    root["project"] = {
        {"name", session.projectName},
        {"version", session.version},
        {"workflow_phase", ToString(session.workflowPhase)},
        {"registration_preset", session.projectPreferences.registrationPreset},
        {"auto_alignment_method", session.projectPreferences.autoAlignmentMethod},
        {"mask_method", session.projectPreferences.maskMethod},
        {"score_method", session.projectPreferences.scoreMethod},
        {"refinement_method", session.projectPreferences.refinementMethod},
        {"transform_type", session.projectPreferences.transformType},
        {"max_iterations", session.projectPreferences.maxIterations},
        {"coarse_levels", session.projectPreferences.coarseLevels},
        {"use_alignment_preview", session.projectPreferences.useAlignmentPreview}
    };

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
        result["slices"] = nlohmann::json::array();
        for (const SliceRecord& slice : stack.slices)
        {
            result["slices"].push_back({
                {"index", slice.stackIndex},
                {"file_path", slice.filePath},
                {"file_name", slice.fileName},
                {"width", slice.width},
                {"height", slice.height}
            });
        }
        return result;
    };

    root["stacks"] = nlohmann::json::array({serializeStack(session.stackA), serializeStack(session.stackB)});
    root["pairing"] = {
        {"fixed_stack_id", session.pairing.fixedStackId},
        {"moving_stack_id", session.pairing.movingStackId},
        {"global_offset", session.pairing.globalOffset},
        {"fixed_timeline_offset", session.pairing.fixedTimelineOffset},
        {"moving_timeline_offset", session.pairing.movingTimelineOffset},
        {"pairs", nlohmann::json::array()}
    };

    for (const PairRecord& pair : session.pairing.pairs)
    {
        root["pairing"]["pairs"].push_back({
            {"fixed_index", pair.fixedIndex},
            {"moving_index", pair.movingIndex},
            {"valid", pair.valid},
            {"status", align::ToString(pair.status)}
        });
    }

    root["registrations"] = nlohmann::json::array();
    for (const RegistrationResult& registration : session.registrations)
    {
        root["registrations"].push_back({
            {"fixed_index", registration.fixedIndex},
            {"moving_index", registration.movingIndex},
            {"transform_type", registration.transformType},
            {"forward_matrix_3x3", registration.forward.matrix},
            {"inverse_matrix_3x3", registration.inverse.matrix},
            {"score", registration.score},
            {"manual_rms_error", registration.manualRmsError},
            {"converged", registration.converged},
            {"is_manual", registration.isManual},
            {"has_convergence_prior", registration.hasConvergencePrior},
            {"convergence_outlier", registration.convergenceOutlier},
            {"refined_with_prior", registration.refinedWithPrior},
            {"prior_tx", registration.priorTx},
            {"prior_ty", registration.priorTy},
            {"prior_theta", registration.priorTheta},
            {"prior_scale", registration.priorScale},
            {"prior_sx", registration.priorSx},
            {"prior_sy", registration.priorSy},
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

    if (root.contains("stacks") && root["stacks"].is_array() && root["stacks"].size() >= 2)
    {
        auto parseStack = [](const nlohmann::json& node, StackModel& stack)
        {
            stack.id = node.value("id", stack.id);
            stack.name = node.value("name", stack.name);
            stack.modality = node.value("modality", stack.modality);
            stack.directory = node.value("directory", stack.directory);
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
                    stack.slices.push_back(slice);
                }
            }
        };

        parseStack(root["stacks"][0], loaded.stackA);
        parseStack(root["stacks"][1], loaded.stackB);
    }

    if (root.contains("pairing"))
    {
        loaded.pairing.fixedStackId = root["pairing"].value("fixed_stack_id", loaded.pairing.fixedStackId);
        loaded.pairing.movingStackId = root["pairing"].value("moving_stack_id", loaded.pairing.movingStackId);
        loaded.pairing.globalOffset = root["pairing"].value("global_offset", loaded.pairing.globalOffset);
        loaded.pairing.fixedTimelineOffset =
            root["pairing"].value("fixed_timeline_offset", loaded.pairing.fixedTimelineOffset);
        loaded.pairing.movingTimelineOffset =
            root["pairing"].value("moving_timeline_offset", loaded.pairing.movingTimelineOffset);
        if (!root["pairing"].contains("fixed_timeline_offset") && !root["pairing"].contains("moving_timeline_offset"))
        {
            loaded.pairing.fixedTimelineOffset = 0;
            loaded.pairing.movingTimelineOffset = -loaded.pairing.globalOffset;
        }
        loaded.pairing.pairs.clear();
        if (root["pairing"].contains("pairs"))
        {
            for (const auto& item : root["pairing"]["pairs"])
            {
                PairRecord pair;
                pair.fixedIndex = item.value("fixed_index", -1);
                pair.movingIndex = item.value("moving_index", -1);
                pair.valid = item.value("valid", false);
                pair.status = ParsePairStatus(item.value("status", "unmatched"));
                loaded.pairing.pairs.push_back(pair);
            }
        }
    }

    loaded.registrations.clear();
    if (root.contains("registrations"))
    {
        for (const auto& item : root["registrations"])
        {
            RegistrationResult registration;
            registration.fixedIndex = item.value("fixed_index", -1);
            registration.movingIndex = item.value("moving_index", -1);
            registration.transformType = item.value("transform_type", "similarity");
            registration.score = item.value("score", 0.0);
            registration.manualRmsError = item.value("manual_rms_error", 0.0);
            registration.converged = item.value("converged", false);
            registration.isManual = item.value("is_manual", false);
            registration.hasConvergencePrior = item.value("has_convergence_prior", false);
            registration.convergenceOutlier = item.value("convergence_outlier", false);
            registration.refinedWithPrior = item.value("refined_with_prior", false);
            registration.priorTx    = item.value("prior_tx", 0.0);
            registration.priorTy    = item.value("prior_ty", 0.0);
            registration.priorTheta = item.value("prior_theta", 0.0);
            registration.priorScale = item.value("prior_scale", 1.0);
            registration.priorSx    = item.value("prior_sx", -1.0);
            registration.priorSy    = item.value("prior_sy", -1.0);
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

    session = std::move(loaded);
    return Result{};
}
} // namespace align

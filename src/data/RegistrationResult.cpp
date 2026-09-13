#include "data/RegistrationResult.h"

#include "core/Transform2DMath.h"

namespace align
{
void ResetRegistrationBase(RegistrationResult& registration)
{
    registration.baseForward = registration.forward;
    registration.baseInverse = registration.inverse;
    registration.manualAdjustment = Transform2D{};
    registration.hasManualAdjustment = false;
}

void UpdateRegistrationBasePreservingAdjustment(RegistrationResult& registration)
{
    registration.baseForward = registration.forward;
    registration.baseInverse = registration.inverse;
    if (registration.hasManualAdjustment)
    {
        registration.forward = ComposeTransform2D(registration.manualAdjustment, registration.baseForward);
        registration.inverse = InvertTransform2D(registration.forward);
    }
}

void AppendHistorySnapshot(RegistrationResult& registration, const std::string& label)
{
    RegistrationSnapshot snapshot;
    snapshot.label = label;
    snapshot.timestamp = registration.timestamp;
    snapshot.transformType = registration.transformType;
    snapshot.forward = registration.forward;
    snapshot.inverse = registration.inverse;
    snapshot.score = registration.score;
    snapshot.manualRmsError = registration.manualRmsError;
    snapshot.isManual = registration.isManual;
    registration.history.push_back(std::move(snapshot));
}

RegistrationResult* FindRegistrationResult(std::vector<RegistrationResult>& registrations,
                                           const StackId& fixedStackId,
                                           const StackId& movingStackId,
                                           SliceIndex fixedIndex,
                                           SliceIndex movingIndex)
{
    for (RegistrationResult& registration : registrations)
    {
        if (registration.fixedIndex == fixedIndex && registration.movingIndex == movingIndex &&
            registration.fixedStackId == fixedStackId && registration.movingStackId == movingStackId)
        {
            return &registration;
        }
    }

    return nullptr;
}

const RegistrationResult* FindRegistrationResult(const std::vector<RegistrationResult>& registrations,
                                                 const StackId& fixedStackId,
                                                 const StackId& movingStackId,
                                                 SliceIndex fixedIndex,
                                                 SliceIndex movingIndex)
{
    for (const RegistrationResult& registration : registrations)
    {
        if (registration.fixedIndex == fixedIndex && registration.movingIndex == movingIndex &&
            registration.fixedStackId == fixedStackId && registration.movingStackId == movingStackId)
        {
            return &registration;
        }
    }

    return nullptr;
}
} // namespace align

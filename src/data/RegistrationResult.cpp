#include "data/RegistrationResult.h"

namespace align
{
RegistrationResult* FindRegistrationResult(std::vector<RegistrationResult>& registrations,
                                           SliceIndex fixedIndex,
                                           SliceIndex movingIndex)
{
    for (RegistrationResult& registration : registrations)
    {
        if (registration.fixedIndex == fixedIndex && registration.movingIndex == movingIndex)
        {
            return &registration;
        }
    }

    return nullptr;
}

const RegistrationResult* FindRegistrationResult(const std::vector<RegistrationResult>& registrations,
                                                 SliceIndex fixedIndex,
                                                 SliceIndex movingIndex)
{
    for (const RegistrationResult& registration : registrations)
    {
        if (registration.fixedIndex == fixedIndex && registration.movingIndex == movingIndex)
        {
            return &registration;
        }
    }

    return nullptr;
}
} // namespace align

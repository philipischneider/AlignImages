#include "data/PairingModel.h"

namespace align
{
const char* ToString(PairStatus status)
{
    switch (status)
    {
    case PairStatus::Unmatched:
        return "unmatched";
    case PairStatus::Candidate:
        return "candidate";
    case PairStatus::Aligned:
        return "aligned";
    case PairStatus::Suspect:
        return "suspect";
    case PairStatus::Manual:
        return "manual";
    default:
        return "unmatched";
    }
}
} // namespace align

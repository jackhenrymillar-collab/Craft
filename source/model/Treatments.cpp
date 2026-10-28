#include "Treatments.h"

#include <algorithm>

namespace foundry
{
namespace
{
void clamp01 (double& v) noexcept { v = std::clamp (v, 0.0, 1.0); }
}

bool Treatments::isIdentity() const noexcept
{
    return age.amount <= 0.0 && deform.intensity <= 0.0 && twist.amount == 0.0
        && puncture.count <= 0.0;
}

void Treatments::clampToRange() noexcept
{
    clamp01 (age.amount); clamp01 (age.climate);
    clamp01 (deform.intensity); clamp01 (deform.size); clamp01 (deform.density);
    twist.amount = std::clamp (twist.amount, -1.0, 1.0);
    clamp01 (twist.pivot); clamp01 (twist.focus); clamp01 (twist.wrinkle);
    clamp01 (puncture.count); clamp01 (puncture.size); clamp01 (puncture.placement);
    clamp01 (puncture.scatter); clamp01 (puncture.ragged);
}

} // namespace foundry

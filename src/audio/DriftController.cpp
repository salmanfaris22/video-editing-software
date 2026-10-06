#include "audio/DriftController.h"

#include <algorithm>
#include <cmath>

namespace lectern::audio {

DriftController::Decision DriftController::evaluate(double targetPosition, double actualPosition) noexcept {
    Decision d;
    const double error = actualPosition - targetPosition;
    d.errorSamples = error;
    lastError_ = error;

    if (error < -config_.hardThresholdSamples) {
        // Behind: input samples are missing (gap) → fill with silence.
        d.action = Action::InsertSilence;
        d.samples = std::llround(-error);
        ++hardCorrections_;
        return d;
    }
    if (error > config_.hardThresholdSamples) {
        // Ahead: input overlaps already-written time → drop input.
        d.action = Action::DropInput;
        d.samples = std::llround(error);
        ++hardCorrections_;
        return d;
    }

    maxAbsSoftError_ = std::max(maxAbsSoftError_, std::fabs(error));
    const double maxDelta = config_.maxRatio * config_.compensationDistance;
    d.action = Action::Soft;
    d.samples = std::llround(std::clamp(-error, -maxDelta, maxDelta));
    d.compensationDistance = config_.compensationDistance;
    return d;
}

}  // namespace lectern::audio

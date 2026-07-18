#import <Foundation/Foundation.h>

#include "metrics/thermal_sampler.hpp"

namespace agentpulse {

std::string to_string(ThermalState state) {
    switch (state) {
        case ThermalState::Nominal:  return "nominal";
        case ThermalState::Fair:     return "fair";
        case ThermalState::Serious:  return "serious";
        case ThermalState::Critical: return "critical";
    }
    return "nominal";
}

ThermalState thermal_state_from_string(const std::string& name) {
    if (name == "fair") return ThermalState::Fair;
    if (name == "serious") return ThermalState::Serious;
    if (name == "critical") return ThermalState::Critical;
    return ThermalState::Nominal;
}

ThermalState sample_thermal_state() {
    @autoreleasepool {
        switch ([[NSProcessInfo processInfo] thermalState]) {
            case NSProcessInfoThermalStateNominal:
                return ThermalState::Nominal;
            case NSProcessInfoThermalStateFair:
                return ThermalState::Fair;
            case NSProcessInfoThermalStateSerious:
                return ThermalState::Serious;
            case NSProcessInfoThermalStateCritical:
                return ThermalState::Critical;
        }
    }
    return ThermalState::Nominal;
}

}  // namespace agentpulse

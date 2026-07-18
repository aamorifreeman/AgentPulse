#pragma once

#include <string>

namespace agentpulse {

// The system-wide thermal pressure, per Apple's ProcessInfo.thermalState.
// Ordered so callers (e.g. alert rules) can compare severity.
enum class ThermalState { Nominal = 0, Fair = 1, Serious = 2, Critical = 3 };

std::string to_string(ThermalState state);

// Parses a name ("nominal"/"fair"/"serious"/"critical") back to the enum;
// defaults to Nominal on anything unrecognized.
ThermalState thermal_state_from_string(const std::string& name);

// Reads the current thermal state (implemented in Objective-C++).
ThermalState sample_thermal_state();

}  // namespace agentpulse

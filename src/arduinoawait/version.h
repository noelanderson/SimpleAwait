#pragma once

// ArduinoAwait — library version.
//
// Semantic version of the ArduinoAwait library. Keep this in sync with the
// version fields in library.properties and library.json.

#define ARDUINOAWAIT_VERSION_MAJOR 0
#define ARDUINOAWAIT_VERSION_MINOR 1
#define ARDUINOAWAIT_VERSION_PATCH 0

#define ARDUINOAWAIT_VERSION_STRING "0.1.0"

namespace arduinoawait {

inline constexpr int version_major = ARDUINOAWAIT_VERSION_MAJOR;
inline constexpr int version_minor = ARDUINOAWAIT_VERSION_MINOR;
inline constexpr int version_patch = ARDUINOAWAIT_VERSION_PATCH;

} // namespace arduinoawait

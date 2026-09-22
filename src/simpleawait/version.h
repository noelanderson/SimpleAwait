#pragma once

// SimpleAwait — library version.
//
// Semantic version of the SimpleAwait library. Keep this in sync with the
// version fields in library.properties and library.json.

#define SIMPLEAWAIT_VERSION_MAJOR 1
#define SIMPLEAWAIT_VERSION_MINOR 0
#define SIMPLEAWAIT_VERSION_PATCH 0

#define SIMPLEAWAIT_VERSION_STRING "1.0.0"

namespace simpleawait {

inline constexpr int version_major = SIMPLEAWAIT_VERSION_MAJOR;
inline constexpr int version_minor = SIMPLEAWAIT_VERSION_MINOR;
inline constexpr int version_patch = SIMPLEAWAIT_VERSION_PATCH;

} // namespace simpleawait

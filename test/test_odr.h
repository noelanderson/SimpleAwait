#pragma once

// Shared declarations for the two-translation-unit ODR test. Both TUs include
// the public header; tu2 defines these accessors and tu1 (main) uses them to
// confirm the header-only library has a single definition of its inline
// entities across translation units.

#include <SimpleAwait.h>

using sa_handler_ptr = void (*)(int) noexcept;

const int* sa_tu2_version_major_addr();
sa_handler_ptr sa_tu2_error_handler_addr();
int sa_tu2_max_tasks();

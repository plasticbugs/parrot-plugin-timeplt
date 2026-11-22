// MSVC has no unistd.h. host.c includes it for pread and pwrite, which
// the simulator replaces (model.c), so this is all it needs.
#pragma once
#include <io.h>
#include <stddef.h>
typedef ptrdiff_t ssize_t;

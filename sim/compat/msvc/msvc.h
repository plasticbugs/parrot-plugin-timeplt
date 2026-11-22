// Included first in every file when building with MSVC (sim/CMakeLists.txt).
// The SDK's headers mark a few functions with GCC's attributes; MSVC has
// none, and none of those the SDK uses changes a struct's layout.
#pragma once
#define __attribute__(x)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

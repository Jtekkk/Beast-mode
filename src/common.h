#ifndef beast_common_h
#define beast_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// NaN boxing is the default value representation. Build with
// -DBEAST_NO_NAN_BOXING to use the tagged-union fallback (kept alive in CI
// so exotic platforms always have a working build).
#ifndef BEAST_NO_NAN_BOXING
#define NAN_BOXING
#endif

// Computed-goto dispatch needs the labels-as-values GNU extension.
#if defined(__GNUC__) || defined(__clang__)
#define BEAST_COMPUTED_GOTO
#endif

#define FRAMES_MAX 1024
#define STACK_MAX (FRAMES_MAX * 256)

#define UINT8_COUNT (UINT8_MAX + 1)

#endif

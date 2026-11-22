#ifndef OPENTONY_HAL_SDL_INCLUDE_H
#define OPENTONY_HAL_SDL_INCLUDE_H

/* Include SDL3 the same way in every backend file.
 *
 * The hook DLL compiles with clang-cl against the Visual C++ 6.0 headers:
 * - SDL_stdinc.h would include <sal.h> (newer than VC6); SDL provides this
 *   switch to leave its analysis macros empty instead;
 * - SDL's byte-swap and SIMD headers pull in clang's <intrin.h>, whose inline
 *   helpers mention _aligned_malloc/_aligned_free. VC6's CRT headers do not
 *   declare them; nothing calls them, so declarations are enough. */
#ifndef SDL_DISABLE_ANALYZE_MACROS
#define SDL_DISABLE_ANALYZE_MACROS 1
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *__cdecl _aligned_malloc(size_t size, size_t alignment);
void __cdecl _aligned_free(void *memory);
#ifdef __cplusplus
}
#endif
#endif

#include <SDL3/SDL.h>

#endif /* OPENTONY_HAL_SDL_INCLUDE_H */

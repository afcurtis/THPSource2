#ifndef OPENTONY_HAL_SDL_API_H
#define OPENTONY_HAL_SDL_API_H

/* The SDL3 functions the backends use, called through OT_SDL(name).
 *
 * In the hook DLL (OT_SDL_DYNAMIC=1) SDL3.dll is loaded only when a [hal]
 * service selects SDL, so opentony.dll has no link-time dependency on it and
 * a stock configuration never touches SDL. The pointer types come from the
 * SDL headers themselves (decltype), so a signature mismatch is a compile
 * error. A standalone build links SDL3 and defines OT_SDL_DYNAMIC=0. */

#include "sdl_include.h"

#ifndef OT_SDL_DYNAMIC
#define OT_SDL_DYNAMIC 1
#endif

#define OT_SDL_FUNCTIONS(X) \
    X(SDL_Init)             \
    X(SDL_Quit)             \
    X(SDL_InitSubSystem)    \
    X(SDL_QuitSubSystem)    \
    X(SDL_WasInit)          \
    X(SDL_GetVersion)       \
    X(SDL_GetRevision)      \
    X(SDL_GetError)         \
    X(SDL_SetHint)          \
    X(SDL_GetTicksNS)       \
    X(SDL_GetScancodeName)  \
    X(SDL_DelayNS)          \
    X(SDL_DelayPrecise)     \
    X(SDL_GetPerformanceCounter) \
    X(SDL_GetPerformanceFrequency) \
    X(SDL_free)                  \
    X(SDL_malloc)                \
    X(SDL_strlcpy)               \
    X(SDL_GlobDirectory)         \
    X(SDL_LoadPNG)               \
    X(SDL_ConvertSurface)        \
    X(SDL_DestroySurface)        \
    X(SDL_CreateProperties)      \
    X(SDL_DestroyProperties)     \
    X(SDL_SetPointerProperty)    \
    X(SDL_SetStringProperty)     \
    X(SDL_SetNumberProperty)     \
    X(SDL_SetBooleanProperty)    \
    X(SDL_GetPointerProperty)    \
    X(SDL_CreateWindowWithProperties) \
    X(SDL_DestroyWindow)         \
    X(SDL_GetWindowProperties)   \
    X(SDL_GetWindowID)           \
    X(SDL_GetWindowSize)         \
    X(SDL_GetWindowSizeInPixels) \
    X(SDL_GetPrimaryDisplay)     \
    X(SDL_GetDisplayUsableBounds) \
    X(SDL_GetDisplayBounds)      \
    X(SDL_GetDesktopDisplayMode) \
    X(SDL_SetWindowTitle)        \
    X(SDL_SetWindowFullscreen)   \
    X(SDL_GetDisplayForWindow)   \
    X(SDL_GetClosestFullscreenDisplayMode) \
    X(SDL_SetWindowFullscreenMode) \
    X(SDL_ShowCursor)            \
    X(SDL_HideCursor)            \
    X(SDL_PumpEvents)            \
    X(SDL_PollEvent)             \
    X(SDL_GetKeyboardState)      \
    X(SDL_GetKeyboardFocus)      \
    X(SDL_GetMouseState)         \
    X(SDL_GetRelativeMouseState) \
    X(SDL_WarpMouseInWindow)     \
    X(SDL_GetGamepads)           \
    X(SDL_OpenGamepad)           \
    X(SDL_CloseGamepad)          \
    X(SDL_GetGamepadID)          \
    X(SDL_GetGamepadName)        \
    X(SDL_GetGamepadButton)      \
    X(SDL_GetGamepadAxis)        \
    X(SDL_RumbleGamepad)         \
    X(SDL_AttachVirtualJoystick) \
    X(SDL_DetachVirtualJoystick) \
    X(SDL_OpenJoystick)          \
    X(SDL_CloseJoystick)         \
    X(SDL_SetJoystickVirtualButton) \
    X(SDL_SetJoystickVirtualAxis) \
    X(SDL_GL_SetAttribute)       \
    X(SDL_GL_CreateContext)      \
    X(SDL_GL_MakeCurrent)        \
    X(SDL_GL_GetProcAddress)     \
    X(SDL_GL_DestroyContext)     \
    X(SDL_GL_SwapWindow)         \
    X(SDL_GL_SetSwapInterval)    \
    X(SDL_GetCurrentAudioDriver) \
    X(SDL_GetNumAudioDrivers)    \
    X(SDL_GetAudioDriver)        \
    X(SDL_OpenAudioDevice)       \
    X(SDL_CloseAudioDevice)      \
    X(SDL_ResumeAudioDevice)     \
    X(SDL_GetAudioDeviceFormat)  \
    X(SDL_SetAudioDeviceGain)    \
    X(SDL_CreateAudioStream)     \
    X(SDL_DestroyAudioStream)    \
    X(SDL_BindAudioStream)       \
    X(SDL_UnbindAudioStream)     \
    X(SDL_SetAudioStreamGetCallback) \
    X(SDL_PutAudioStreamData)    \
    X(SDL_GetAudioStreamQueued)  \
    X(SDL_FlushAudioStream)      \
    X(SDL_ClearAudioStream)      \
    X(SDL_SetAudioStreamFrequencyRatio) \
    X(SDL_SetAudioStreamGain)    \
    X(SDL_CreateMutex)           \
    X(SDL_DestroyMutex)          \
    X(SDL_LockMutex)             \
    X(SDL_UnlockMutex)           \
    X(SDL_CreateGPUDevice)       \
    X(SDL_DestroyGPUDevice)      \
    X(SDL_GetGPUDeviceDriver)    \
    X(SDL_GetGPUShaderFormats)   \
    X(SDL_CreateGPUShader)       \
    X(SDL_ReleaseGPUShader)      \
    X(SDL_CreateGPUGraphicsPipeline) \
    X(SDL_ReleaseGPUGraphicsPipeline) \
    X(SDL_CreateGPUSampler)      \
    X(SDL_ReleaseGPUSampler)     \
    X(SDL_CreateGPUTexture)      \
    X(SDL_ReleaseGPUTexture)     \
    X(SDL_CreateGPUBuffer)       \
    X(SDL_ReleaseGPUBuffer)      \
    X(SDL_CreateGPUTransferBuffer) \
    X(SDL_ReleaseGPUTransferBuffer) \
    X(SDL_MapGPUTransferBuffer)  \
    X(SDL_UnmapGPUTransferBuffer) \
    X(SDL_AcquireGPUCommandBuffer) \
    X(SDL_PushGPUVertexUniformData) \
    X(SDL_PushGPUFragmentUniformData) \
    X(SDL_BeginGPUCopyPass)      \
    X(SDL_UploadToGPUTexture)    \
    X(SDL_UploadToGPUBuffer)     \
    X(SDL_EndGPUCopyPass)        \
    X(SDL_BeginGPURenderPass)    \
    X(SDL_BindGPUGraphicsPipeline) \
    X(SDL_SetGPUViewport)        \
    X(SDL_SetGPUScissor)         \
    X(SDL_BindGPUVertexBuffers)  \
    X(SDL_BindGPUFragmentSamplers) \
    X(SDL_DrawGPUPrimitives)     \
    X(SDL_EndGPURenderPass)      \
    X(SDL_BlitGPUTexture)        \
    X(SDL_WindowSupportsGPUPresentMode) \
    X(SDL_ClaimWindowForGPUDevice) \
    X(SDL_ReleaseWindowFromGPUDevice) \
    X(SDL_SetGPUSwapchainParameters) \
    X(SDL_WaitAndAcquireGPUSwapchainTexture) \
    X(SDL_SubmitGPUCommandBuffer) \
    X(SDL_GPUTextureSupportsFormat) \
    X(SDL_DownloadFromGPUTexture) \
    X(SDL_WaitForGPUIdle)

#if OT_SDL_DYNAMIC
struct OtSdlApi {
    int loaded;
#define OT_SDL_POINTER(name) decltype(&::name) name;
    OT_SDL_FUNCTIONS(OT_SDL_POINTER)
#undef OT_SDL_POINTER
};

extern OtSdlApi ot_sdl_api;

/* Loads SDL3.dll from `directory` (with a trailing separator; 0 = the search
 * path only) and resolves every function above (sdl_loader.cpp). Returns 1,
 * or 0 with `*missing` naming the DLL or the first function not found. */
int ot_sdl_load(const char *directory, const char **missing);
#define OT_SDL(name) (ot_sdl_api.name)
#else
#define OT_SDL(name) (::name)
#endif

#endif /* OPENTONY_HAL_SDL_API_H */

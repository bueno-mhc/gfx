/// @file renderdoc_capture.cpp
/// @brief RenderDoc's in-application API: found only when RenderDoc already injected itself into the
///        process (the library never loads RenderDoc on its own). A plain translation unit: it includes
///        <windows.h>.
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <renderdoc_app.h>

#include "renderdoc_capture.hpp"

namespace gfx::vulkan::renderdoc
{

namespace
{

/// @brief The API of the RenderDoc injected into the process, or null.
RENDERDOC_API_1_6_0*
api() noexcept
{
    static RENDERDOC_API_1_6_0* const loaded = []() -> RENDERDOC_API_1_6_0*
    {
        // RenderDoc's module, only if it is already loaded.
#if defined(_WIN32)
        HMODULE module = GetModuleHandleA("renderdoc.dll");
        if (module == nullptr)
        {
            return nullptr;
        }
        const auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(reinterpret_cast<void (*)()>(GetProcAddress(module, "RENDERDOC_GetAPI")));
#else
        void* module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
        if (module == nullptr)
        {
            return nullptr;
        }
        const auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(module, "RENDERDOC_GetAPI"));
#endif

        // Version 1.6 of its API.
        RENDERDOC_API_1_6_0* result = nullptr;
        if (get_api == nullptr || get_api(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&result)) != 1)
        {
            return nullptr;
        }
        return result;
    }();
    return loaded;
}

} // namespace

bool
is_attached() noexcept
{
    return api() != nullptr;
}

bool
start_capture(void* instance,
              const char* title) noexcept
{
    RENDERDOC_API_1_6_0* renderdoc = api();
    if (renderdoc == nullptr)
    {
        return false;
    }
    renderdoc->StartFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance), nullptr);
    if (title != nullptr)
    {
        renderdoc->SetCaptureTitle(title);
    }
    return true;
}

bool
end_capture(void* instance) noexcept
{
    RENDERDOC_API_1_6_0* renderdoc = api();
    return renderdoc != nullptr && renderdoc->EndFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance), nullptr) == 1;
}

} // namespace gfx::vulkan::renderdoc

// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIRenderer.h"

#ifdef NOMADUI_OPENGL
#include "OpenGL/NUIRendererGL.h"
#endif

#ifdef NOMADUI_VULKAN
#include "Vulkan/NUIRendererVK.h"
#endif

namespace NomadUI {

std::unique_ptr<NUIRenderer> createRenderer() {
#ifdef NOMADUI_OPENGL
    return std::make_unique<NUIRendererGL>();
#elif defined(NOMADUI_VULKAN)
    return std::make_unique<NUIRendererVK>();
#else
    // No renderer backend - return nullptr
    // This allows core classes to be tested without OpenGL
    return nullptr;
#endif
}

} // namespace NomadUI

// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIRenderer.h"

#ifdef AestraUI_OPENGL
#include "OpenGL/NUIRendererGL.h"
#endif

#ifdef AestraUI_VULKAN
#include "Vulkan/NUIRendererVK.h"
#endif

namespace AestraUI {

std::unique_ptr<NUIRenderer> createRenderer() {
#ifdef AestraUI_OPENGL
    return std::make_unique<NUIRendererGL>();
#elif defined(AestraUI_VULKAN)
    return std::make_unique<NUIRendererVK>();
#else
    // No renderer backend - return nullptr
    // This allows core classes to be tested without OpenGL
    return nullptr;
#endif
}

} // namespace AestraUI

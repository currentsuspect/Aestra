// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIRenderer.h"

#ifdef AestraUI_OPENGL
#include "OpenGL/NUIRendererGL.h"
#endif

namespace AestraUI {

std::unique_ptr<NUIRenderer> createRenderer() {
#ifdef AestraUI_OPENGL
    return std::make_unique<NUIRendererGL>();
#else
    return nullptr;
#endif
}

} // namespace AestraUI

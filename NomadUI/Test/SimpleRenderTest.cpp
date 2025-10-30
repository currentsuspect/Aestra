// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * Simple Render Test - Test basic OpenGL rendering
 * This creates a simple colored rectangle to verify OpenGL is working
 */

#include <iostream>
#include <memory>
#include <chrono>

// Include stddef.h first to get standard ptrdiff_t
#include <cstddef>
#include <cstdint>

// Windows headers first with proper macro definitions
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOCOMM
#include <Windows.h>

// GLAD must be included after Windows headers to avoid macro conflicts
#include <glad/glad.h>

// Suppress APIENTRY redefinition warning - both define the same value
#pragma warning(push)
#pragma warning(disable: 4005)
// Windows.h redefines APIENTRY but it's the same value, so we can ignore the warning
#pragma warning(pop)

// NomadUI includes
#include "Core/NUIComponent.h"
#include "Graphics/OpenGL/NUIRendererGL.h"
#include "Platform/NUIPlatformBridge.h"

using namespace NomadUI;
using NUIWindowWin32 = NUIPlatformBridge;  // Compatibility typedef

class SimpleRenderTest : public NUIComponent
{
public:
    SimpleRenderTest()
    {
        setSize(400, 300);
    }

    void onRender(NUIRenderer& renderer) override
    {
        auto bounds = getBounds();
        
        // Draw a bright red background
        renderer.fillRect(bounds, NUIColor::fromHex(0xffFF0000));
        
        // Draw a green rectangle in the center
        NUIRect centerRect = {
            bounds.x + bounds.getWidth() * 0.25f,
            bounds.y + bounds.getHeight() * 0.25f,
            bounds.getWidth() * 0.5f,
            bounds.getHeight() * 0.5f
        };
        renderer.fillRect(centerRect, NUIColor::fromHex(0xff00FF00));
        
        // Draw a blue circle
        NUIPoint center = {bounds.x + bounds.getWidth() * 0.5f, bounds.y + bounds.getHeight() * 0.5f};
        renderer.fillCircle(center, 50.0f, NUIColor::fromHex(0xff0000FF));
        
        // Draw a white border
        renderer.strokeRect(bounds, 5.0f, NUIColor::fromHex(0xffFFFFFF));
    }
};

int main()
{
    std::cout << "==================================" << std::endl;
    std::cout << "  NomadUI - Simple Render Test" << std::endl;
    std::cout << "==================================" << std::endl;

    try
    {
        // Create window first (this creates the OpenGL context)
        auto window = std::make_unique<NUIWindowWin32>();
        if (!window->create("NomadUI Simple Render Test", 400, 300))
        {
            std::cerr << "Failed to create window!" << std::endl;
            return -1;
        }

        // Make the OpenGL context current
        if (!window->makeContextCurrent())
        {
            std::cerr << "Failed to make OpenGL context current!" << std::endl;
            return -1;
        }

        // Now create and initialize the OpenGL renderer
        auto renderer = std::make_unique<NUIRendererGL>();
        if (!renderer->initialize(400, 300))
        {
            std::cerr << "Failed to initialize OpenGL renderer!" << std::endl;
            return -1;
        }

        // Show the window
        window->show();

        // Create test component
        auto test = std::make_unique<SimpleRenderTest>();

        std::cout << "Window created and shown successfully!" << std::endl;
        std::cout << "You should see a RED background with GREEN rectangle and BLUE circle!" << std::endl;
        std::cout << "Close the window to exit." << std::endl;
        std::cout << std::endl;

        // Main loop
        auto lastTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;

        while (window->processEvents())
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            // Render
            renderer->beginFrame();
            test->onRender(*renderer);
            renderer->endFrame();
            window->swapBuffers();

            // FPS counter
            frameCount++;
            if (frameCount % 60 == 0)
            {
                float fps = 1.0f / deltaTime;
                std::cout << "FPS: " << static_cast<int>(fps) << std::endl;
            }

            // Small delay to prevent 100% CPU usage
            Sleep(16); // ~60 FPS
        }

        std::cout << std::endl;
        std::cout << "==================================" << std::endl;
        std::cout << "  Test closed successfully!" << std::endl;
        std::cout << "==================================" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}

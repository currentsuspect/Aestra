#pragma once

#include <glad/glad.h>
#include <vector>
#include <memory>

namespace AestraUI {

/**
 * @brief Handles the creation of the frosted glass effect.
 * Captures the current framebuffer, downsamples it, applies a high-quality
 * Gaussian blur, and makes it available for the UI rendering pass.
 */
class GlassmorphismPass {
public:
    GlassmorphismPass();
    ~GlassmorphismPass();

    /**
     * @brief Initialize FBOs and Shaders.
     * @param width Screen width
     * @param height Screen height
     */
    bool initialize(int width, int height);

    /**
     * @brief Resize internal FBOs.
     */
    void resize(int width, int height);

    /**
     * @brief Capture the current framebuffer content and blur it.
     * Call this before rendering the glass UI layer.
     * @param sourceFBO The framebuffer to capture from (0 for default backbuffer).
     */
    void execute(GLuint sourceFBO);

    /**
     * @brief Get the blurred texture ID.
     */
    GLuint getBlurredTexture() const { return pingPongTextures_[0]; }

private:
    void createResources(int width, int height);
    void cleanup();
    bool loadShaders();

    int width_ = 0;
    int height_ = 0;
    
    // Downsample factor for performance and broader blur
    const float kDownsampleFactor_ = 0.5f;

    // FBOs
    GLuint sceneCaptureFBO_ = 0;
    GLuint sceneCaptureTexture_ = 0;

    // Ping-Pong FBOs for separable Gaussian inner-loop
    GLuint pingPongFBOs_[2] = {0, 0};
    GLuint pingPongTextures_[2] = {0, 0};

    // Shaders using raw OpenGL ID
    GLuint blurShader_ = 0;
    GLuint screenQuadVAO_ = 0;
    GLuint screenQuadVBO_ = 0;
};

} // namespace AestraUI

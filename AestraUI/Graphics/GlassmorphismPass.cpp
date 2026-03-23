#include "GlassmorphismPass.h"
#include <iostream>
#include <string>

namespace AestraUI {

// -----------------------------------------------------------------------------
// Shader Sources
// -----------------------------------------------------------------------------

static const char* kBlurVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
}
)";

// Optimized 9-tap Gaussian Blur (Linear Sampling could reduce taps, but this is explicit)
static const char* kBlurFragmentShader = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D image;
uniform bool horizontal;
uniform float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {             
    vec2 tex_offset = 1.0 / textureSize(image, 0); // gets size of single texel
    vec3 result = texture(image, TexCoords).rgb * weight[0]; // current fragment's contribution
    
    if(horizontal) {
        for(int i = 1; i < 5; ++i) {
            result += texture(image, TexCoords + vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
            result += texture(image, TexCoords - vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
        }
    } else {
        for(int i = 1; i < 5; ++i) {
            result += texture(image, TexCoords + vec2(0.0, tex_offset.y * i)).rgb * weight[i];
            result += texture(image, TexCoords - vec2(0.0, tex_offset.y * i)).rgb * weight[i];
        }
    }
    FragColor = vec4(result, 1.0);
}
)";

// -----------------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------------

GlassmorphismPass::GlassmorphismPass() {}

GlassmorphismPass::~GlassmorphismPass() {
    cleanup();
}

bool GlassmorphismPass::initialize(int width, int height) {
    if (!loadShaders()) return false;
    
    // Setup Screen Quad
    float quadVertices[] = { 
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    
    glGenVertexArrays(1, &screenQuadVAO_);
    glGenBuffers(1, &screenQuadVBO_);
    glBindVertexArray(screenQuadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, screenQuadVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    resize(width, height);
    return true;
}

void GlassmorphismPass::cleanup() {
    if (sceneCaptureFBO_) glDeleteFramebuffers(1, &sceneCaptureFBO_);
    if (sceneCaptureTexture_) glDeleteTextures(1, &sceneCaptureTexture_);
    if (pingPongFBOs_[0]) glDeleteFramebuffers(2, pingPongFBOs_);
    if (pingPongTextures_[0]) glDeleteTextures(2, pingPongTextures_);
    if (blurShader_) glDeleteProgram(blurShader_);
    if (screenQuadVAO_) glDeleteVertexArrays(1, &screenQuadVAO_);
    if (screenQuadVBO_) glDeleteBuffers(1, &screenQuadVBO_);
}

void GlassmorphismPass::resize(int width, int height) {
    width_ = width;
    height_ = height;
    createResources(width, height);
}

void GlassmorphismPass::createResources(int width, int height) {
    // Cleanup old resources if resizing
    if (sceneCaptureFBO_) glDeleteFramebuffers(1, &sceneCaptureFBO_);
    if (sceneCaptureTexture_) glDeleteTextures(1, &sceneCaptureTexture_);
    if (pingPongFBOs_[0]) glDeleteFramebuffers(2, pingPongFBOs_);
    if (pingPongTextures_[0]) glDeleteTextures(2, pingPongTextures_);

    int w = static_cast<int>(width * kDownsampleFactor_);
    int h = static_cast<int>(height * kDownsampleFactor_);
    
    // 1. Capture FBO
    glGenFramebuffers(1, &sceneCaptureFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneCaptureFBO_);
    
    glGenTextures(1, &sceneCaptureTexture_);
    glBindTexture(GL_TEXTURE_2D, sceneCaptureTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // Linear for downsample
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneCaptureTexture_, 0);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "GlassmorphismPass: Capture FBO not complete!" << std::endl;
        
    // 2. PingPong FBOs
    glGenFramebuffers(2, pingPongFBOs_);
    glGenTextures(2, pingPongTextures_);
    
    for (unsigned int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingPongFBOs_[i]);
        glBindTexture(GL_TEXTURE_2D, pingPongTextures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pingPongTextures_[i], 0);
        
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "GlassmorphismPass: PingPong FBO " << i << " not complete!" << std::endl;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GlassmorphismPass::loadShaders() {
    // Basic Shader compilation helpers
    auto compile = [](GLuint type, const char* src) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, NULL);
        glCompileShader(shader);
        int success; char infoLog[512];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if(!success) {
            glGetShaderInfoLog(shader, 512, NULL, infoLog);
            std::cerr << "Glassmorphism Shader Error: " << infoLog << std::endl;
        }
        return shader;
    };
    
    GLuint vs = compile(GL_VERTEX_SHADER, kBlurVertexShader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kBlurFragmentShader);
    
    blurShader_ = glCreateProgram();
    glAttachShader(blurShader_, vs);
    glAttachShader(blurShader_, fs);
    glLinkProgram(blurShader_);
    
    int success; char infoLog[512];
    glGetProgramiv(blurShader_, GL_LINK_STATUS, &success);
    if(!success) {
        glGetProgramInfoLog(blurShader_, 512, NULL, infoLog);
        std::cerr << "Glassmorphism Program Link Error: " << infoLog << std::endl;
        return false;
    }
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    return true;
}

void GlassmorphismPass::execute(GLuint sourceFBO) {
    if (width_ == 0 || height_ == 0) return;

    GLint originalFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &originalFBO);
    
    GLint originalViewport[4];
    glGetIntegerv(GL_VIEWPORT, originalViewport);

    int w = static_cast<int>(width_ * kDownsampleFactor_);
    int h = static_cast<int>(height_ * kDownsampleFactor_);

    // 1. Blit source to capture FBO (Downsample)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneCaptureFBO_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // 2. Blur Loop
    bool horizontal = true;
    bool first_iteration = true;
    int amount = 10; // 5 horizontal, 5 vertical passes for creamy smoothness
    
    glUseProgram(blurShader_);
    glBindVertexArray(screenQuadVAO_);
    glViewport(0, 0, w, h);
    
    for (unsigned int i = 0; i < amount; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, pingPongFBOs_[horizontal]); 
        glUniform1i(glGetUniformLocation(blurShader_, "horizontal"), horizontal);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, first_iteration ? sceneCaptureTexture_ : pingPongTextures_[!horizontal]);
        
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        horizontal = !horizontal;
        if (first_iteration) first_iteration = false;
    }
    
    glBindVertexArray(0);
    glUseProgram(0);

    // Restore state
    glBindFramebuffer(GL_FRAMEBUFFER, originalFBO);
    glViewport(originalViewport[0], originalViewport[1], originalViewport[2], originalViewport[3]);
    
    // Result is in pingPongTextures_[!horizontal]
    // We swap it to index 0 for easy retrieval if needed, or just remember the index
    if (horizontal) { // The *next* pass would be horizontal, so the *last* write was vertical (index 0)
       // Result is in 0. No swap needed if logic holds.
       // Actually if horizontal is true here, it means loop finished with horizontal = !horizontal (false -> true).
       // So the last loop ran with horizontal = false (Vertical).
       // pingPongFBOs_[0] was written to.
    } else {
       // Last write was horizontal (index 1).
       // Swap texture handles so index 0 always has the result
       std::swap(pingPongTextures_[0], pingPongTextures_[1]);
       std::swap(pingPongFBOs_[0], pingPongFBOs_[1]);
    }
}

} // namespace AestraUI

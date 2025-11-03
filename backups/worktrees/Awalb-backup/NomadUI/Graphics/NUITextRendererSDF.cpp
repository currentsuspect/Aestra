// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUITextRendererSDF.h"
#include "../External/glad/include/glad/glad.h"
#include <iostream>
#include <cmath>

namespace NomadUI {

// SDF text rendering shader
static const char* sdfVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* sdfFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec4 uColor;
uniform float uSmoothness;

void main() {
    // Sample the distance field
    float distance = texture(uTexture, vTexCoord).a;
    
    // Smooth anti-aliasing using smoothstep
    float alpha = smoothstep(0.5 - uSmoothness, 0.5 + uSmoothness, distance);
    
    // Output color with alpha
    FragColor = vec4(uColor.rgb, uColor.a * alpha);
}
)";

NUITextRendererSDF::NUITextRendererSDF()
    : initialized_(false)
    , atlasTexture_(0)
    , shaderProgram_(0)
    , vao_(0)
    , vbo_(0)
    , atlasWidth_(512.0f)
    , atlasHeight_(512.0f)
{
}

NUITextRendererSDF::~NUITextRendererSDF() {
    shutdown();
}

bool NUITextRendererSDF::initialize() {
    if (initialized_) return true;

    // Create shader
    if (!createShader()) {
        std::cerr << "Failed to create SDF text shader" << std::endl;
        return false;
    }

    // Create VAO and VBO for text quads
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    // TexCoord attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    // For now, create a simple placeholder atlas
    // TODO: Load actual SDF font atlas
    glGenTextures(1, &atlasTexture_);
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    initialized_ = true;
    std::cout << "âœ“ SDF Text Renderer initialized" << std::endl;
    return true;
}

void NUITextRendererSDF::shutdown() {
    if (atlasTexture_) {
        glDeleteTextures(1, &atlasTexture_);
        atlasTexture_ = 0;
    }

    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }

    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    if (shaderProgram_) {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }

    initialized_ = false;
}

void NUITextRendererSDF::drawText(
    const std::string& text,
    const NUIPoint& position,
    float fontSize,
    const NUIColor& color)
{
    if (!initialized_) return;

    // TODO: Implement actual SDF text rendering
    // For now, this is a placeholder
}

NUISize NUITextRendererSDF::measureText(const std::string& text, float fontSize) {
    if (!initialized_) return {0, 0};

    // TODO: Implement proper text measurement
    return {
        static_cast<float>(text.length() * fontSize * 0.6f),
        fontSize
    };
}

bool NUITextRendererSDF::createShader() {
    // Compile vertex shader
    uint32_t vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &sdfVertexShader, nullptr);
    glCompileShader(vertexShader);

    int success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "SDF Vertex Shader compilation failed: " << infoLog << std::endl;
        return false;
    }

    // Compile fragment shader
    uint32_t fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &sdfFragmentShader, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "SDF Fragment Shader compilation failed: " << infoLog << std::endl;
        return false;
    }

    // Link shader program
    shaderProgram_ = glCreateProgram();
    glAttachShader(shaderProgram_, vertexShader);
    glAttachShader(shaderProgram_, fragmentShader);
    glLinkProgram(shaderProgram_);

    glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(shaderProgram_, 512, nullptr, infoLog);
        std::cerr << "SDF Shader linking failed: " << infoLog << std::endl;
        return false;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return true;
}

} // namespace NomadUI

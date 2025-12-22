#include "NUIPaperRenderer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

namespace NomadUI::Paper {

    NUIPaperRenderer::NUIPaperRenderer() {}

    NUIPaperRenderer::~NUIPaperRenderer() {
        Shutdown();
    }

    bool NUIPaperRenderer::Initialize() {
        // Create an empty VAO (required by Core Profile even if we don't bind vertex attributes directly)
        // actually we DO bind instance attributes, so we need a VAO to capture that state.
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_instanceVBO);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);

        // Define Attributes (Instanced, Divisor=1)
        // struct UIPrimitive layout:
        // 0: vec4 bounds
        // 1: vec4 color
        // 2: vec4 clipRect
        // 3: vec4 corners
        // 4: float borderWidth
        // 5: vec4 borderColor
        // 6: int textureLayer
        // 7: int shaderType

        size_t stride = sizeof(UIPrimitive);
        size_t offset = 0;

        // 0: Bounds
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(0, 1);
        offset += sizeof(glm::vec4);

        // 1: Color
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(1, 1);
        offset += sizeof(glm::vec4);

        // 2: ClipRect
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(2, 1);
        offset += sizeof(glm::vec4);

        // 3: Corners
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(3, 1);
        offset += sizeof(glm::vec4);

        // 4: BorderWidth
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(4, 1);
        offset += sizeof(float);

        // 5: BorderColor
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(5, 1);
        offset += sizeof(glm::vec4);

        // 6: TextureLayer (int)
        glEnableVertexAttribArray(6);
        glVertexAttribIPointer(6, 1, GL_INT, stride, (void*)offset);
        glVertexAttribDivisor(6, 1);
        offset += sizeof(int);

        // 7: ShaderType (int)
        glEnableVertexAttribArray(7);
        glVertexAttribIPointer(7, 1, GL_INT, stride, (void*)offset);
        glVertexAttribDivisor(7, 1);
        offset += sizeof(int);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        
        return true;
    }

    void NUIPaperRenderer::Shutdown() {
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        if (m_instanceVBO) glDeleteBuffers(1, &m_instanceVBO);
        if (m_programID) glDeleteProgram(m_programID);
    }

    void NUIPaperRenderer::BeginFrame() {
        m_primitives.clear();
    }

    void NUIPaperRenderer::DrawRect(const glm::vec4& bounds, const glm::vec4& color, const glm::vec4& clipRect, const glm::vec4& corners, float borderWidth, const glm::vec4& borderColor) {
        UIPrimitive p;
        p.bounds = bounds;
        p.color = color;
        p.clipRect = clipRect;
        p.corners = corners;
        p.borderWidth = borderWidth;
        p.borderColor = borderColor;
        p.textureLayer = -1;
        p.shaderType = (int)PrimitiveType::RECT;
        m_primitives.push_back(p);
    }

    void NUIPaperRenderer::DrawCircle(const glm::vec4& bounds, const glm::vec4& color, const glm::vec4& clipRect, float borderWidth, const glm::vec4& borderColor) {
        UIPrimitive p;
        p.bounds = bounds;
        p.color = color;
        p.clipRect = clipRect;
        p.corners = glm::vec4(0.0f); // Not used for circle usually, or could serve as radii overrides
        p.borderWidth = borderWidth;
        p.borderColor = borderColor;
        p.textureLayer = -1;
        p.shaderType = (int)PrimitiveType::CIRCLE;
        m_primitives.push_back(p);
    }

    void NUIPaperRenderer::DrawTextPlaceholder(const glm::vec4& bounds, const glm::vec4& color, const glm::vec4& clipRect) {
         UIPrimitive p;
        p.bounds = bounds;
        p.color = color;
        p.clipRect = clipRect;
        p.corners = glm::vec4(0.0f);
        p.borderWidth = 0.0f;
        p.borderColor = glm::vec4(0.0f);
        p.textureLayer = -1; // Would be >=0 for real text
        p.shaderType = (int)PrimitiveType::TEXT;
        m_primitives.push_back(p);
    }

    void NUIPaperRenderer::Render(const glm::mat4& projection, const glm::mat4& view) {
        if (m_primitives.empty()) return;
        if (m_programID == 0) return;

        glUseProgram(m_programID);
        
        // Upload Uniforms
        glUniformMatrix4fv(m_locProjection, 1, GL_FALSE, &projection[0][0]);
        glUniformMatrix4fv(m_locView, 1, GL_FALSE, &view[0][0]);
        
        // Upload Instances
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
        // Orphan Buffer
        glBufferData(GL_ARRAY_BUFFER, m_primitives.size() * sizeof(UIPrimitive), nullptr, GL_STREAM_DRAW);
        // Upload Data
        glBufferSubData(GL_ARRAY_BUFFER, 0, m_primitives.size() * sizeof(UIPrimitive), m_primitives.data());
        
        glBindVertexArray(m_vao);
        
        // Draw 4 verts per instance
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)m_primitives.size());
        
        glBindVertexArray(0);
        glUseProgram(0);
    }

    bool NUIPaperRenderer::LoadShaders(const std::string& vertPath, const std::string& fragPath) {
        std::string vertCode, fragCode;
        std::ifstream vShaderFile, fShaderFile;
        
        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        
        try {
            vShaderFile.open(vertPath);
            fShaderFile.open(fragPath);
            std::stringstream vShaderStream, fShaderStream;
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            vShaderFile.close();
            fShaderFile.close();
            vertCode = vShaderStream.str();
            fragCode = fShaderStream.str();
        } catch (std::ifstream::failure& e) {
            std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ: " << e.what() << std::endl;
            return false;
        }

        GLuint vID, fID;
        CompileShader(vertCode.c_str(), GL_VERTEX_SHADER, vID);
        CompileShader(fragCode.c_str(), GL_FRAGMENT_SHADER, fID);
        LinkProgram(vID, fID);

        glDeleteShader(vID);
        glDeleteShader(fID);
        
        // locations
        m_locProjection = glGetUniformLocation(m_programID, "uProjection");
        m_locView = glGetUniformLocation(m_programID, "uView");
        
        return true;
    }

    void NUIPaperRenderer::CompileShader(const char* source, GLenum type, GLuint& shaderID) {
        shaderID = glCreateShader(type);
        glShaderSource(shaderID, 1, &source, NULL);
        glCompileShader(shaderID);
        
        int success;
        char infoLog[512];
        glGetShaderiv(shaderID, GL_COMPILE_STATUS, &success);
        if(!success) {
            glGetShaderInfoLog(shaderID, 512, NULL, infoLog);
            std::cerr << "ERROR::SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
        }
    }

    void NUIPaperRenderer::LinkProgram(GLuint vertID, GLuint fragID) {
        m_programID = glCreateProgram();
        glAttachShader(m_programID, vertID);
        glAttachShader(m_programID, fragID);
        glLinkProgram(m_programID);
        
        int success;
        char infoLog[512];
        glGetProgramiv(m_programID, GL_LINK_STATUS, &success);
        if(!success) {
            glGetProgramInfoLog(m_programID, 512, NULL, infoLog);
             std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        }
    }

}

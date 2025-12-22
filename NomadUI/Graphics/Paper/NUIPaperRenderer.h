#pragma once

#include <vector>
#include <string>
#include <memory>
#include "NUIPaperTypes.h"
#include "../../External/glad/include/glad/glad.h"

namespace NomadUI::Paper {

    class NUIPaperRenderer {
    public:
        NUIPaperRenderer();
        ~NUIPaperRenderer();

        bool Initialize();
        void Shutdown();

        // 1. Begin Frame (clears lists)
        void BeginFrame();

        // 2. Queue Primitives
        void DrawRect(const glm::vec4& bounds, const glm::vec4& color, const glm::vec4& clipRect, const glm::vec4& corners, float borderWidth, const glm::vec4& borderColor);
        void DrawCircle(const glm::vec4& bounds, const glm::vec4& color, const glm::vec4& clipRect, float borderWidth, const glm::vec4& borderColor);
        // Text stub for now
        void DrawTextPlaceholder(const glm::vec4& bounds, const glm::vec4& color, const glm::vec4& clipRect);

        // 3. End Frame (renders) - In a real scenario this might be just "Render" called by the owner
        void Render(const glm::mat4& projection, const glm::mat4& view);

        // Utils
        bool LoadShaders(const std::string& vertPath, const std::string& fragPath);

    private:
        GLuint m_programID = 0;
        GLuint m_instanceVBO = 0;
        GLuint m_vao = 0; // Empty VAO for array drawing

        std::vector<UIPrimitive> m_primitives;
        
        // Locations
        GLint m_locProjection = -1;
        GLint m_locView = -1;
        GLint m_locTextureArray = -1;

        void CompileShader(const char* source, GLenum type, GLuint& shaderID);
        void LinkProgram(GLuint vertID, GLuint fragID);
    };

}

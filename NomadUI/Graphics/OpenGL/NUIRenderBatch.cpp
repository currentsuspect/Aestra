#include "NUIRenderBatch.h"
#include <algorithm>
#include <iostream>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <Windows.h>
#endif

// GLAD must be included after Windows headers
#include "../../External/glad/include/glad/glad.h"

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4005)
#pragma warning(pop)
#endif

namespace NomadUI {

    // ========================================================================================
    // NUIRenderBatch
    // ========================================================================================

    NUIRenderBatch::NUIRenderBatch(BatchType type, size_t maxVertices)
        : m_type(type)
        , m_vertexCount(0)
        , m_maxVertices(maxVertices)
        , m_vao(0)
        , m_vbo(0)
        , m_ebo(0)
        , m_initialized(false)
    {
        m_vertices.reserve(maxVertices);
    }

    NUIRenderBatch::~NUIRenderBatch() {
        if (m_initialized) {
            glDeleteVertexArrays(1, &m_vao);
            glDeleteBuffers(1, &m_vbo);
            glDeleteBuffers(1, &m_ebo);
        }
    }

    void NUIRenderBatch::ensureInitialized() {
        if (m_initialized) return;

        // Create VAO, VBO, EBO
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);

        glBindVertexArray(m_vao);

        // Setup VBO
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, m_maxVertices * sizeof(BatchVertex), nullptr, GL_DYNAMIC_DRAW);

        // Position attribute (location = 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)offsetof(BatchVertex, x));

        // TexCoord attribute (location = 1)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)offsetof(BatchVertex, u));

        // Color attribute (location = 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)offsetof(BatchVertex, r));

        // CornerRadius attribute (location = 3)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)offsetof(BatchVertex, cornerRadius));

        // TextureId attribute (location = 4)
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)offsetof(BatchVertex, textureId));

        // Setup EBO (indices for quads)
        std::vector<uint32_t> indices;
        indices.reserve((m_maxVertices / 4) * 6); // 6 indices per quad
        for (size_t i = 0; i < m_maxVertices / 4; ++i) {
            uint32_t baseIdx = static_cast<uint32_t>(i * 4);
            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 3);
            indices.push_back(baseIdx + 0);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

        glBindVertexArray(0);

        m_initialized = true;
    }

    void NUIRenderBatch::addQuad(const NUIRect& rect, const NUIColor& color) {
        if (!canFit(4)) {
            flush();
        }

        // Top-left
        BatchVertex v0;
        v0.x = rect.x; v0.y = rect.y;
        v0.u = 0; v0.v = 0;
        v0.r = color.r; v0.g = color.g; v0.b = color.b; v0.a = color.a;
        addVertex(v0);

        // Top-right
        BatchVertex v1;
        v1.x = rect.x + rect.width; v1.y = rect.y;
        v1.u = 1; v1.v = 0;
        v1.r = color.r; v1.g = color.g; v1.b = color.b; v1.a = color.a;
        addVertex(v1);

        // Bottom-right
        BatchVertex v2;
        v2.x = rect.x + rect.width; v2.y = rect.y + rect.height;
        v2.u = 1; v2.v = 1;
        v2.r = color.r; v2.g = color.g; v2.b = color.b; v2.a = color.a;
        addVertex(v2);

        // Bottom-left
        BatchVertex v3;
        v3.x = rect.x; v3.y = rect.y + rect.height;
        v3.u = 0; v3.v = 1;
        v3.r = color.r; v3.g = color.g; v3.b = color.b; v3.a = color.a;
        addVertex(v3);
    }

    void NUIRenderBatch::addRoundedRect(const NUIRect& rect, float cornerRadius, const NUIColor& color) {
        if (!canFit(4)) {
            flush();
        }

        // Similar to addQuad but with cornerRadius
        BatchVertex v0;
        v0.x = rect.x; v0.y = rect.y;
        v0.u = 0; v0.v = 0;
        v0.r = color.r; v0.g = color.g; v0.b = color.b; v0.a = color.a;
        v0.cornerRadius = cornerRadius;
        addVertex(v0);

        BatchVertex v1;
        v1.x = rect.x + rect.width; v1.y = rect.y;
        v1.u = 1; v1.v = 0;
        v1.r = color.r; v1.g = color.g; v1.b = color.b; v1.a = color.a;
        v1.cornerRadius = cornerRadius;
        addVertex(v1);

        BatchVertex v2;
        v2.x = rect.x + rect.width; v2.y = rect.y + rect.height;
        v2.u = 1; v2.v = 1;
        v2.r = color.r; v2.g = color.g; v2.b = color.b; v2.a = color.a;
        v2.cornerRadius = cornerRadius;
        addVertex(v2);

        BatchVertex v3;
        v3.x = rect.x; v3.y = rect.y + rect.height;
        v3.u = 0; v3.v = 1;
        v3.r = color.r; v3.g = color.g; v3.b = color.b; v3.a = color.a;
        v3.cornerRadius = cornerRadius;
        addVertex(v3);
    }

    void NUIRenderBatch::addTexturedQuad(const NUIRect& rect, const NUIRect& texCoords, uint32_t textureId, const NUIColor& tint) {
        if (!canFit(4)) {
            flush();
        }

        // Top-left
        BatchVertex v0;
        v0.x = rect.x; v0.y = rect.y;
        v0.u = texCoords.x; v0.v = texCoords.y;
        v0.r = tint.r; v0.g = tint.g; v0.b = tint.b; v0.a = tint.a;
        v0.textureId = static_cast<float>(textureId);
        addVertex(v0);

        // Top-right
        BatchVertex v1;
        v1.x = rect.x + rect.width; v1.y = rect.y;
        v1.u = texCoords.x + texCoords.width; v1.v = texCoords.y;
        v1.r = tint.r; v1.g = tint.g; v1.b = tint.b; v1.a = tint.a;
        v1.textureId = static_cast<float>(textureId);
        addVertex(v1);

        // Bottom-right
        BatchVertex v2;
        v2.x = rect.x + rect.width; v2.y = rect.y + rect.height;
        v2.u = texCoords.x + texCoords.width; v2.v = texCoords.y + texCoords.height;
        v2.r = tint.r; v2.g = tint.g; v2.b = tint.b; v2.a = tint.a;
        v2.textureId = static_cast<float>(textureId);
        addVertex(v2);

        // Bottom-left
        BatchVertex v3;
        v3.x = rect.x; v3.y = rect.y + rect.height;
        v3.u = texCoords.x; v3.v = texCoords.y + texCoords.height;
        v3.r = tint.r; v3.g = tint.g; v3.b = tint.b; v3.a = tint.a;
        v3.textureId = static_cast<float>(textureId);
        addVertex(v3);
    }

    void NUIRenderBatch::addVertex(const BatchVertex& vertex) {
        if (m_vertexCount < m_maxVertices) {
            if (m_vertexCount >= m_vertices.size()) {
                m_vertices.push_back(vertex);
            } else {
                m_vertices[m_vertexCount] = vertex;
            }
            m_vertexCount++;
        }
    }

    bool NUIRenderBatch::canFit(size_t vertexCount) const {
        return (m_vertexCount + vertexCount) <= m_maxVertices;
    }

    void NUIRenderBatch::flush() {
        if (m_vertexCount == 0) return;

        // TODO: Implement OpenGL rendering
        // For now, this is a stub - we'll complete this after fixing the GL context issues
        
        // Clear for next batch
        m_vertexCount = 0;
    }

    void NUIRenderBatch::clear() {
        m_vertexCount = 0;
    }

    // ========================================================================================
    // NUIBatchManager
    // ========================================================================================

    NUIBatchManager::NUIBatchManager()
        : m_quadBatch(BatchType::Quad)
        , m_roundedRectBatch(BatchType::RoundedRect)
        , m_textBatch(BatchType::Text)
        , m_texturedQuadBatch(BatchType::TexturedQuad)
        , m_enabled(true)
        , m_totalQuadsRendered(0)
        , m_batchFlushCount(0)
    {
    }

    NUIBatchManager::~NUIBatchManager() {
        flushAll();
    }

    void NUIBatchManager::addQuad(const NUIRect& rect, const NUIColor& color) {
        if (!m_enabled) return; // Fall back to immediate mode if disabled

        m_quadBatch.addQuad(rect, color);
        m_totalQuadsRendered++;
    }

    void NUIBatchManager::addRoundedRect(const NUIRect& rect, float cornerRadius, const NUIColor& color) {
        if (!m_enabled) return;

        m_roundedRectBatch.addRoundedRect(rect, cornerRadius, color);
        m_totalQuadsRendered++;
    }

    void NUIBatchManager::addTexturedQuad(const NUIRect& rect, const NUIRect& texCoords, uint32_t textureId, const NUIColor& tint) {
        if (!m_enabled) return;

        m_texturedQuadBatch.addTexturedQuad(rect, texCoords, textureId, tint);
        m_totalQuadsRendered++;
    }

    void NUIBatchManager::flushAll() {
        m_quadBatch.flush();
        m_roundedRectBatch.flush();
        m_textBatch.flush();
        m_texturedQuadBatch.flush();

        m_batchFlushCount++;
    }

    void NUIBatchManager::clearAll() {
        m_quadBatch.clear();
        m_roundedRectBatch.clear();
        m_textBatch.clear();
        m_texturedQuadBatch.clear();

        m_totalQuadsRendered = 0;
        m_batchFlushCount = 0;
    }

    NUIRenderBatch* NUIBatchManager::getCurrentBatch(BatchType type) {
        switch (type) {
            case BatchType::Quad: return &m_quadBatch;
            case BatchType::RoundedRect: return &m_roundedRectBatch;
            case BatchType::Text: return &m_textBatch;
            case BatchType::TexturedQuad: return &m_texturedQuadBatch;
            default: return nullptr;
        }
    }

    void NUIBatchManager::flushBatch(BatchType type) {
        NUIRenderBatch* batch = getCurrentBatch(type);
        if (batch) {
            batch->flush();
        }
    }

    size_t NUIBatchManager::getTotalQuads() const {
        return m_totalQuadsRendered;
    }

    size_t NUIBatchManager::getBatchCount() const {
        size_t count = 0;
        if (!m_quadBatch.isEmpty()) count++;
        if (!m_roundedRectBatch.isEmpty()) count++;
        if (!m_textBatch.isEmpty()) count++;
        if (!m_texturedQuadBatch.isEmpty()) count++;
        return count;
    }

} // namespace NomadUI

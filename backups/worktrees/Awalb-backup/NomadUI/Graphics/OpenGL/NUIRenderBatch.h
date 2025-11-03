// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#ifndef NUIRENDERBATCH_H
#define NUIRENDERBATCH_H

#include "../../Core/NUITypes.h"
#include <vector>

namespace NomadUI {

    // Vertex structure for batched rendering
    struct BatchVertex {
        float x, y;           // Position
        float u, v;           // Texture coordinates
        float r, g, b, a;     // Color
        float cornerRadius;   // For rounded rects (0 = sharp)
        float textureId;      // Texture slot index

        BatchVertex() : x(0), y(0), u(0), v(0), r(1), g(1), b(1), a(1), cornerRadius(0), textureId(-1) {}
    };

    // Types of batched primitives
    enum class BatchType {
        Quad,           // Simple rectangles
        RoundedRect,    // Rounded rectangles
        Text,           // Text rendering
        TexturedQuad    // Textured quads
    };

    // A single batch of similar draw calls
    class NUIRenderBatch {
    public:
        NUIRenderBatch(BatchType type, size_t maxVertices = 10000);
        ~NUIRenderBatch();

        // Add primitives to the batch
        void addQuad(const NUIRect& rect, const NUIColor& color);
        void addRoundedRect(const NUIRect& rect, float cornerRadius, const NUIColor& color);
        void addTexturedQuad(const NUIRect& rect, const NUIRect& texCoords, uint32_t textureId, const NUIColor& tint);

        // Check if batch can accept more vertices
        bool canFit(size_t vertexCount) const;

        // Flush the batch to GPU
        void flush();

        // Clear the batch
        void clear();

        // Get batch stats
        size_t getVertexCount() const { return m_vertexCount; }
        size_t getQuadCount() const { return m_vertexCount / 4; }
        BatchType getType() const { return m_type; }
        bool isEmpty() const { return m_vertexCount == 0; }

    private:
        void ensureInitialized();
        void addVertex(const BatchVertex& vertex);

        BatchType m_type;
        std::vector<BatchVertex> m_vertices;
        size_t m_vertexCount;
        size_t m_maxVertices;

        // OpenGL resources
        uint32_t m_vao;
        uint32_t m_vbo;
        uint32_t m_ebo;
        bool m_initialized;
    };

    // Batch manager - manages multiple batches
    class NUIBatchManager {
    public:
        NUIBatchManager();
        ~NUIBatchManager();

        // Add primitives (will auto-batch and flush when needed)
        void addQuad(const NUIRect& rect, const NUIColor& color);
        void addRoundedRect(const NUIRect& rect, float cornerRadius, const NUIColor& color);
        void addTexturedQuad(const NUIRect& rect, const NUIRect& texCoords, uint32_t textureId, const NUIColor& tint);

        // Flush all batches
        void flushAll();

        // Clear all batches
        void clearAll();

        // Enable/disable batching
        void setEnabled(bool enabled) { m_enabled = enabled; }
        bool isEnabled() const { return m_enabled; }

        // Get stats
        size_t getTotalQuads() const;
        size_t getBatchCount() const;

    private:
        NUIRenderBatch* getCurrentBatch(BatchType type);
        void flushBatch(BatchType type);

        NUIRenderBatch m_quadBatch;
        NUIRenderBatch m_roundedRectBatch;
        NUIRenderBatch m_textBatch;
        NUIRenderBatch m_texturedQuadBatch;

        bool m_enabled;
        size_t m_totalQuadsRendered;
        size_t m_batchFlushCount;
    };

} // namespace NomadUI

#endif // NUIRENDERBATCH_H

// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Index buffers for the renderer's batch (see the quadOnlyBatch_ note in
// NUIRendererGL.h). A batch of canonical quads draws from one static index
// buffer instead of writing and uploading six indices per quad every frame;
// geometry that shares vertices switches its batch back to explicit indices.
#include "NUIRendererGL.h"

#include <algorithm>
#include <cassert>
#include <vector>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <Windows.h>
#endif

// GLAD must be included after Windows headers to avoid macro conflicts
#include "../../External/glad/include/glad/glad.h"

namespace AestraUI {

void NUIRendererGL::pushQuadIndices(uint32_t base) {
    if (quadOnlyBatch_) {
        return; // quadEbo_ already holds this quad's indices
    }
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void NUIRendererGL::useExplicitIndices() {
    if (!quadOnlyBatch_) {
        return;
    }
    quadOnlyBatch_ = false;
    // The pending vertices are all canonical quads: give them the indices
    // quadEbo_ would have, so the batch draws the same triangles in the same order.
    const uint32_t pendingVertices = static_cast<uint32_t>(vertices_.size());
    assert(pendingVertices % 4 == 0 && "quad-only batch holds a partial quad");
    indices_.reserve(indices_.size() + (pendingVertices / 4) * 6);
    for (uint32_t base = 0; base + 3 < pendingVertices; base += 4) {
        pushQuadIndices(base);
    }
}

uint32_t NUIRendererGL::bindBatchIndices() {
    if (!quadOnlyBatch_) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint32_t), indices_.data(), GL_DYNAMIC_DRAW);
        return static_cast<uint32_t>(indices_.size());
    }

    const uint32_t quads = static_cast<uint32_t>(vertices_.size() / 4);
    assert(vertices_.size() % 4 == 0 && "quad-only batch holds a partial quad");
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEbo_);
    if (quads > quadEboQuads_) {
        // Grown geometrically, so a frame that draws more quads than any before
        // it pays for one rebuild, not one per frame.
        const uint32_t capacity = std::max({quads, quadEboQuads_ * 2u, 4096u});
        std::vector<uint32_t> pattern(static_cast<size_t>(capacity) * 6u);
        for (uint32_t q = 0; q < capacity; ++q) {
            const uint32_t base = q * 4u;
            uint32_t* out = &pattern[static_cast<size_t>(q) * 6u];
            out[0] = base + 0;
            out[1] = base + 1;
            out[2] = base + 2;
            out[3] = base + 0;
            out[4] = base + 2;
            out[5] = base + 3;
        }
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, pattern.size() * sizeof(uint32_t), pattern.data(), GL_STATIC_DRAW);
        quadEboQuads_ = capacity;
    }
    return quads * 6u;
}

void NUIRendererGL::clearBatch() {
    vertices_.clear();
    indices_.clear();
    quadOnlyBatch_ = true;
}

} // namespace AestraUI

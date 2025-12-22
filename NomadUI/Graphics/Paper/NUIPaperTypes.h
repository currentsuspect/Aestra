#pragma once

#include <glm/glm.hpp>

namespace NomadUI::Paper {

    // Primitive Types for the Shader
    enum class PrimitiveType : int {
        RECT = 0,
        TEXT = 1,
        CIRCLE = 2
    };

    // The "Stateless" UI Primitive
    // This MUST match the layout in the Vertex Shader attributes
    struct UIPrimitive {
        glm::vec4 bounds;       // x, y, width, height
        glm::vec4 color;        // r, g, b, a
        glm::vec4 clipRect;     // World-space clipping bounds (minX, minY, maxX, maxY)
        glm::vec4 corners;      // TopLeft, TopRight, BotRight, BotLeft radii
        float borderWidth;
        glm::vec4 borderColor;  // r, g, b, a (Note: Using full vec4 for border color might be needed, though struct says vec4 in prompt, let's stick to prompt or reasonable expansion)
                                // Prompt said: vec4 borderColor;
                                
        // Packing int attributes into floats or using glVertexAttribIPointer
        // For simplicity in this struct, we keep them as native types, but we'll need to be careful with alignment/upload.
        // Ideally, in the shader these are passed as separate integer attributes or packed.
        // Let's stick to the prompt's layout but use GL-friendly types if needed.
        // Prompt Layout:
        // vec4 bounds;
        // vec4 color;
        // vec4 clipRect;
        // vec4 corners;
        // float borderWidth;
        // vec4 borderColor;
        // int textureLayer;
        // int shaderType;

        // Implementation detail: Alignment is important for UBOs/SSBOs, but for Instanced Attributes
        // we set the stride manually on the generic vertex attributes.
        
        int textureLayer;  // Index in the Texture Array (-1 for solid color)
        int shaderType;    // 0=Rect, 1=Text, 2=Circle/Knob
        
        // Padding might be needed if we were using std140/std430, but for Vertex Attribs we define the pointers.
        float padding[2]; 
    };

}

#version 330 core

// Instanced Attributes (Divisor = 1)
// Corresponds to UIPrimitive struct
layout(location = 0) in vec4 iBounds;       // x, y, width, height
layout(location = 1) in vec4 iColor;        // r, g, b, a
layout(location = 2) in vec4 iClipRect;     // minX, minY, maxX, maxY
layout(location = 3) in vec4 iCorners;      // TopLeft, TopRight, BotRight, BotLeft
layout(location = 4) in float iBorderWidth;
layout(location = 5) in vec4 iBorderColor;
layout(location = 6) in int iTextureLayer;
layout(location = 7) in int iShaderType;

// Outputs to Fragment Shader
flat out vec4  vBounds;
flat out vec4  vColor;
flat out vec4  vClipRect;
flat out vec4  vCorners;
flat out float vBorderWidth;
flat out vec4  vBorderColor;
flat out int   vTextureLayer;
flat out int   vShaderType;

out vec2 vUV; // Standard 0-1 UV for the quad
out vec2 vPixelPos; // World space pixel position

// Uniforms
uniform mat4 uProjection;
uniform mat4 uView; // Likely identity for 2D UI, but good to have

void main() {
    // Generate Quad Vertices on the fly (Indices: 0, 1, 2, 3)
    // Triangle Strip: 0=(0,0), 1=(1,0), 2=(0,1), 3=(1,1)
    
    // gl_VertexID is used if we draw arrays without a bound VBO for vertices, 
    // but the prompt suggests "glBindVertexArray(quadVAO); // A single 1x1 static quad".
    // Assuming a standard 0..1 quad VBO for simplicity or generating it.
    // Let's assume the user provides a standard quad [0,0] to [1,1].
    // Actually, to be safer and "stateless", we can generate the quad from gl_VertexID 
    // if we draw 4 vertices per instance.
    
    // Let's assume standard VBO input on location 8 if we wanted, 
    // but for now let's generate it to be fully self-contained as "Paper" suggests.
    // "glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, numWidgets);"
    
    vec2 pos;
    if (gl_VertexID == 0) pos = vec2(0.0, 0.0);
    else if (gl_VertexID == 1) pos = vec2(1.0, 0.0);
    else if (gl_VertexID == 2) pos = vec2(0.0, 1.0);
    else pos = vec2(1.0, 1.0);
    
    vUV = pos;
    
    // Scale and Translate based on Instance Data
    // iBounds = (x, y, width, height)
    vec2 worldPos = iBounds.xy + (pos * iBounds.zw);
    vPixelPos = worldPos;
    
    // Pass user data
    vBounds = iBounds;
    vColor = iColor;
    vClipRect = iClipRect;
    vCorners = iCorners;
    vBorderWidth = iBorderWidth;
    vBorderColor = iBorderColor;
    vTextureLayer = iTextureLayer;
    vShaderType = iShaderType;

    gl_Position = uProjection * uView * vec4(worldPos, 0.0, 1.0);
}

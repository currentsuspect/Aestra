#version 330 core

flat in vec4  vBounds;       // x, y, width, height
flat in vec4  vColor;
flat in vec4  vClipRect;     // minX, minY, maxX, maxY
flat in vec4  vCorners;      // tl, tr, br, bl
flat in float vBorderWidth;
flat in vec4  vBorderColor;
flat in int   vTextureLayer;
flat in int   vShaderType;

in vec2 vUV;
in vec2 vPixelPos;

out vec4 FragColor;

uniform sampler2DArray uTextureArray;

// SDF Functions
float sdRoundedBox(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    vec2 q = abs(p) - b + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}

float sdCircle(vec2 p, float r) {
    return length(p) - r;
}

void main() {
    // 1. Clipping
    if (vPixelPos.x < vClipRect.x || vPixelPos.x > vClipRect.z ||
        vPixelPos.y < vClipRect.y || vPixelPos.y > vClipRect.w) {
        discard;
    }

    vec4 finalColor = vColor;
    float alpha = 1.0;
    float dist = 0.0;

    // Center coordinates for SDF
    // vBounds.xy is top-left, vBounds.zw is size
    // Center is vBounds.xy + vBounds.zw * 0.5
    vec2 halfSize = vBounds.zw * 0.5;
    vec2 center = vBounds.xy + halfSize;
    vec2 p = vPixelPos - center;
    
    // Flip Y for standard SDF logic if needed, but here we work in pixels.
    // Usually SDF assumes Y up or down depending on coord sys. 
    // Assuming screen space (Y down mostly in UI, but OpenGL is Y up).
    // The match is symmetric for Box/Circle so Y direction matters less unless corners differ.
    // vCorners order: TopLeft, TopRight, BotRight, BotLeft.
    // If Y is down (0 at top), Top is negative relative to center?
    // Let's assume standard GL Y-up for now. If UI is Y-down, we might need to flip p.y or corner mapping.
    // Nomad usually uses Y-down for UI. Let's flip p.y to match cartesian standard for calculation if needed.
    // Actually, sdRoundedBox quadrant selection:
    // p.x > 0 is Right, p.y > 0 is Bottom (in Y-down) or Top (in Y-up).
    // Let's assume Y-down (Top is y=0, Bot is y=Height).
    // So Top is p.y < 0. Bottom is p.y > 0.
    // vCorners: x=TL, y=TR, z=BR, w=BL.
    // code: r.xy = (p.x > 0.0) ? r.xy : r.zw; -> Right side gets r.xy (TL/TR?? No).
    // We need to map carefully.
    
    // Re-map corners to: vec4(TR, BR, TL, BL) for the logic:
    // select x vs z based on p.x > 0 (Right vs Left)
    // select result.x vs result.y based on p.y > 0 (Bottom vs Top)
    
    // Let's customize the selection explicitly:
    float radius = 0.0;
    if (p.x > 0.0) { // Right
        if (p.y > 0.0) radius = vCorners.z; // BR (Bottom-Right)
        else radius = vCorners.y;           // TR (Top-Right)
    } else { // Left
        if (p.y > 0.0) radius = vCorners.w; // BL (Bottom-Left)
        else radius = vCorners.x;           // TL (Top-Left)
    }
    
    // TYPE 0: RECT
    if (vShaderType == 0) {
        dist = sdRoundedBox(p, halfSize, vec4(radius));
        
        // Main Shape AA
        alpha = 1.0 - smoothstep(0.0, 1.0, dist);
        
        // Border
        if (vBorderWidth > 0.0) {
            float borderDist = dist + vBorderWidth;
            float borderAlpha = 1.0 - smoothstep(0.0, 1.0, abs(dist + vBorderWidth * 0.5) - vBorderWidth * 0.5); 
            // Better border: stroke is inside or centered? 
            // Usually Inside: dist goes from 0 to -inf inside.
            // Outline:
            float outline = 1.0 - smoothstep(vBorderWidth - 1.0, vBorderWidth, abs(dist)); 
            // Let's act like internal border.
            // Inner color mixing?
            
            // Simple approach: mix borderColor over fillColor based on border dist
            float bFactor = smoothstep(-vBorderWidth - 0.5, -vBorderWidth + 0.5, dist);
            finalColor = mix(vBorderColor, vColor, bFactor);
        }
        
        finalColor.a *= alpha;
    }
    // TYPE 1: TEXT
    else if (vShaderType == 1) {
        // Sample Texture Array (SDF Font)
        // vTextureLayer holds the page index
        // UVs need to be passed strictly? Or assumed from quad?
        // For text, we usually render glyph by glyph, so vUV is correct.
        // But the quad is the glyph quad.
        
        if (vTextureLayer >= 0) {
             float sdf = texture(uTextureArray, vec3(vUV, float(vTextureLayer))).r;
             // Basic SDF Text AA
             float smoothing = 0.25 / (halfSize.y * 0.1); // approximation?
             // Standard SDF smooth
             alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, sdf);
             finalColor = vColor * alpha;
        } else {
             finalColor = vColor; // Solid block text placeholder
        }
    }
    // TYPE 2: CIRCLE / KNOB
    else if (vShaderType == 2) {
        dist = sdCircle(p, min(halfSize.x, halfSize.y)); // Radius is min dim
        alpha = 1.0 - smoothstep(0.0, 1.0, dist);
        
         // Border
        if (vBorderWidth > 0.0) {
             float bFactor = smoothstep(-vBorderWidth - 0.5, -vBorderWidth + 0.5, dist);
             finalColor = mix(vBorderColor, vColor, bFactor);
        }
        
        finalColor.a *= alpha;
    }

    if (finalColor.a <= 0.0) discard;
    FragColor = finalColor;
}

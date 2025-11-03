#version 330 core

in vec2 vTexCoord;
in vec4 vColor;

out vec4 FragColor;

uniform int uPrimitiveType; // 0=rect, 1=rounded_rect, 2=circle, 3=gradient, 4=radial_gradient, 5=texture
uniform float uRadius;
uniform vec2 uSize;
uniform vec4 uGradientColor; // For gradients
uniform sampler2D uTexture; // For texture rendering

// Signed distance function for rounded rectangle
float sdRoundedRect(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

// Signed distance function for circle
float sdCircle(vec2 p, float radius) {
    return length(p) - radius;
}

void main() {
    vec4 color = vColor;
    
    if (uPrimitiveType == 1) {
        // Rounded rectangle
        vec2 center = uSize * 0.5;
        vec2 pos = vTexCoord * uSize;
        float dist = sdRoundedRect(pos - center, center, uRadius);
        
        // Anti-aliasing
        float alpha = 1.0 - smoothstep(-1.0, 1.0, dist);
        color.a *= alpha;
        
        if (color.a < 0.01) discard;
    }
    else if (uPrimitiveType == 2) {
        // Circle
        vec2 center = vec2(0.5, 0.5);
        float dist = sdCircle(vTexCoord - center, 0.5);
        
        // Anti-aliasing
        float alpha = 1.0 - smoothstep(-0.01, 0.01, dist);
        color.a *= alpha;
        
        if (color.a < 0.01) discard;
    }
    else if (uPrimitiveType == 3) {
        // Linear gradient (vertical by default)
        float t = vTexCoord.y;
        color = mix(vColor, uGradientColor, t);
    }
    else if (uPrimitiveType == 4) {
        // Radial gradient
        vec2 center = vec2(0.5, 0.5);
        float dist = length(vTexCoord - center) * 2.0;
        color = mix(vColor, uGradientColor, clamp(dist, 0.0, 1.0));
    }
    else if (uPrimitiveType == 5) {
        // Texture rendering
        vec4 texColor = texture(uTexture, vTexCoord);
        color = vec4(texColor.rgb, texColor.a * vColor.a);
    }
    
    FragColor = color;
}

#include "render/shaders.hpp"

namespace tp::gfx::shaders {

namespace {

const char* const kHeader = R"(#version 330 core
layout(std140) uniform FrameData {
    mat4 uViewProj;
    mat4 uLightViewProj;
    vec4 uCameraPos;     // xyz: eye (render origin relative), w: time in seconds
    vec4 uSunDir;        // xyz: direction towards the sun, w: sun strength
    vec4 uSunColor;
    vec4 uZenithColor;
    vec4 uHorizonColor;
    vec4 uAmbientSky;
    vec4 uAmbientGround;
    vec4 uFogParams;     // x: fog start, y: fog end, zw: fog centre (xz)
    vec4 uShadowParams;  // x: texel size, y: depth bias, z: enabled, w: normal offset
};
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toSrgb(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }
)";

const char* const kLighting = R"(
uniform sampler2DShadow uShadowMap;
float shadowFactor(vec3 worldPos, vec3 n) {
    if (uShadowParams.z < 0.5) return 1.0;
    vec4 lp = uLightViewProj * vec4(worldPos + n * uShadowParams.w, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;
    float t = uShadowParams.x;
    float s = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            s += texture(uShadowMap, vec3(p.xy + vec2(x, y) * t, p.z - uShadowParams.y));
    return s / 9.0;
}
vec3 litColor(vec3 albedo, vec3 n, vec3 worldPos) {
    float ndl = max(dot(n, uSunDir.xyz), 0.0);
    float sh = ndl > 0.0 ? shadowFactor(worldPos, n) : 0.0;
    vec3 ambient = mix(uAmbientGround.rgb, uAmbientSky.rgb, n.y * 0.5 + 0.5);
    return albedo * (ambient + uSunColor.rgb * (ndl * sh * uSunDir.w));
}
vec3 applyFog(vec3 c, vec3 worldPos) {
    float d = length(worldPos.xz - uFogParams.zw);
    float f = clamp((d - uFogParams.x) / max(uFogParams.y - uFogParams.x, 1.0), 0.0, 1.0);
    return mix(c, uHorizonColor.rgb, f * f * (3.0 - 2.0 * f));
}
)";

std::string join(std::initializer_list<const char*> parts) {
    std::string s;
    for (const char* p : parts) s += p;
    return s;
}

} // namespace

std::string terrainVS() {
    return join({kHeader, R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aNormal;
layout(location = 2) in vec4 aColor;
uniform vec3 uOffset;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
void main() {
    vec3 wp = aPos + uOffset;
    vWorldPos = wp;
    vNormal = aNormal.xyz;
    vColor = toLinear(aColor.rgb);
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)"});
}

std::string terrainFS() {
    return join({kHeader, kLighting, R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNormal);
    vec3 c = litColor(vColor, n, vWorldPos);
    fragColor = vec4(toSrgb(applyFog(c, vWorldPos)), 1.0);
}
)"});
}

std::string modelVS() {
    return join({kHeader, R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 iRow0;
layout(location = 4) in vec4 iRow1;
layout(location = 5) in vec4 iRow2;
layout(location = 6) in vec4 iTint;
uniform vec3 uOffset;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 wp = vec3(dot(iRow0, p), dot(iRow1, p), dot(iRow2, p)) + uOffset;
    vec3 n = vec3(dot(iRow0.xyz, aNormal.xyz), dot(iRow1.xyz, aNormal.xyz), dot(iRow2.xyz, aNormal.xyz));
    vNormal = normalize(n);
    // Vertices with alpha 0 are tintable: they take the instance colour scaled by their red channel.
    vec3 c = aColor.a < 0.5 ? iTint.rgb * (aColor.r * 2.0) : aColor.rgb;
    vColor = toLinear(c);
    vWorldPos = wp;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)"});
}

std::string modelFS() { return terrainFS(); }

std::string waterVS() {
    return join({kHeader, R"(
layout(location = 0) in vec3 aPos;
uniform vec3 uOffset;
uniform vec2 uWavePhase;
out vec3 vWorldPos;
void main() {
    vec3 wp = aPos + uOffset;
    vec2 q = aPos.xz + uWavePhase;
    float t = uCameraPos.w;
    wp.y += sin(q.x * 1.7 + t * 1.1) * 0.03 + sin(q.y * 1.3 - t * 0.9) * 0.03 + sin((q.x + q.y) * 0.9 + t * 1.6) * 0.02;
    vWorldPos = wp;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)"});
}

std::string waterFS() {
    return join({kHeader, kLighting, R"(
in vec3 vWorldPos;
out vec4 fragColor;
void main() {
    vec3 n = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
    if (n.y < 0.0) n = -n;
    vec3 base = toLinear(vec3(0.16, 0.46, 0.66));
    vec3 c = litColor(base, n, vWorldPos);
    vec3 v = normalize(uCameraPos.xyz - vWorldPos);
    vec3 h = normalize(v + uSunDir.xyz);
    float spec = pow(max(dot(n, h), 0.0), 64.0) * uSunDir.w;
    c += uSunColor.rgb * spec * 0.5;
    float fres = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    c = mix(c, uZenithColor.rgb * 0.8, fres * 0.35);
    fragColor = vec4(toSrgb(applyFog(c, vWorldPos)), 0.8);
}
)"});
}

std::string skyVS() {
    return join({kHeader, R"(
out vec2 vNdc;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
)"});
}

std::string skyFS() {
    return join({kHeader, R"(
in vec2 vNdc;
uniform mat4 uInvViewProj;
out vec4 fragColor;
void main() {
    vec4 a = uInvViewProj * vec4(vNdc, -1.0, 1.0);
    vec4 b = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
    float up = max(dir.y, 0.0);
    vec3 c = mix(uHorizonColor.rgb, uZenithColor.rgb, pow(up, 0.55));
    if (dir.y < 0.0) c = uHorizonColor.rgb;
    float sd = dot(dir, uSunDir.xyz);
    c += uSunColor.rgb * (smoothstep(0.9988, 0.9995, sd) * 1.5 + pow(max(sd, 0.0), 24.0) * 0.2) * uSunDir.w;
    fragColor = vec4(toSrgb(c), 1.0);
}
)"});
}

std::string shadowStaticVS() {
    return join({kHeader, R"(
layout(location = 0) in vec3 aPos;
uniform vec3 uOffset;
void main() { gl_Position = uLightViewProj * vec4(aPos + uOffset, 1.0); }
)"});
}

std::string shadowModelVS() {
    return join({kHeader, R"(
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 iRow0;
layout(location = 4) in vec4 iRow1;
layout(location = 5) in vec4 iRow2;
uniform vec3 uOffset;
void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 wp = vec3(dot(iRow0, p), dot(iRow1, p), dot(iRow2, p)) + uOffset;
    gl_Position = uLightViewProj * vec4(wp, 1.0);
}
)"});
}

std::string shadowFS() {
    return join({kHeader, R"(
void main() {}
)"});
}

std::string overlayVS() {
    return join({kHeader, R"(
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec4 aColor;
uniform vec3 uOffset;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos + uOffset, 1.0);
}
)"});
}

std::string overlayFS() {
    return join({kHeader, R"(
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)"});
}

} // namespace tp::gfx::shaders

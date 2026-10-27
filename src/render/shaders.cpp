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
    vec4 uWeather;       // x: cloud cover (0-1), y: mist, zw: cloud drift (world units)
    vec4 uOrigin;        // xz: render origin in the world, y: height of the cloud layer, w: mist level
    vec4 uVisualOptions; // x: scene brightness, y: cloud shadows, z: tree sway, w: water animation
    // Lamps lighting the scene at night: headlights, station and street lamps.
    vec4 uLightPos[24];   // xyz: position (render origin relative), w: reach
    vec4 uLightColor[24]; // rgb: colour and strength (already faded in by the night)
    vec4 uLightDir[24];   // xyz: spot direction, w: cosine of the cone's edge (below -1: all round)
    vec4 uLightCount;     // x: number of lights
};
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
// Self-lit vertices, marked by their colour's alpha: 254 warm windows (at night), 252 white
// lamps and 251 red lamps (always a little, brightly at night).
float emissionKind(float alpha) {
    float a = alpha * 255.0;
    return abs(a - 254.0) < 0.25 ? 1.0 : abs(a - 252.0) < 0.25 ? 2.0 : abs(a - 251.0) < 0.25 ? 3.0 : 0.0;
}
vec3 applyEmission(vec3 c, float kind) {
    if (kind < 0.5) return c;
    if (kind < 1.5) return mix(c, vec3(1.0, 0.62, 0.22), uSunColor.w);
    vec3 lamp = kind < 2.5 ? vec3(1.6, 1.5, 1.3) : vec3(1.6, 0.12, 0.08);
    return mix(c, lamp, max(uSunColor.w, 0.25));
}
vec3 toSrgb(vec3 c) { return pow(max(c * uVisualOptions.x, vec3(0.0)), vec3(1.0 / 2.2)); }

// Weather noise; the renderer mirrors these functions to place the clouds that cast the shadows.
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x), mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}
// Cloud density over the world (absolute xz), 0 = clear sky, 1 = thick cloud.
float cloudField(vec2 world) {
    vec2 p = (world + uWeather.zw) / 90.0;
    float n = valueNoise(p) * 0.55 + valueNoise(p * 2.13 + 7.3) * 0.3 + valueNoise(p * 4.37 + 1.7) * 0.15;
    float edge = 1.0 - uWeather.x * 0.75;
    return smoothstep(edge, edge + 0.16, n);
}
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
// Shadow of the cloud layer: look up the clouds where the ray towards the sun crosses it.
float cloudShadow(vec3 worldPos) {
    if (uWeather.x <= 0.001 || uVisualOptions.y < 0.5) return 1.0;
    float rise = max(uOrigin.y - worldPos.y, 0.0);
    vec2 at = worldPos.xz + uOrigin.xz + uSunDir.xz / max(uSunDir.y, 0.25) * rise;
    return 1.0 - cloudField(at) * 0.6;
}
// Light from the lamps: falling off to nothing at each lamp's reach, spots within their cone.
vec3 lampLight(vec3 n, vec3 worldPos) {
    vec3 sum = vec3(0.0);
    int count = int(uLightCount.x);
    for (int i = 0; i < count; ++i) {
        vec3 d = uLightPos[i].xyz - worldPos;
        float dist = length(d);
        float reach = uLightPos[i].w;
        if (dist >= reach) continue;
        vec3 l = d / max(dist, 0.0001);
        float fall = 1.0 - dist / reach;
        fall *= fall;
        float spot = 1.0;
        if (uLightDir[i].w > -1.5) spot = smoothstep(uLightDir[i].w, uLightDir[i].w + 0.12, dot(-l, uLightDir[i].xyz));
        float facing = max(dot(n, l), 0.0) * 0.75 + 0.25;
        sum += uLightColor[i].rgb * (fall * spot * facing);
    }
    return sum;
}
vec3 litColor(vec3 albedo, vec3 n, vec3 worldPos) {
    float ndl = max(dot(n, uSunDir.xyz), 0.0);
    float sh = ndl > 0.0 ? shadowFactor(worldPos, n) * cloudShadow(worldPos) : 0.0;
    vec3 ambient = mix(uAmbientGround.rgb, uAmbientSky.rgb, n.y * 0.5 + 0.5);
    vec3 lamps = uLightCount.x > 0.5 ? lampLight(n, worldPos) : vec3(0.0);
    return albedo * (ambient + uSunColor.rgb * (ndl * sh * uSunDir.w) + lamps);
}
vec3 applyFog(vec3 c, vec3 worldPos) {
    // Mist settles in low ground and in patches, thicker the further it is seen through.
    if (uWeather.y > 0.001) {
        float eye = length(worldPos - uCameraPos.xyz);
        float low = exp(-max(worldPos.y - uOrigin.w, 0.0) * 0.32);
        float patches = 0.55 + 0.9 * valueNoise((worldPos.xz + uOrigin.xz + uWeather.zw * 0.35) / 46.0);
        // Keep the map readable after dark, fading smoothly through dusk and dawn.
        float night = uSunColor.w;
        float mistScale = mix(1.0, uHorizonColor.w, night);
        float mist = clamp(uWeather.y * mistScale * low * patches * (1.0 - exp(-eye * 0.014)),
                           0.0, 0.88 * mistScale);
        // The sun's colour stays sunset-orange below the horizon; it must not light night mist.
        vec3 mistColor = mix(uHorizonColor.rgb, uSunColor.rgb * (0.75 * (1.0 - night)) + uAmbientSky.rgb * 0.6, 0.45);
        c = mix(c, mistColor, mist);
    }
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
flat out float vEmission;
void main() {
    vec3 wp = aPos + uOffset;
    vWorldPos = wp;
    vNormal = aNormal.xyz;
    vColor = toLinear(aColor.rgb);
    vEmission = emissionKind(aColor.a);
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)"});
}

std::string terrainFS() {
    return join({kHeader, kLighting, R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
flat in float vEmission;
uniform float uExplored;
// The elevation view (laying track): x: strength (0 = off), y: a height to mark in levels (none
// below -1), z: world units per height level.
uniform vec4 uElevation;
out vec4 fragColor;

// Colours by height, from the lowlands through the hills to the snowy peaks.
vec3 elevationTint(float level) {
    const float stops[7] = float[7](0.0, 6.0, 14.0, 24.0, 38.0, 56.0, 80.0);
    const vec3 tints[7] = vec3[7](vec3(0.27, 0.6, 0.45), vec3(0.47, 0.72, 0.36), vec3(0.8, 0.8, 0.44),
                                  vec3(0.85, 0.65, 0.36), vec3(0.72, 0.45, 0.33), vec3(0.6, 0.48, 0.55),
                                  vec3(0.93, 0.93, 0.95));
    vec3 c = tints[0];
    for (int i = 1; i < 7; ++i) c = mix(c, tints[i], clamp((level - stops[i - 1]) / (stops[i] - stops[i - 1]), 0.0, 1.0));
    return toLinear(c);
}
// Coverage of a line drawn at every whole value of `v`, about `width` pixels wide. Lines fade
// out where they would crowd together: far away, or on the steepest slopes.
float contour(float v, float width) {
    float fw = max(fwidth(v), 1e-5);
    float d = abs(fract(v + 0.5) - 0.5) / fw;
    return (1.0 - smoothstep(width * 0.5, width * 0.5 + 1.0, d)) * (1.0 - smoothstep(0.2, 0.4, fw));
}

void main() {
    vec3 n = normalize(vNormal);
    vec3 albedo = vColor;
    // Unexplored land is shown desaturated and slightly darker.
    float grey = dot(albedo, vec3(0.3, 0.59, 0.11));
    albedo = mix(vec3(grey), albedo, mix(0.35, 1.0, uExplored)) * mix(0.8, 1.0, uExplored);
    float level = vWorldPos.y / uElevation.z;
    bool elevation = uElevation.x > 0.001 && vWorldPos.y > 0.01;
    if (elevation) albedo = mix(albedo, elevationTint(level) * (grey * 0.6 + 0.55), 0.6 * uElevation.x);
    vec3 c = applyEmission(litColor(albedo, n, vWorldPos), vEmission);
    if (elevation) {
        // A fine contour at every height level and a bold one every five: following a contour
        // keeps track level, crossing them climbs.
        c = mix(c, c * 0.35, contour(level, 1.0) * 0.6 * uElevation.x);
        c = mix(c, vec3(0.03, 0.025, 0.02), contour(level / 5.0, 2.2) * 0.8 * uElevation.x);
        if (uElevation.y > -1.0) {
            float fw = max(fwidth(level), 1e-5);
            float mark = 1.0 - smoothstep(1.5, 3.0, abs(level - uElevation.y) / fw);
            c = mix(c, vec3(1.0, 0.85, 0.25), mark * uElevation.x);
        }
    }
    fragColor = vec4(toSrgb(applyFog(c, vWorldPos)), 1.0);
}
)"});
}

std::string modelFS() {
    return join({kHeader, kLighting, R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
flat in float vEmission;
flat in float vGlass;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNormal);
    // Self-lit windows and lamps bypass sun/shadow lighting, while still receiving distance fog.
    vec3 c = applyEmission(litColor(vColor, n, vWorldPos), vEmission);
    fragColor = vec4(toSrgb(applyFog(c, vWorldPos)), vGlass > 0.5 ? 0.20 : 1.0);
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
flat out float vEmission;
flat out float vGlass;
void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 wp = vec3(dot(iRow0, p), dot(iRow1, p), dot(iRow2, p)) + uOffset;
    // Trees (vertex alpha 250) sway in the wind, more towards their tops, in gusts, and harder
    // under cloud.
    if (uVisualOptions.z > 0.5 && abs(aColor.a * 255.0 - 250.0) < 0.25) {
        vec2 at = wp.xz + uOrigin.xz;
        float t = uCameraPos.w;
        float gust = 0.55 + 0.45 * sin(t * 0.4 + at.x * 0.045 + at.y * 0.031);
        float sway = sin(t * 1.6 + at.x * 0.8 + at.y * 0.6) + 0.5 * sin(t * 2.7 + at.x * 1.9 - at.y * 0.4);
        wp.xz += vec2(0.85, 0.5) * (sway * gust * aPos.y * 0.03 * (1.0 + uWeather.x));
    }
    vec3 n = vec3(dot(iRow0.xyz, aNormal.xyz), dot(iRow1.xyz, aNormal.xyz), dot(iRow2.xyz, aNormal.xyz));
    vNormal = normalize(n);
    vGlass = abs(aColor.a * 255.0 - 253.0) < 0.25 ? 1.0 : 0.0;
    // Vertices with alpha 0 are tintable: they take the instance colour scaled by their red channel.
    vec3 c = aColor.a < 0.5 ? iTint.rgb * (aColor.r * 2.0) : aColor.rgb;
    vColor = toLinear(c);
    vEmission = vGlass > 0.5 ? 1.0 : emissionKind(aColor.a);
    vWorldPos = wp;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)"});
}


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
    wp.y += (sin(q.x * 1.7 + t * 1.1) * 0.03 + sin(q.y * 1.3 - t * 0.9) * 0.03 + sin((q.x + q.y) * 0.9 + t * 1.6) * 0.02) * uVisualOptions.w;
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
    // At night: stars, twinkling and thinning towards the horizon, and the moon opposite the sun.
    float night = uSunColor.w * (1.0 - uWeather.x * 0.9);
    if (night > 0.01 && dir.y > 0.0) {
        vec3 q = dir * 150.0;
        vec3 cell = floor(q);
        float h = hash12(cell.xy + cell.z * 17.31);
        float star = step(0.986, h) * smoothstep(0.35, 0.05, length(fract(q) - 0.5));
        float twinkle = 0.7 + 0.3 * sin(uCameraPos.w * 2.3 + h * 61.0);
        c += vec3(0.85, 0.9, 1.0) * star * twinkle * (0.4 + 0.6 * fract(h * 37.0)) * night * smoothstep(0.0, 0.25, dir.y);
        float md = dot(dir, -uSunDir.xyz);
        float disc = smoothstep(0.99955, 0.99975, md);
        float shade = 0.82 + 0.18 * hash12(floor(dir.xz * 2400.0));
        c = mix(c, vec3(0.92, 0.93, 0.98) * shade, disc * night);
        c += vec3(0.5, 0.55, 0.7) * pow(max(md, 0.0), 300.0) * 0.35 * night;
    }
    if (dir.y > 0.02 && uWeather.x > 0.001) {
        // The cloud layer seen from below, fading towards the horizon.
        vec2 at = uOrigin.xz + uCameraPos.xz + dir.xz / dir.y * max(uOrigin.y - uCameraPos.y, 20.0) * 2.0;
        float cloud = cloudField(at) * smoothstep(0.02, 0.25, dir.y);
        vec3 lit = uAmbientSky.rgb * 1.3 + uSunColor.rgb * uSunDir.w * (0.55 + 0.45 * max(sd, 0.0));
        c = mix(c, lit, cloud * 0.85);
    }
    fragColor = vec4(toSrgb(c), 1.0);
}
)"});
}

std::string cloudVS() {
    return join({kHeader, R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 iRow0;
layout(location = 4) in vec4 iRow1;
layout(location = 5) in vec4 iRow2;
layout(location = 6) in vec4 iTint;
out vec3 vWorldPos;
out vec3 vNormal;
out float vAlpha;
void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 wp = vec3(dot(iRow0, p), dot(iRow1, p), dot(iRow2, p));
    vNormal = normalize(vec3(dot(iRow0.xyz, aNormal.xyz), dot(iRow1.xyz, aNormal.xyz), dot(iRow2.xyz, aNormal.xyz)));
    vAlpha = iTint.a;
    vWorldPos = wp;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)"});
}

std::string cloudFS() {
    return join({kHeader, R"(
in vec3 vWorldPos;
in vec3 vNormal;
in float vAlpha;
out vec4 fragColor;
void main() {
    vec3 n = normalize(vNormal);
    float ndl = max(dot(n, uSunDir.xyz), 0.0);
    // Bright tops, soft grey undersides, warm at sunrise and sunset.
    vec3 c = toLinear(vec3(0.97, 0.98, 1.0)) * (uAmbientSky.rgb * 1.2 + uAmbientGround.rgb * 0.4 * (1.0 - n.y)) +
             uSunColor.rgb * uSunDir.w * (0.35 + 0.65 * ndl);
    c = mix(c, uHorizonColor.rgb, 0.18);
    fragColor = vec4(toSrgb(c), vAlpha);
}
)"});
}

std::string glowVS() {
    return join({kHeader, R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
out vec2 vUv;
out vec4 vColor;
void main() {
    vUv = aUv;
    // Halos fade out with the distance fog.
    float d = length(aPos.xz - uFogParams.zw);
    float fog = clamp((d - uFogParams.x) / max(uFogParams.y - uFogParams.x, 1.0), 0.0, 1.0);
    vColor = vec4(aColor.rgb, aColor.a * (1.0 - fog));
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)"});
}

std::string glowFS() {
    return join({kHeader, R"(
in vec2 vUv;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float r2 = dot(vUv, vUv);
    float a = (exp(-r2 * 6.0) + exp(-r2 * 40.0) * 0.8) * vColor.a;
    if (a < 0.004) discard;
    fragColor = vec4(vColor.rgb * a, 1.0);
}
)"});
}

std::string puffVS() {
    return join({kHeader, R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
out vec2 vUv;
out vec4 vColor;
void main() {
    vUv = aUv;
    float d = length(aPos.xz - uFogParams.zw);
    float fog = clamp((d - uFogParams.x) / max(uFogParams.y - uFogParams.x, 1.0), 0.0, 1.0);
    vColor = vec4(aColor.rgb, aColor.a * (1.0 - fog));
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)"});
}

std::string puffFS() {
    // Soft round puffs of steam and smoke, lit by the sky and the sun: brighter on top, a little
    // denser in the middle, with a lumpy edge.
    return join({kHeader, R"(
in vec2 vUv;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float r2 = dot(vUv, vUv);
    float edge = 1.0 - 0.12 * sin(atan(vUv.y, vUv.x) * 5.0 + vColor.a * 7.0);
    if (r2 > edge) discard;
    float a = (1.0 - r2 / edge);
    a = a * a * (3.0 - 2.0 * a) * vColor.a;
    vec3 light = uAmbientSky.rgb * 1.1 + uAmbientGround.rgb * 0.35 +
                 uSunColor.rgb * uSunDir.w * (0.5 + 0.3 * vUv.y);
    vec3 c = toLinear(vColor.rgb) * light;
    c = mix(c, uHorizonColor.rgb, 0.12);
    fragColor = vec4(toSrgb(c), a);
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
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 iRow0;
layout(location = 4) in vec4 iRow1;
layout(location = 5) in vec4 iRow2;
uniform vec3 uOffset;
void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 wp = vec3(dot(iRow0, p), dot(iRow1, p), dot(iRow2, p)) + uOffset;
    // Trees (vertex alpha 250) sway in the wind, more towards their tops, in gusts, and harder
    // under cloud.
    if (uVisualOptions.z > 0.5 && abs(aColor.a * 255.0 - 250.0) < 0.25) {
        vec2 at = wp.xz + uOrigin.xz;
        float t = uCameraPos.w;
        float gust = 0.55 + 0.45 * sin(t * 0.4 + at.x * 0.045 + at.y * 0.031);
        float sway = sin(t * 1.6 + at.x * 0.8 + at.y * 0.6) + 0.5 * sin(t * 2.7 + at.x * 1.9 - at.y * 0.4);
        wp.xz += vec2(0.85, 0.5) * (sway * gust * aPos.y * 0.03 * (1.0 + uWeather.x));
    }
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

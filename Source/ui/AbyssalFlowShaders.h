#pragma once

namespace amanita::ui::abyssal_flow
{
// Original Amanita Ocean shader. A full-screen liquid field stores its sparse
// emission mask in alpha for the renderer's separable GPU bloom passes.
inline constexpr auto vertex = R"GLSL(
#version 150 core

out vec2 vUv;

void main()
{
    vec2 point = vec2(float((gl_VertexID << 1) & 2),
                      float(gl_VertexID & 2));
    vUv = point;
    gl_Position = vec4(point * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

inline constexpr auto fragment = R"GLSL(
#version 150 core

in vec2 vUv;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uEvolution;
uniform float uFocus;
uniform float uDirectOutput;
uniform vec3 uAccent;
uniform vec4 uCharacterBlend;
uniform float uCurrentBlend;
uniform vec2 uCurrentFlow;
uniform float uCurrentStrength;

float hash21(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float valueNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 local = fract(p);
    local = local * local * local * (local * (local * 6.0 - 15.0) + 10.0);

    float a = hash21(cell);
    float b = hash21(cell + vec2(1.0, 0.0));
    float c = hash21(cell + vec2(0.0, 1.0));
    float d = hash21(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);
}

float flowFbm(vec2 p)
{
    const mat2 octaveRotation = mat2(0.80, -0.60, 0.60, 0.80);
    float result = valueNoise(p) * 0.57;
    p = octaveRotation * p * 2.07 + vec2(13.17, 7.91);
    result += valueNoise(p) * 0.27;
    p = octaveRotation * p * 2.07 + vec2(-5.73, 11.29);
    result += valueNoise(p) * 0.13;
    return result * 1.031;
}

void main()
{
    vec2 resolution = max(uResolution, vec2(1.0));
    vec2 uv = clamp(vUv, 0.0, 1.0);
    vec2 p = uv - 0.5;
    p.x *= resolution.x / resolution.y;

    float evolution = clamp(uEvolution, 0.0, 1.0);
    float focus = clamp(uFocus, 0.0, 1.0);
    vec4 character = max(uCharacterBlend, vec4(0.0));
    float currentBlend = max(uCurrentBlend, 0.0);
    float characterWeight = max(dot(character, vec4(1.0))
                                + currentBlend,
                                0.0001);
    character /= characterWeight;
    currentBlend /= characterWeight;

    vec2 currentVector = clamp(uCurrentFlow, vec2(-1.0), vec2(1.0));
    float currentDepth = currentBlend
                       * clamp(uCurrentStrength, 0.0, 1.0);

    float scaleFactor = (dot(character, vec4(1.00, 0.78, 1.08, 0.84))
                      + currentBlend * 0.94)
                      * mix(0.97, 1.05, evolution);
    vec2 anisotropy;
    anisotropy.x = dot(character, vec4(1.00, 1.00, 1.35, 0.76))
                 + currentBlend * 1.48;
    anisotropy.y = dot(character, vec4(1.00, 1.08, 0.78, 1.32))
                 + currentBlend * 0.72;
    float warpFactor = dot(character, vec4(1.00, 0.90, 1.08, 1.20))
                     + currentBlend * 1.28;
    float speedFactor = dot(character, vec4(0.72, 0.54, 1.24, 0.46))
                      + currentBlend * 0.82;
    float densityFactor = dot(character, vec4(1.15, 1.28, 1.08, 0.88))
                        + currentBlend * 1.12;
    float maskFactor = dot(character, vec4(0.88, 1.18, 1.02, 0.58))
                     + currentBlend * 0.90;
    float time = uTime * speedFactor * mix(0.68, 1.12, evolution);

    vec2 q = p * anisotropy * scaleFactor;
    q += currentVector * currentDepth * 0.16;
    vec2 warp;
    warp.x = flowFbm(q * 0.78
                     + time * vec2(0.008, -0.006));
    warp.y = flowFbm(q * 0.78 + vec2(7.31, -4.17)
                     + time * vec2(-0.006, 0.009));
    warp = warp * 2.0 - 1.0;
    warp += currentDepth
          * vec2(currentVector.y, -currentVector.x) * 0.12;

    vec2 flow = q
              + warp * mix(0.34, 0.76, evolution) * warpFactor
              + time * vec2(-0.006, 0.004);
    flow += currentDepth
          * (currentVector * 0.22 + vec2(-p.y, p.x) * 0.06);
    float body = flowFbm(flow * 0.94);
    float undercurrent = flowFbm(
        mat2(0.78, -0.63, 0.63, 0.78) * flow * 1.46
        - warp * 0.42
        + time * vec2(0.010, -0.013));
    float detail = valueNoise(
        flow * 1.90 + warp.yx * 0.30
        + time * vec2(-0.016, 0.011));

    float farSignal = body * 0.72 + undercurrent * 0.28;
    float farDensity = smoothstep(0.33, 0.66, farSignal);
    float midSignal = body * 0.48 + undercurrent * 0.52;
    float midDensity = smoothstep(0.37, 0.68, midSignal);
    float nearSignal = undercurrent * 0.66 + detail * 0.34;
    float nearDensity = smoothstep(mix(0.57, 0.50, evolution),
                                   mix(0.79, 0.72, evolution),
                                   nearSignal)
                      * smoothstep(0.10, 0.55, midDensity);
    farDensity = pow(farDensity, mix(0.96, 1.02, focus));
    midDensity = pow(midDensity, mix(0.94, 1.05, focus));
    nearDensity = pow(nearDensity, mix(0.92, 1.08, focus));

    vec3 accent = clamp(uAccent, vec3(0.0), vec3(1.0));
    float luminance = dot(accent, vec3(0.2126, 0.7152, 0.0722));
    accent = mix(vec3(luminance), accent, 0.72);

    vec3 deepTop = vec3(0.014, 0.043, 0.050);
    vec3 deepBottom = vec3(0.004, 0.015, 0.020);
    vec3 colour = mix(deepBottom, deepTop, pow(1.0 - uv.y, 0.74));
    vec3 farTint = mix(vec3(0.010, 0.040, 0.048), accent * 0.130, 0.35);
    vec3 midTint = mix(vec3(0.014, 0.052, 0.062), accent * 0.190, 0.55);
    vec3 nearTint = mix(vec3(0.018, 0.062, 0.072), accent * 0.250, 0.68);
    colour += farTint * farDensity
            * mix(0.72, 1.08, evolution) * densityFactor;
    colour += midTint * midDensity
            * mix(0.80, 1.24, evolution) * densityFactor;
    colour += nearTint * nearDensity
            * mix(0.58, 1.12, evolution) * densityFactor;
    colour *= 0.92;

    float vignette = 1.0 - smoothstep(
        0.52, 1.04, length(p * vec2(0.72, 1.10)));
    colour *= mix(0.96, 1.0, vignette);

    float peak = undercurrent * 0.64 + detail * 0.36;
    float core = smoothstep(mix(0.70, 0.63, evolution),
                            mix(0.84, 0.77, evolution),
                            peak);
    float gate = smoothstep(0.45, 0.64, body + warp.x * 0.06);
    float softCurrent = smoothstep(0.48, 0.72,
                                   undercurrent * 0.62 + body * 0.38)
                      * smoothstep(0.10, 0.64, midDensity);
    float emission = max(pow(core * gate, 1.08),
                         softCurrent * mix(0.18, 0.34, evolution))
                   * maskFactor * mix(0.58, 1.10, evolution);

    colour /= vec3(1.0) + colour * 0.30;
    colour = pow(max(colour, vec3(0.0)), vec3(0.82));
    float outputAlpha = mix(clamp(emission, 0.0, 1.0),
                            1.0,
                            clamp(uDirectOutput, 0.0, 1.0));
    fragColor = vec4(colour, outputAlpha);
}
)GLSL";

inline constexpr auto blurFragment = R"GLSL(
#version 150 core

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uSource;
uniform vec2 uDirection;

float maskAt(vec2 offset)
{
    return texture(uSource, clamp(vUv + offset, 0.0, 1.0)).a;
}

void main()
{
    float blurred = maskAt(vec2(0.0)) * 0.227027;
    blurred += (maskAt(uDirection * 1.384615)
                + maskAt(-uDirection * 1.384615)) * 0.316216;
    blurred += (maskAt(uDirection * 3.230769)
                + maskAt(-uDirection * 3.230769)) * 0.070270;
    fragColor = vec4(0.0, 0.0, 0.0, blurred);
}
)GLSL";

inline constexpr auto compositeFragment = R"GLSL(
#version 150 core

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uEvolution;
uniform float uFocus;
uniform vec3 uAccent;
uniform vec4 uCharacterBlend;
uniform float uCurrentBlend;

void main()
{
    vec2 uv = clamp(vUv, 0.0, 1.0);
    vec4 scene = texture(uScene, uv);
    float halo = texture(uBloom, uv).a;
    float evolution = clamp(uEvolution, 0.0, 1.0);
    float focus = clamp(uFocus, 0.0, 1.0);
    vec4 character = max(uCharacterBlend, vec4(0.0));
    float currentBlend = max(uCurrentBlend, 0.0);
    float characterWeight = max(dot(character, vec4(1.0))
                                + currentBlend,
                                0.0001);
    character /= characterWeight;
    currentBlend /= characterWeight;

    vec3 accent = clamp(uAccent, vec3(0.0), vec3(1.0));
    float luminance = dot(accent, vec3(0.2126, 0.7152, 0.0722));
    accent = mix(vec3(luminance), accent, 0.74);
    vec3 glowTint = mix(vec3(0.040, 0.125, 0.138), accent, 0.70);

    float coreGain = (dot(character, vec4(0.068, 0.086, 0.081, 0.041))
                    + currentBlend * 0.052)
                   * mix(0.55, 1.15, evolution)
                   * mix(0.88, 1.12, focus);
    float haloGain = (dot(character, vec4(0.44, 0.63, 0.49, 0.55))
                    + currentBlend * 0.56)
                   * mix(0.68, 1.25, evolution)
                   * mix(1.12, 1.00, focus);
    float light = scene.a * coreGain
                + halo * haloGain * uBloomStrength;

    vec3 colour = scene.rgb + glowTint * light;
    colour /= vec3(1.0) + colour * 0.18;
    fragColor = vec4(colour, 1.0);
}
)GLSL";
} // namespace amanita::ui::abyssal_flow

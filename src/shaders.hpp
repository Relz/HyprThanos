#pragma once

#include <string_view>

namespace HyprThanos::Shaders {

    inline constexpr std::string_view VERTEX = R"GLSL(#version 300 es
precision highp float;
precision highp int;

uniform mat3 proj;
uniform float u_progress;
uniform float u_seed;
uniform float u_grain_size;
uniform float u_spread;
uniform float u_lateral_repulsion;
uniform vec2 u_direction;
uniform float u_turbulence;
uniform vec4 u_source_box;
uniform vec4 u_window_box;
uniform vec2 u_texture_size;
uniform vec2 u_draw_size;
uniform ivec2 u_grid_size;

in vec2 pos;

out highp vec2 v_local;
flat out highp vec2 v_tile_origin;
flat out highp vec2 v_tile_size;
flat out highp float v_particle_opacity;
flat out highp float v_softness;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33 + u_seed);
    return fract((p3.x + p3.y) * p3.z);
}

float fourthPower(float value) {
    float squared = value * value;
    return squared * squared;
}

float weightedReleasePosition(float peak, vec2 grain) {
    float leftMass = 1.0 - fourthPower(1.0 - peak);
    float rightMass = 1.0 - fourthPower(peak);
    float totalMass = max(leftMass + rightMass, 0.0001);
    bool useLeft = hash12(grain + 17.0) * totalMass < leftMass;
    float span = useLeft ? peak : 1.0 - peak;
    float spanMass = 1.0 - fourthPower(1.0 - span);
    float remainder = max(1.0 - hash12(grain + 43.0) * spanMass, 0.0);
    float distance = 1.0 - sqrt(sqrt(remainder));
    return clamp(peak + (useLeft ? -distance : distance), 0.0, 0.999999);
}

void main() {
    int columns = max(u_grid_size.x, 1);
    int instance = gl_InstanceID;
    ivec2 grid = ivec2(instance % columns, instance / columns);
    vec2 grain = vec2(grid);

    float grainSize = max(u_grain_size, 1.0);
    vec2 tileOrigin = u_source_box.xy + grain * grainSize;
    vec2 tileSize = min(vec2(grainSize), u_source_box.xy + u_source_box.zw - tileOrigin);
    vec2 tileCenter = tileOrigin + tileSize * 0.5;
    vec2 windowSize = max(u_window_box.zw, vec2(1.0));
    vec2 windowCenter = u_window_box.xy + windowSize * 0.5;

    float peak = clamp((tileCenter.y - u_window_box.y) / windowSize.y, 0.0, 1.0);
    float releaseProgress = weightedReleasePosition(peak, grain);
    float particleStart = 0.55 * releaseProgress;
    float particleEnd = 0.23 + 0.77 * releaseProgress;

    if (u_progress >= particleEnd || tileSize.x <= 0.0 || tileSize.y <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    float particleAge = clamp((u_progress - particleStart) / max(particleEnd - particleStart, 0.001), 0.0, 1.0);
    float motionProgress = particleAge * (2.0 - particleAge);
    float fadeStart = mix(0.20, 0.42, hash12(grain + 149.0));
    float particleOpacity = 1.0 - smoothstep(fadeStart, 1.0, particleAge);

    vec2 direction = u_direction;
    float directionLength = length(direction);
    direction = directionLength > 0.0001 ? direction / directionLength : vec2(1.0, 0.0);
    vec2 lateralAxis = vec2(-direction.y, direction.x);
    float lateralHalfSpan = max(dot(abs(lateralAxis), windowSize * 0.5), 1.0);
    float lateralSide = clamp(dot(tileCenter - windowCenter, lateralAxis) / lateralHalfSpan, -1.0, 1.0);
    float lateralAmount = lateralSide * clamp(u_lateral_repulsion, 0.0, 1.0);
    vec2 fannedDirection = (direction + lateralAxis * lateralAmount) * inversesqrt(1.0 + lateralAmount * lateralAmount);

    float displacementScale = max(windowSize.x, windowSize.y) * u_spread;
    float travelScale = mix(0.78, 1.0, hash12(grain + 191.0));
    vec2 controlJitter = vec2(hash12(grain + 71.0), hash12(grain + 113.0)) * 2.0 - 1.0;
    vec2 endJitter = vec2(hash12(grain + 233.0), hash12(grain + 277.0)) * 2.0 - 1.0;
    float remainingMotion = 1.0 - motionProgress;
    vec2 curvedJitter = 2.0 * remainingMotion * motionProgress * controlJitter + motionProgress * motionProgress * endJitter;
    vec2 displacement = fannedDirection * displacementScale * travelScale * motionProgress;
    displacement += curvedJitter * u_turbulence * displacementScale * 0.35;

    float blurProgress = clamp(u_progress / 0.23, 0.0, 1.0);
    float softness = 0.8 * (1.0 - (1.0 - blurProgress) * (1.0 - blurProgress)) * smoothstep(0.0, 0.20, particleAge);
    vec2 local = pos * (tileSize + 2.0 * softness) - softness;

    vec2 drawSize = max(abs(u_draw_size), vec2(1.0));
    vec2 targetCenter = tileCenter / u_texture_size * drawSize;
    vec2 targetLocal = (local - tileSize * 0.5) / u_texture_size * drawSize;
    float spin = hash12(grain + 313.0) * 2.0 - 1.0;
    float spinScale = mix(0.65, 1.0, hash12(grain + 347.0));
    float angle = radians(spin * spinScale * 15.0 * motionProgress);
    float cosine = cos(angle);
    float sine = sin(angle);
    vec2 rotatedLocal = vec2(cosine * targetLocal.x - sine * targetLocal.y, sine * targetLocal.x + cosine * targetLocal.y);
    vec2 targetPosition = targetCenter + rotatedLocal + displacement;

    gl_Position = vec4(proj * vec3(targetPosition / drawSize, 1.0), 1.0);
    v_local = local;
    v_tile_origin = tileOrigin;
    v_tile_size = tileSize;
    v_particle_opacity = particleOpacity;
    v_softness = softness;
}
)GLSL";

    inline constexpr std::string_view FRAGMENT = R"GLSL(#version 300 es
precision highp float;

uniform sampler2D tex;
uniform float alpha;
uniform vec2 u_texture_size;

in highp vec2 v_local;
flat in highp vec2 v_tile_origin;
flat in highp vec2 v_tile_size;
flat in highp float v_particle_opacity;
flat in highp float v_softness;

layout(location = 0) out vec4 fragColor;

void main() {
    vec2 outside = max(max(-v_local, v_local - v_tile_size), vec2(0.0));
    float feather = v_softness > 0.0001 ? 1.0 - smoothstep(0.0, v_softness, length(outside)) : 1.0;
    if (feather <= 0.0001)
        discard;

    vec2 halfPixel = min(vec2(0.5), v_tile_size * 0.5);
    vec2 maxSample = max(v_tile_size - halfPixel, halfPixel);
    vec2 samplePixel = v_tile_origin + clamp(v_local, halfPixel, maxSample);
    samplePixel = clamp(samplePixel, vec2(0.5), u_texture_size - vec2(0.5));
    vec4 source = texture(tex, samplePixel / u_texture_size);
    if (source.a <= 0.00001)
        discard;

    float opacity = clamp(alpha, 0.0, 1.0) * v_particle_opacity * feather;
    if (opacity <= 0.0001)
        discard;

    fragColor = source * opacity;
}
)GLSL";

}

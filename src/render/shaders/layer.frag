#version 440
// All drawing modes of the GPU renderer. Mirrors editor/Compositor.cpp:
// the same 8-bit color curves (as a 256x1 texture), the same LUT, the same
// shapes (antialiased with signed distances), shadows, vignette and border.

layout(location = 0) in vec2 vLocal;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Params {
    mat4 mvp;        // local pixels -> clip space
    vec4 quad;       // drawn quad in local pixels: x, y, w, h
    vec4 shape;      // w, h, corner radius (px), circle (0/1)
    vec4 uvMap;      // texture uv = uvMap.xy + local * uvMap.zw
    vec4 look;       // opacity, border width (px), vignette, mode
    vec4 grade;      // saturation, use curves (0/1), mirror (0/1), shadow sigma (px)
    vec4 lutScale;   // xyz: LUT coordinate scale, w: LUT mix (0 = no LUT)
    vec4 lutOffset;  // xyz: LUT coordinate offset
    vec4 color0;     // border / placeholder / gradient start (straight alpha)
    vec4 color1;     // gradient end; w: shadow opacity
    vec4 blurDir;    // xy: texel step, z: sigma (texels), w: taps per side
    vec4 inputColor; // x: transfer (0 BT.709, 1 sRGB, 2 PQ, 3 HLG), y: opaque source, z: convert (0/1)
    vec4 gamut0;     // source gamut -> Rec.709 (linear light), rows
    vec4 gamut1;
    vec4 gamut2;
    vec4 grade2;     // x: color boost (vibrance), y: hue rotation (radians)
};

layout(binding = 1) uniform sampler2D tex;      // source (premultiplied)
layout(binding = 2) uniform sampler2D curves;   // 256x1, nearest
layout(binding = 3) uniform sampler3D lut;      // RGB LUT
layout(binding = 4) uniform sampler2D aux;      // second image (blurred copy)
layout(binding = 5) uniform sampler2D mask;     // person mask (r)

const int MODE_GRADIENT = 0;
const int MODE_MEDIA = 1;
const int MODE_SHADOW = 2;
const int MODE_SOLID = 3;
const int MODE_OVERLAY = 4;
const int MODE_PREPARE = 5;
const int MODE_BLUR = 6;
const int MODE_MASK_MIX = 7;
const int MODE_NV12_Y = 8;
const int MODE_NV12_UV = 9;

float sdRoundBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Signed distance to the layer shape, in pixels (negative inside).
float shapeDistance(vec2 p)
{
    vec2 halfSize = shape.xy * 0.5;
    vec2 c = p - halfSize;
    if (shape.w > 0.5)
        return length(c) - min(halfSize.x, halfSize.y);
    return sdRoundBox(c, halfSize, min(shape.z, min(halfSize.x, halfSize.y)));
}

// Source sample with the layer mapping (crop, zoom, fit/fill, mirror).
vec2 sourceUv(vec2 p)
{
    vec2 q = p;
    if (grade.z > 0.5)
        q.x = shape.x - q.x;
    return uvMap.xy + q * uvMap.zw;
}

// Input color (mirrors editor::convertInputColor): source code values ->
// Rec.709 display-referred code values; HDR is tone-mapped to SDR.
const float kReferenceWhite = 203.0;
const float kKnee = 0.8;

vec3 pqToNits(vec3 e)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    vec3 p = pow(clamp(e, 0.0, 1.0), vec3(1.0 / m2));
    return 10000.0 * pow(max(p - c1, 0.0) / (c2 - c3 * p), vec3(1.0 / m1));
}

vec3 hlgToScene(vec3 e)
{
    const float a = 0.17883277;
    const float b = 1.0 - 4.0 * a;
    const float c = 0.5 - a * log(4.0 * a);
    e = clamp(e, 0.0, 1.0);
    vec3 low = e * e / 3.0;
    vec3 high = (exp((e - c) / a) + b) / 12.0;
    return mix(high, low, vec3(lessThanEqual(e, vec3(0.5))));
}

vec3 srgbToLinear(vec3 v)
{
    return mix(pow((v + 0.055) / 1.055, vec3(2.4)), v / 12.92, vec3(lessThanEqual(v, vec3(0.04045))));
}

vec3 convertInput(vec3 code)
{
    int transfer = int(inputColor.x + 0.5);
    vec3 lin;
    if (transfer == 2) {
        lin = pqToNits(code) / kReferenceWhite;
    } else if (transfer == 3) {
        vec3 scene = hlgToScene(code);
        float ys = dot(scene, vec3(0.2627, 0.6780, 0.0593));
        float ootf = ys > 0.0 ? pow(ys, 0.2) : 0.0;
        lin = 1000.0 * ootf * scene / kReferenceWhite;
    } else if (transfer == 1) {
        lin = srgbToLinear(code);
    } else {
        lin = pow(max(code, 0.0), vec3(2.4));
    }
    vec3 o = max(vec3(dot(gamut0.xyz, lin), dot(gamut1.xyz, lin), dot(gamut2.xyz, lin)), 0.0);
    if (transfer >= 2) {
        float peak = max(o.r, max(o.g, o.b));
        if (peak > kKnee) {
            float rolled = kKnee + (1.0 - kKnee) * (1.0 - exp(-(peak - kKnee) / (1.0 - kKnee)));
            o *= rolled / peak;
        }
    }
    return pow(clamp(o, 0.0, 1.0), vec3(1.0 / 2.4));
}

vec3 applyGrade(vec3 rgb)
{
    if (lutScale.w > 0.0) {
        vec3 graded = texture(lut, rgb * lutScale.xyz + lutOffset.xyz).rgb;
        rgb = clamp(mix(rgb, graded, lutScale.w), 0.0, 1.0);
    }
    if (grade.y > 0.5) {
        ivec3 i = ivec3(floor(clamp(rgb, 0.0, 1.0) * 255.0 + 0.5));
        rgb = vec3(texelFetch(curves, ivec2(i.r, 0), 0).r,
                   texelFetch(curves, ivec2(i.g, 0), 0).g,
                   texelFetch(curves, ivec2(i.b, 0), 0).b);
    }
    if (grade.x != 1.0) {
        float l = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        rgb = clamp(vec3(l) + (rgb - vec3(l)) * grade.x, 0.0, 1.0);
    }
    if (grade2.x != 0.0) {  // color boost: muted colors gain more (mirrors editor::boostAndHue)
        float l = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        float chroma = max(rgb.r, max(rgb.g, rgb.b)) - min(rgb.r, min(rgb.g, rgb.b));
        rgb = vec3(l) + (rgb - vec3(l)) * (1.0 + grade2.x * (1.0 - chroma));
    }
    if (grade2.y != 0.0) {  // hue rotation around the gray axis
        float c = cos(grade2.y);
        float s = sin(grade2.y);
        mat3 m = mat3(0.2126 + 0.7874 * c - 0.2126 * s, 0.2126 - 0.2126 * c + 0.143 * s, 0.2126 - 0.2126 * c - 0.7874 * s,
                      0.7152 - 0.7152 * c - 0.7152 * s, 0.7152 + 0.2848 * c + 0.140 * s, 0.7152 - 0.7152 * c + 0.7152 * s,
                      0.0722 - 0.0722 * c + 0.9278 * s, 0.0722 - 0.0722 * c - 0.283 * s, 0.0722 + 0.9278 * c + 0.0722 * s);
        rgb = m * rgb;
    }
    return clamp(rgb, 0.0, 1.0);
}

vec4 gradedSource(vec2 uv)
{
    vec4 s = texture(tex, uv);
    if (inputColor.y > 0.5)
        s.a = 1.0;  // 10-bit sources carry no alpha
    if (s.a <= 0.0)
        return vec4(0.0);
    vec3 rgb = s.rgb / s.a;
    if (inputColor.z > 0.5)
        rgb = convertInput(rgb);
    rgb = applyGrade(rgb);
    return vec4(rgb * s.a, s.a);
}

// Gaussian-blurred rounded box (Evan Wallace, "Fast rounded rectangle shadows").
vec2 erf2(vec2 x)
{
    vec2 s = sign(x), a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}
float gaussian(float x, float sigma)
{
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (sqrt(2.0 * 3.141592653589793) * sigma);
}
float shadowX(float x, float y, float sigma, float corner, vec2 halfSize)
{
    float delta = min(halfSize.y - corner - abs(y), 0.0);
    float curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    vec2 integral = 0.5 + 0.5 * erf2((x + vec2(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}
float roundedBoxShadow(vec2 halfSize, vec2 point, float sigma, float corner)
{
    float low = point.y - halfSize.y;
    float high = point.y + halfSize.y;
    float start = clamp(-3.0 * sigma, low, high);
    float end = clamp(3.0 * sigma, low, high);
    float step = (end - start) / 8.0;
    float y = start + step * 0.5;
    float value = 0.0;
    for (int i = 0; i < 8; ++i) {
        value += shadowX(point.x, point.y - y, sigma, corner, halfSize) * gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

void main()
{
    int mode = int(look.w + 0.5);
    vec2 p = vLocal;

    if (mode == MODE_GRADIENT) {
        // Linear gradient from the top-left to the bottom-right corner.
        float t = clamp(dot(p, shape.xy) / dot(shape.xy, shape.xy), 0.0, 1.0);
        fragColor = vec4(mix(color0.rgb, color1.rgb, t), 1.0);
        return;
    }
    if (mode == MODE_PREPARE) {
        fragColor = gradedSource(sourceUv(p));
        return;
    }
    if (mode == MODE_BLUR) {
        vec2 uv = p / shape.xy;
        float sigma = max(blurDir.z, 0.001);
        int taps = int(blurDir.w);
        vec4 sum = texture(tex, uv);
        float total = 1.0;
        for (int i = 1; i <= taps; ++i) {
            float w = exp(-float(i * i) / (2.0 * sigma * sigma));
            vec2 o = blurDir.xy * float(i);
            sum += (texture(tex, uv + o) + texture(tex, uv - o)) * w;
            total += 2.0 * w;
        }
        fragColor = sum / total;
        return;
    }
    if (mode == MODE_MASK_MIX) {
        // out = person * sharp + (1 - person) * blurred
        vec2 uv = p / shape.xy;
        float person = texture(mask, sourceUv(p)).r;
        fragColor = mix(texture(aux, uv), texture(tex, uv), person);
        return;
    }
    if (mode == MODE_NV12_Y || mode == MODE_NV12_UV) {
        // BT.709, limited range, from the finished canvas (export).
        // UV: the sample sits between four canvas pixels, so the linear
        // filter averages the 2x2 block.
        vec3 rgb = texture(tex, p / shape.xy).rgb;
        float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        if (mode == MODE_NV12_Y) {
            fragColor = vec4((16.0 + 219.0 * y) / 255.0, 0.0, 0.0, 1.0);
        } else {
            float cb = (rgb.b - y) / 1.8556;
            float cr = (rgb.r - y) / 1.5748;
            fragColor = vec4((128.0 + 224.0 * cb) / 255.0, (128.0 + 224.0 * cr) / 255.0, 0.0, 1.0);
        }
        return;
    }
    if (mode == MODE_OVERLAY) {
        fragColor = texture(tex, uvMap.xy + p * uvMap.zw) * look.x;
        return;
    }
    if (mode == MODE_SHADOW) {
        vec2 halfSize = shape.xy * 0.5;
        float corner = shape.w > 0.5 ? min(halfSize.x, halfSize.y) : min(shape.z, min(halfSize.x, halfSize.y));
        float sigma = max(grade.w, 0.5);
        // Deep inside the shape the blurred silhouette is solid: skip the integral.
        float a = shapeDistance(p) < -3.0 * sigma ? 1.0 : roundedBoxShadow(halfSize, p - halfSize, sigma, corner);
        fragColor = vec4(0.0, 0.0, 0.0, clamp(a, 0.0, 1.0) * color1.w);
        return;
    }

    float d = shapeDistance(p);
    float coverage = clamp(0.5 - d, 0.0, 1.0);
    vec4 c = vec4(0.0);
    if (mode == MODE_SOLID) {
        c = vec4(color0.rgb * color0.a, color0.a) * coverage;
    } else {  // MODE_MEDIA
        c = (uvMap.z == 0.0 && uvMap.w == 0.0) ? vec4(0.0) : gradedSource(sourceUv(p));
        if (look.z > 0.0) {
            float r = length(p - shape.xy * 0.5) / (length(shape.xy) * 0.5);
            float v = clamp((r - 0.5) / 0.5, 0.0, 1.0) * (230.0 / 255.0) * look.z;
            c.rgb *= 1.0 - v;
        }
        c *= coverage;
        if (look.y > 0.0) {  // border stroke centered on the outline
            float b = clamp(look.y * 0.5 + 0.5 - abs(d), 0.0, 1.0) * color0.a;
            c = vec4(color0.rgb, 1.0) * b + c * (1.0 - b);
        }
    }
    fragColor = c * look.x;
}

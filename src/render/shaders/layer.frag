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
    vec4 grade2;     // x: color boost (vibrance), y: hue rotation (radians), z: HSL curves (0/1)
    vec4 fx;         // x: grain amount, y: grain size (px), z: grain seed; threshold: x threshold;
                     // denoise: x luma sigma, y chroma sigma, z step (px), w spatial sigma (px); sharpen: x strength, y coring
    vec4 nodeInfo;   // x: node count, y: source aspect (w/h), z: person mask bound (0/1), w: highlighted node (-1 none)
    vec4 nodes[70];  // 7 per node (up to 10): grade, window0, window1, qual0, qual1, qual2, misc (see layer.frag)
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
const int MODE_THRESHOLD = 10;
const int MODE_ADD = 11;
const int MODE_DENOISE = 12;
const int MODE_SHARPEN = 13;

// Film grain (mirrors editor::grainNoise / addGrain): value noise from an
// integer hash, so the CPU and the GPU draw the same grain.
uint hash32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float lattice(int ix, int iy, uint seed)
{
    uint h = hash32(uint(ix) * 0x8da6b343u ^ hash32(uint(iy) * 0xd8163841u ^ seed));
    return float(h & 0xFFFFFFu) / 16777215.0;
}
float grainNoise(vec2 p, float size, uint seed)
{
    vec2 f = p / size;
    vec2 i = floor(f);
    vec2 t = f - i;
    int ix = int(i.x);
    int iy = int(i.y);
    float top = mix(lattice(ix, iy, seed), lattice(ix + 1, iy, seed), t.x);
    float bottom = mix(lattice(ix, iy + 1, seed), lattice(ix + 1, iy + 1, seed), t.x);
    return clamp((mix(top, bottom, t.y) - 0.5) * 3.4, -1.0, 1.0);
}
vec4 addGrain(vec4 c, vec2 p)
{
    if (fx.x <= 0.0 || c.a <= 0.0)
        return c;
    float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)) / c.a;
    float w = 0.4 + 2.4 * l * (1.0 - l);
    float d = grainNoise(p, fx.y, uint(fx.z + 0.5)) * fx.x * 0.12 * w * c.a;
    return vec4(clamp(c.rgb + vec3(d), 0.0, 1.0), c.a);
}

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

// The per-channel curves of row `row` of the curves texture (8-bit tables).
vec3 curvesRow(vec3 rgb, int row)
{
    ivec3 i = ivec3(floor(clamp(rgb, 0.0, 1.0) * 255.0 + 0.5));
    return vec3(texelFetch(curves, ivec2(i.r, row), 0).r,
                texelFetch(curves, ivec2(i.g, row), 0).g,
                texelFetch(curves, ivec2(i.b, row), 0).b);
}

// HSL curves (mirrors editor::applyHsl). The curves texture holds, for grade
// row n (0 = the layer's grade, k + 1 = node k): row HSL_A + n = hue vs hue,
// hue vs sat, hue vs lum, lum vs sat; row HSL_B + n = sat vs sat, sat vs lum.
const int HSL_A = 11;
const int HSL_B = 22;
vec3 applyHsl(vec3 rgb, int n)
{
    float mx = max(rgb.r, max(rgb.g, rgb.b));
    float mn = min(rgb.r, min(rgb.g, rgb.b));
    float chroma = mx - mn;
    float h = 0.0;
    if (chroma > 1e-6) {
        if (mx == rgb.r)
            h = (rgb.g - rgb.b) / chroma;
        else if (mx == rgb.g)
            h = 2.0 + (rgb.b - rgb.r) / chroma;
        else
            h = 4.0 + (rgb.r - rgb.g) / chroma;
        h /= 6.0;
        if (h < 0.0)
            h += 1.0;
    }
    float s = mx > 1e-6 ? chroma / mx : 0.0;
    float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    ivec3 i = ivec3(floor(clamp(vec3(h, s, y), 0.0, 1.0) * 255.0 + 0.5));
    vec4 byHue = texelFetch(curves, ivec2(i.x, HSL_A + n), 0);
    float lumSat = texelFetch(curves, ivec2(i.z, HSL_A + n), 0).a;
    vec4 bySat = texelFetch(curves, ivec2(i.y, HSL_B + n), 0);
    const float neutral = 128.0 / 255.0;
    float shift = byHue.r - neutral;
    float gain = (byHue.g / neutral) * (lumSat / neutral) * (bySat.r / neutral);
    float lift = (byHue.b - neutral) * s + (bySat.g - neutral);
    float cb = (rgb.b - y) / 1.8556;
    float cr = (rgb.r - y) / 1.5748;
    if (shift != 0.0) {
        float a = shift * 6.283185307179586;
        float ca = cos(a);
        float sa = sin(a);
        float ncb = cb * ca - cr * sa;
        cr = cb * sa + cr * ca;
        cb = ncb;
    }
    cb *= gain;
    cr *= gain;
    float y2 = y + lift;
    float r2 = y2 + 1.5748 * cr;
    float b2 = y2 + 1.8556 * cb;
    return clamp(vec3(r2, (y2 - 0.2126 * r2 - 0.0722 * b2) / 0.7152, b2), 0.0, 1.0);
}

// Saturation, HSL curves (row n, or none when n < 0), color boost and hue
// rotation (mirrors editor::applyColor / boostAndHue).
vec3 finishGrade(vec3 rgb, float saturation, float boost, float hueAngle, int hslRow)
{
    if (saturation != 1.0) {
        float l = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        rgb = clamp(vec3(l) + (rgb - vec3(l)) * saturation, 0.0, 1.0);
    }
    if (hslRow >= 0)
        rgb = applyHsl(rgb, hslRow);
    if (boost != 0.0) {  // color boost: muted colors gain more
        float l = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        float chroma = max(rgb.r, max(rgb.g, rgb.b)) - min(rgb.r, min(rgb.g, rgb.b));
        rgb = vec3(l) + (rgb - vec3(l)) * (1.0 + boost * (1.0 - chroma));
    }
    if (hueAngle != 0.0) {  // hue rotation around the gray axis
        float c = cos(hueAngle);
        float s = sin(hueAngle);
        mat3 m = mat3(0.2126 + 0.7874 * c - 0.2126 * s, 0.2126 - 0.2126 * c + 0.143 * s, 0.2126 - 0.2126 * c - 0.7874 * s,
                      0.7152 - 0.7152 * c - 0.7152 * s, 0.7152 + 0.2848 * c + 0.140 * s, 0.7152 - 0.7152 * c + 0.7152 * s,
                      0.0722 - 0.0722 * c + 0.9278 * s, 0.0722 - 0.0722 * c - 0.283 * s, 0.0722 + 0.9278 * c + 0.0722 * s);
        rgb = m * rgb;
    }
    return clamp(rgb, 0.0, 1.0);
}

vec3 applyGrade(vec3 rgb)
{
    if (lutScale.w > 0.0) {
        vec3 graded = texture(lut, rgb * lutScale.xyz + lutOffset.xyz).rgb;
        rgb = clamp(mix(rgb, graded, lutScale.w), 0.0, 1.0);
    }
    if (grade.y > 0.5)
        rgb = curvesRow(rgb, 0);
    return finishGrade(rgb, grade.x, grade2.x, grade2.y, grade2.z > 0.5 ? 0 : -1);
}

// ---- Nodes (mirror editor::windowMatte / qualifierMatte / nodeMatte / applyNodes)
// Per node n, nodes[7n + k]:
//   0 grade:   saturation multiplier, color boost, hue rotation (radians), HSL curves (0/1)
//   1 window0: shape (0 none, 1 circle, 2 rectangle, 3 gradient), center x, center y, rotation (radians)
//   2 window1: width, height, softness, invert
//   3 qual0:   enabled, hue, hue width, hue softness
//   4 qual1:   sat low, sat high, sat softness, invert
//   5 qual2:   lum low, lum high, lum softness, subject (0 none, 1 person, 2 background)
//   6 misc:    invert the whole selection, -, -, -
// The node's curves are row n + 1 of the curves texture.

float windowMatte(int n, vec2 uv)
{
    vec4 w0 = nodes[n * 7 + 1];
    vec4 w1 = nodes[n * 7 + 2];
    int shapeKind = int(w0.x + 0.5);
    if (shapeKind == 0)
        return 1.0;
    float aspect = nodeInfo.y;
    vec2 p = vec2((uv.x - w0.y) * aspect, uv.y - w0.z);
    float c = cos(w0.w);
    float s = sin(w0.w);
    vec2 q = vec2(c * p.x + s * p.y, -s * p.x + c * p.y);
    vec2 halfSize = vec2(max(w1.x * aspect * 0.5, 1e-4), max(w1.y * 0.5, 1e-4));
    float m;
    if (shapeKind == 3) {
        m = 1.0 - smoothstep(-1.0, 1.0, q.y / halfSize.y);
    } else {
        float e = shapeKind == 2 ? max(abs(q.x) / halfSize.x, abs(q.y) / halfSize.y) : length(q / halfSize);
        float soft = max(w1.z * 0.5, 0.004);
        m = 1.0 - smoothstep(1.0 - soft, 1.0 + soft, e);
    }
    return w1.w > 0.5 ? 1.0 - m : m;
}

float qualifierMatte(int n, vec3 rgb)
{
    vec4 q0 = nodes[n * 7 + 3];
    if (q0.x < 0.5)
        return 1.0;
    vec4 q1 = nodes[n * 7 + 4];
    vec4 q2 = nodes[n * 7 + 5];
    float mx = max(rgb.r, max(rgb.g, rgb.b));
    float mn = min(rgb.r, min(rgb.g, rgb.b));
    float chroma = mx - mn;
    float h = 0.0;
    if (chroma > 1e-6) {
        if (mx == rgb.r)
            h = (rgb.g - rgb.b) / chroma;
        else if (mx == rgb.g)
            h = 2.0 + (rgb.b - rgb.r) / chroma;
        else
            h = 4.0 + (rgb.r - rgb.g) / chroma;
        h /= 6.0;
        if (h < 0.0)
            h += 1.0;
    }
    float sat = mx > 1e-6 ? chroma / mx : 0.0;
    float lum = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float dh = abs(h - q0.y);
    dh = min(dh, 1.0 - dh);
    float hm = 1.0 - smoothstep(q0.z, q0.z + max(q0.w, 1e-4), dh);
    float ss = max(q1.z, 1e-4);
    float ls = max(q2.z, 1e-4);
    float sm = smoothstep(q1.x - ss, q1.x, sat) * (1.0 - smoothstep(q1.y, q1.y + ss, sat));
    float lm = smoothstep(q2.x - ls, q2.x, lum) * (1.0 - smoothstep(q2.y, q2.y + ls, lum));
    float m = hm * sm * lm;
    return q1.w > 0.5 ? 1.0 - m : m;
}

float nodeMatte(int n, vec3 rgb, vec2 uv)
{
    float m = windowMatte(n, uv) * qualifierMatte(n, rgb);
    int subject = int(nodes[n * 7 + 5].w + 0.5);
    if (subject != 0 && nodeInfo.z > 0.5) {
        float person = texture(mask, uv).r;
        m *= subject == 1 ? person : 1.0 - person;
    }
    return nodes[n * 7 + 6].x > 0.5 ? 1.0 - m : m;
}

vec3 applyNodes(vec3 rgb, vec2 uv)
{
    int count = int(nodeInfo.x + 0.5);
    int highlight = int(floor(nodeInfo.w + 0.5));
    for (int n = 0; n < count; ++n) {
        float m = nodeMatte(n, rgb, uv);
        bool show = n == highlight;
        if (m <= 0.0 && !show)
            continue;
        vec4 g = nodes[n * 7];
        vec3 graded = finishGrade(curvesRow(rgb, n + 1), g.x, g.y, g.z, g.w > 0.5 ? n + 1 : -1);
        if (show)  // the selection in color, everything else mid gray
            return mix(vec3(128.0 / 255.0), graded, m);
        rgb = mix(rgb, graded, m);
    }
    return rgb;
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
    if (nodeInfo.x > 0.5)
        rgb = applyNodes(rgb, uv);
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
    if (mode == MODE_THRESHOLD) {
        // The bright parts (glow, halation): luma above fx.x, soft over 0.25.
        vec4 c = texture(tex, p / shape.xy);
        float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
        fragColor = c * smoothstep(fx.x, min(1.0, fx.x + 0.25), l);
        return;
    }
    if (mode == MODE_ADD) {
        // The picture plus a tinted, blurred glow: color0.rgb = tint * amount.
        vec2 uv = p / shape.xy;
        vec4 c = texture(tex, uv);
        fragColor = vec4(clamp(c.rgb + texture(aux, uv).rgb * color0.rgb, 0.0, 1.0), c.a);
        return;
    }
    if (mode == MODE_DENOISE) {
        // Bilateral filters on 9 x 9 whole pixels fx.z apart (editor::denoiseImage):
        // luma averaged over pixels of similar luma, color (B - Y, R - Y) over
        // pixels of similar color and luma, so edges stay sharp and colors stay put.
        const float DENOISE_GUIDE = 0.1;  // editor::kDenoiseGuideSigma
        const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);
        vec2 texel = 1.0 / shape.xy;
        vec2 uv = p / shape.xy;
        vec3 c = texture(tex, uv).rgb;
        float cy = dot(c, LUMA);
        vec2 cc = vec2(c.b - cy, c.r - cy);
        float s2 = 2.0 * fx.w * fx.w;
        float y2 = 2.0 * fx.x * fx.x;
        float c2 = 2.0 * fx.y * fx.y;
        float g2 = 2.0 * DENOISE_GUIDE * DENOISE_GUIDE;
        float sy = 0.0, ty = 0.0, sa = 0.0, tc = 0.0;
        vec2 sc = vec2(0.0);
        for (int j = -4; j <= 4; ++j) {
            for (int i = -4; i <= 4; ++i) {
                vec4 v = texture(tex, uv + vec2(float(i), float(j)) * fx.z * texel);
                float y = dot(v.rgb, LUMA);
                vec2 ch = vec2(v.b - y, v.r - y);
                float dy = y - cy;
                vec2 dc = ch - cc;
                float near = -float(i * i + j * j) * fx.z * fx.z / s2;
                float wy = exp(near - dy * dy / y2);
                float wc = exp(near - dot(dc, dc) / c2 - dy * dy / g2);
                sy += y * wy;
                sa += v.a * wy;
                ty += wy;
                sc += ch * wc;
                tc += wc;
            }
        }
        float ny = sy / ty;
        vec2 nc = sc / tc;
        float r = ny + nc.y;
        float b = ny + nc.x;
        float g = (ny - 0.2126 * r - 0.0722 * b) / 0.7152;
        float a = sa / ty;
        fragColor = vec4(clamp(vec3(r, g, b), 0.0, a), a);
        return;
    }
    if (mode == MODE_SHARPEN) {
        // Unsharp mask: picture + strength * cored(picture - blurred); detail
        // below the coring level (noise) is left alone (editor::sharpenImage).
        vec2 uv = p / shape.xy;
        vec4 c = texture(tex, uv);
        vec3 detail = c.rgb - texture(aux, uv).rgb;
        detail = sign(detail) * max(abs(detail) - fx.y, 0.0);
        fragColor = vec4(clamp(c.rgb + detail * fx.x, 0.0, c.a), c.a);
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
        c = addGrain(c, p);
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

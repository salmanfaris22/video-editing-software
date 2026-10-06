#version 440
// One quad per draw; every position is in the layer's local pixel space.

layout(location = 0) in vec2 corner;  // 0..1
layout(location = 0) out vec2 vLocal;

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
    vec4 fx;         // x: grain amount, y: grain size (px), z: grain seed; threshold/add modes: x threshold
    vec4 nodeInfo;   // x: node count, y: source aspect (w/h), z: person mask bound (0/1), w: highlighted node (-1 none)
    vec4 nodes[70];  // 7 per node (up to 10): grade, window0, window1, qual0, qual1, qual2, misc (see layer.frag)
};

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    vLocal = quad.xy + corner * quad.zw;
    gl_Position = mvp * vec4(vLocal, 0.0, 1.0);
}

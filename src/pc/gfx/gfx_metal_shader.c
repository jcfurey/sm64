// Metal shading language generation for the color combiner.
//
// Split out of gfx_metal.mm so it can be tested without Metal, UIKit or a
// device in the way: everything here is string building over a CCFeatures,
// and tools/porttest/metal_shader_test.c exercises the real function rather
// than a copy of it. gfx_metal.mm compiles the result into a pipeline.

#ifdef ENABLE_METAL

#include <stdio.h>
#include <string.h>

#include "gfx_cc.h"
#include "gfx_metal_shader.h"

static void append_str(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
}

static void append_line(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
    buf[(*len)++] = '\n';
}

// PSInput always declares the combiner inputs as float4 -- VSMain pads them
// with 1.0 when the combiner has no alpha -- so an expression building a
// float3 has to say .rgb. MSL rejects implicit vector conversions outright,
// and a shader that trips one falls back to the solid-magenta pipeline.
static const char *shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            default:
            case SHADER_0:
                return with_alpha ? "float4(0.0, 0.0, 0.0, 0.0)" : "float3(0.0, 0.0, 0.0)";
            case SHADER_INPUT_1:
                return with_alpha ? "in.input1" : "in.input1.rgb";
            case SHADER_INPUT_2:
                return with_alpha ? "in.input2" : "in.input2.rgb";
            case SHADER_INPUT_3:
                return with_alpha ? "in.input3" : "in.input3.rgb";
            case SHADER_INPUT_4:
                return with_alpha ? "in.input4" : "in.input4.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a" :
                    (with_alpha ? "float4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)" : "float3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
        }
    } else {
        switch (item) {
            default:
            case SHADER_0:
                return "0.0";
            case SHADER_INPUT_1:
                return "in.input1.a";
            case SHADER_INPUT_2:
                return "in.input2.a";
            case SHADER_INPUT_3:
                return "in.input3.a";
            case SHADER_INPUT_4:
                return "in.input4.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
        }
    }
}

static void append_formula(char *buf, size_t *len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix, bool with_alpha, bool only_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, false));
    }
}

// Generates the MSL for one combiner configuration. The vertex function
// reads the interleaved float stream directly from a device buffer by
// vertex id ("vertex pulling"), so no vertex descriptor is needed and the
// dynamic per-shader layout stays a simple stride constant.
size_t gfx_metal_generate_shader_source(char *buf, struct CCFeatures *cc, size_t *num_floats_out) {
    size_t len = 0;
    size_t num_floats = 4;
    char tmp[256];

    append_line(buf, &len, "#include <metal_stdlib>");
    append_line(buf, &len, "using namespace metal;");

    append_line(buf, &len, "struct PSInput {");
    append_line(buf, &len, "    float4 position [[position]];");
    if (cc->used_textures[0] || cc->used_textures[1]) {
        append_line(buf, &len, "    float2 uv;");
        num_floats += 2;
    }
    if (cc->opt_alpha && cc->opt_noise) {
        append_line(buf, &len, "    float4 screenPos;");
    }
    if (cc->opt_fog) {
        append_line(buf, &len, "    float4 fog;");
        num_floats += 4;
    }
    for (int i = 0; i < cc->num_inputs; i++) {
        sprintf(tmp, "    float4 input%d;", i + 1);
        append_line(buf, &len, tmp);
        num_floats += cc->opt_alpha ? 4 : 3;
    }
    append_line(buf, &len, "};");

    if (cc->opt_alpha && cc->opt_noise) {
        append_line(buf, &len, "struct FrameUniforms {");
        append_line(buf, &len, "    uint frame_count;");
        append_line(buf, &len, "    float noise_scale_x;");
        append_line(buf, &len, "    float noise_scale_y;");
        append_line(buf, &len, "};");
        append_line(buf, &len, "float random(float3 value) {");
        append_line(buf, &len, "    float r = dot(value, float3(12.9898, 78.233, 37.719));");
        append_line(buf, &len, "    return fract(sin(r) * 143758.5453);");
        append_line(buf, &len, "}");
    }

    // Vertex function

    sprintf(tmp, "#define STRIDE %d", (int) num_floats);
    append_line(buf, &len, tmp);
    append_line(buf, &len, "vertex PSInput VSMain(uint vid [[vertex_id]], device const float *verts [[buffer(0)]]) {");
    append_line(buf, &len, "    uint base = vid * STRIDE;");
    append_line(buf, &len, "    PSInput out;");
    append_line(buf, &len, "    out.position = float4(verts[base], verts[base + 1], verts[base + 2], verts[base + 3]);");
    {
        size_t off = 4;
        if (cc->used_textures[0] || cc->used_textures[1]) {
            sprintf(tmp, "    out.uv = float2(verts[base + %d], verts[base + %d]);", (int) off, (int) off + 1);
            append_line(buf, &len, tmp);
            off += 2;
        }
        if (cc->opt_alpha && cc->opt_noise) {
            append_line(buf, &len, "    out.screenPos = out.position;");
        }
        if (cc->opt_fog) {
            sprintf(tmp, "    out.fog = float4(verts[base + %d], verts[base + %d], verts[base + %d], verts[base + %d]);",
                    (int) off, (int) off + 1, (int) off + 2, (int) off + 3);
            append_line(buf, &len, tmp);
            off += 4;
        }
        for (int i = 0; i < cc->num_inputs; i++) {
            if (cc->opt_alpha) {
                sprintf(tmp, "    out.input%d = float4(verts[base + %d], verts[base + %d], verts[base + %d], verts[base + %d]);",
                        i + 1, (int) off, (int) off + 1, (int) off + 2, (int) off + 3);
                off += 4;
            } else {
                sprintf(tmp, "    out.input%d = float4(verts[base + %d], verts[base + %d], verts[base + %d], 1.0);",
                        i + 1, (int) off, (int) off + 1, (int) off + 2);
                off += 3;
            }
            append_line(buf, &len, tmp);
        }
    }
    append_line(buf, &len, "    return out;");
    append_line(buf, &len, "}");

    // Fragment function

    append_str(buf, &len, "fragment float4 PSMain(PSInput in [[stage_in]]");
    if (cc->used_textures[0]) {
        append_str(buf, &len, ", texture2d<float> tex0 [[texture(0)]], sampler smp0 [[sampler(0)]]");
    }
    if (cc->used_textures[1]) {
        append_str(buf, &len, ", texture2d<float> tex1 [[texture(1)]], sampler smp1 [[sampler(1)]]");
    }
    if (cc->opt_alpha && cc->opt_noise) {
        append_str(buf, &len, ", constant FrameUniforms &frame_uniforms [[buffer(0)]]");
    }
    append_line(buf, &len, ") {");

    if (cc->used_textures[0]) {
        append_line(buf, &len, "    float4 texVal0 = tex0.sample(smp0, in.uv);");
    }
    if (cc->used_textures[1]) {
        append_line(buf, &len, "    float4 texVal1 = tex1.sample(smp1, in.uv);");
    }

    append_str(buf, &len, cc->opt_alpha ? "    float4 texel = " : "    float3 texel = ");
    if (!cc->color_alpha_same && cc->opt_alpha) {
        append_str(buf, &len, "float4(");
        append_formula(buf, &len, cc->c, cc->do_single[0], cc->do_multiply[0], cc->do_mix[0], false, false);
        append_str(buf, &len, ", ");
        append_formula(buf, &len, cc->c, cc->do_single[1], cc->do_multiply[1], cc->do_mix[1], true, true);
        append_str(buf, &len, ")");
    } else {
        append_formula(buf, &len, cc->c, cc->do_single[0], cc->do_multiply[0], cc->do_mix[0], cc->opt_alpha, false);
    }
    append_line(buf, &len, ";");

    if (cc->opt_texture_edge && cc->opt_alpha) {
        append_line(buf, &len, "    if (texel.a > 0.3) texel.a = 1.0; else discard_fragment();");
    }

    if (cc->opt_fog) {
        if (cc->opt_alpha) {
            append_line(buf, &len, "    texel = float4(mix(texel.rgb, in.fog.rgb, in.fog.a), texel.a);");
        } else {
            append_line(buf, &len, "    texel = mix(texel, in.fog.rgb, in.fog.a);");
        }
    }

    if (cc->opt_alpha && cc->opt_noise) {
        append_line(buf, &len, "    float2 coords = (in.screenPos.xy / in.screenPos.w) * float2(frame_uniforms.noise_scale_x, frame_uniforms.noise_scale_y);");
        append_line(buf, &len, "    texel.a *= round(random(float3(floor(coords), (float) frame_uniforms.frame_count)));");
    }

    if (cc->opt_alpha) {
        append_line(buf, &len, "    return texel;");
    } else {
        append_line(buf, &len, "    return float4(texel, 1.0);");
    }
    append_line(buf, &len, "}");

    buf[len] = '\0';
    *num_floats_out = num_floats;
    return len;
}

#endif // ENABLE_METAL

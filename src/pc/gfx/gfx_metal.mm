// Metal rendering backend.
//
// Implements the same GfxRenderingAPI contract as the OpenGL and Direct3D
// backends: gfx_pc.c interprets the N64 display lists and hands this file
// batches of pre-transformed triangles plus a color-combiner configuration
// (shader_id). Metal shading language for each combiner is generated at
// runtime and compiled into a pipeline state, with alpha blending baked in.
//
// Conventions match the Direct3D 11 backend rather than the OpenGL one:
// depth ranges over [0, 1] (z_is_from_0_to_1 returns true so gfx_pc remaps
// z), and viewport/scissor rectangles arrive in a bottom-left origin
// convention and are flipped here for Metal's top-left origin.

#ifdef ENABLE_METAL

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdio.h>
#include <string.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

extern "C" {
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_metal.h"

// Provided by the window backend (gfx_sdl2.c): the CAMetalLayer of the
// window's metal view
void *gfx_sdl_get_metal_layer(void);

#ifdef TARGET_IOS
// Provided by controller_touch.c: on-screen control geometry in NDC,
// interleaved [x, y, r, g, b, a] per vertex
const float *touch_overlay_build(int width, int height, int *num_verts);
#endif

#ifdef HIGH_FPS_PC
#include "../framerate.h"
#endif

#include "../configfile.h"
}

#define MAX_FRAMES_IN_FLIGHT 3
// Bump-allocated per-frame vertex storage; a new buffer of this size is
// added whenever a frame outgrows the current one
#define VERTEX_BUFFER_SIZE (512 * 1024)

#define GAME_FRAMERATE 30.0

struct ShaderProgramMetal {
    uint32_t shader_id;
    id<MTLRenderPipelineState> pipeline;
    uint8_t num_inputs;
    uint8_t num_floats;
    bool used_textures[2];
    bool used_noise;
};

struct FrameUniforms {
    uint32_t frame_count;
    float noise_scale_x;
    float noise_scale_y;
};

static struct {
    CAMetalLayer *layer;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;

    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> command_buffer;
    id<MTLRenderCommandEncoder> encoder;

    id<MTLTexture> depth_texture;

    // depth states indexed [test][write]
    id<MTLDepthStencilState> depth_states[2][2];

    // Per-frame vertex buffer arenas
    NSMutableArray<id<MTLBuffer>> *vertex_buffers[MAX_FRAMES_IN_FLIGHT];
    int current_vertex_buffer;
    size_t current_vertex_buffer_offset;
    int frame_index;
    dispatch_semaphore_t frame_semaphore;

    struct ShaderProgramMetal shader_program_pool[64];
    uint8_t shader_program_pool_size;
    struct ShaderProgramMetal *shader_program;

    NSMutableArray *textures;   // id<MTLTexture> or NSNull
    NSMutableArray *samplers;   // id<MTLSamplerState> or NSNull
    int current_tile;
    uint32_t current_texture_ids[2];

    uint32_t render_width, render_height;

    // Current rasterizer state as requested by gfx_pc; applied lazily
    bool depth_test;
    bool depth_mask;
    bool zmode_decal;
    MTLViewport viewport;
    MTLScissorRect scissor;
    bool have_viewport;
    bool have_scissor;

    // Last state applied to the current encoder
    struct ShaderProgramMetal *last_program;
    void *last_textures[2];
    void *last_samplers[2];
    int8_t last_depth_test;
    int8_t last_depth_mask;
    int8_t last_zmode_decal;

    struct FrameUniforms frame_uniforms;

    // Touch overlay
    id<MTLRenderPipelineState> overlay_pipeline;

    // Native drawable size at init, restored when retro mode turns off
    CGSize native_drawable_size;
} mtl;

//==============================================================================
// Shader generation
//==============================================================================

static void append_str(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
}

static void append_line(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
    buf[(*len)++] = '\n';
}

static const char *shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            default:
            case SHADER_0:
                return with_alpha ? "float4(0.0, 0.0, 0.0, 0.0)" : "float3(0.0, 0.0, 0.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "in.input1" : "in.input1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "in.input2" : "in.input2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "in.input3" : "in.input3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "in.input4" : "in.input4.rgb";
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

static void append_formula(char *buf, size_t *len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix, bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}

// Generates the MSL for one combiner configuration. The vertex function
// reads the interleaved float stream directly from a device buffer by
// vertex id ("vertex pulling"), so no vertex descriptor is needed and the
// dynamic per-shader layout stays a simple stride constant.
static size_t generate_shader_source(char *buf, struct CCFeatures *cc, size_t *num_floats_out) {
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
        append_formula(buf, &len, cc->c, cc->do_single[0], cc->do_multiply[0], cc->do_mix[0], false, false, true);
        append_str(buf, &len, ", ");
        append_formula(buf, &len, cc->c, cc->do_single[1], cc->do_multiply[1], cc->do_mix[1], true, true, true);
        append_str(buf, &len, ")");
    } else {
        append_formula(buf, &len, cc->c, cc->do_single[0], cc->do_multiply[0], cc->do_mix[0], cc->opt_alpha, false, cc->opt_alpha);
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

//==============================================================================
// Pipeline and state helpers
//==============================================================================

static id<MTLRenderPipelineState> create_pipeline(id<MTLFunction> vs, id<MTLFunction> fs, bool blend) {
    MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vs;
    desc.fragmentFunction = fs;
    desc.colorAttachments[0].pixelFormat = mtl.layer.pixelFormat;
    if (blend) {
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    }
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    NSError *error = nil;
    id<MTLRenderPipelineState> pipeline = [mtl.device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (pipeline == nil) {
        fprintf(stderr, "Metal pipeline creation failed: %s\n",
                error != nil ? [[error localizedDescription] UTF8String] : "(unknown)");
        abort();
    }
    return pipeline;
}

static id<MTLLibrary> compile_library(const char *source) {
    NSError *error = nil;
    id<MTLLibrary> lib = [mtl.device newLibraryWithSource:[NSString stringWithUTF8String:source]
                                                  options:nil
                                                    error:&error];
    if (lib == nil) {
        fprintf(stderr, "Metal shader compilation failed: %s\nSource:\n%s\n",
                error != nil ? [[error localizedDescription] UTF8String] : "(unknown)", source);
        abort();
    }
    return lib;
}

static void ensure_depth_texture(void) {
    if (mtl.depth_texture != nil
        && mtl.depth_texture.width == mtl.render_width
        && mtl.depth_texture.height == mtl.render_height) {
        return;
    }
    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                           width:mtl.render_width
                                                          height:mtl.render_height
                                                       mipmapped:NO];
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget;
    mtl.depth_texture = [mtl.device newTextureWithDescriptor:desc];
}

// Allocates space for one draw's vertex data in this frame's arena and
// returns the buffer plus the byte offset the data was copied to
static id<MTLBuffer> upload_vertices(const float *data, size_t num_bytes, size_t *offset_out) {
    NSMutableArray<id<MTLBuffer>> *pool = mtl.vertex_buffers[mtl.frame_index];

    // 16-byte align each allocation
    size_t offset = (mtl.current_vertex_buffer_offset + 15) & ~(size_t) 15;
    size_t needed = num_bytes > VERTEX_BUFFER_SIZE ? num_bytes : (size_t) VERTEX_BUFFER_SIZE;

    if ((NSUInteger) mtl.current_vertex_buffer >= pool.count
        || offset + num_bytes > [pool[mtl.current_vertex_buffer] length]) {
        if ((NSUInteger) mtl.current_vertex_buffer < pool.count) {
            mtl.current_vertex_buffer++;
        }
        while ((NSUInteger) mtl.current_vertex_buffer >= pool.count) {
            [pool addObject:[mtl.device newBufferWithLength:needed options:MTLResourceStorageModeShared]];
        }
        offset = 0;
    }

    id<MTLBuffer> buffer = pool[mtl.current_vertex_buffer];
    memcpy((uint8_t *) buffer.contents + offset, data, num_bytes);
    mtl.current_vertex_buffer_offset = offset + num_bytes;
    *offset_out = offset;
    return buffer;
}

//==============================================================================
// GfxRenderingAPI implementation
//==============================================================================

static bool gfx_metal_z_is_from_0_to_1(void) {
    return true;
}

static void gfx_metal_unload_shader(struct ShaderProgram *old_prg) {
}

static void gfx_metal_load_shader(struct ShaderProgram *new_prg) {
    mtl.shader_program = (struct ShaderProgramMetal *) new_prg;
}

static struct ShaderProgram *gfx_metal_create_and_load_new_shader(uint32_t shader_id) {
    struct CCFeatures cc_features;
    gfx_cc_get_features(shader_id, &cc_features);

    static char buf[8192];
    size_t num_floats;
    generate_shader_source(buf, &cc_features, &num_floats);

    id<MTLLibrary> lib = compile_library(buf);
    id<MTLFunction> vs = [lib newFunctionWithName:@"VSMain"];
    id<MTLFunction> fs = [lib newFunctionWithName:@"PSMain"];

    struct ShaderProgramMetal *prg = &mtl.shader_program_pool[mtl.shader_program_pool_size++];
    prg->shader_id = shader_id;
    prg->pipeline = create_pipeline(vs, fs, cc_features.opt_alpha);
    prg->num_inputs = cc_features.num_inputs;
    prg->num_floats = num_floats;
    prg->used_textures[0] = cc_features.used_textures[0];
    prg->used_textures[1] = cc_features.used_textures[1];
    prg->used_noise = cc_features.opt_alpha && cc_features.opt_noise;

    mtl.shader_program = prg;
    return (struct ShaderProgram *) prg;
}

static struct ShaderProgram *gfx_metal_lookup_shader(uint32_t shader_id) {
    for (size_t i = 0; i < mtl.shader_program_pool_size; i++) {
        if (mtl.shader_program_pool[i].shader_id == shader_id) {
            return (struct ShaderProgram *) &mtl.shader_program_pool[i];
        }
    }
    return NULL;
}

static void gfx_metal_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    struct ShaderProgramMetal *p = (struct ShaderProgramMetal *) prg;
    *num_inputs = p->num_inputs;
    used_textures[0] = p->used_textures[0];
    used_textures[1] = p->used_textures[1];
}

static uint32_t gfx_metal_new_texture(void) {
    [mtl.textures addObject:[NSNull null]];
    [mtl.samplers addObject:[NSNull null]];
    return (uint32_t) ([mtl.textures count] - 1);
}

static void gfx_metal_select_texture(int tile, uint32_t texture_id) {
    mtl.current_tile = tile;
    mtl.current_texture_ids[tile] = texture_id;
}

static void gfx_metal_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    id<MTLTexture> texture = [mtl.device newTextureWithDescriptor:desc];
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
               mipmapLevel:0
                 withBytes:rgba32_buf
               bytesPerRow:(NSUInteger) width * 4];
    mtl.textures[mtl.current_texture_ids[mtl.current_tile]] = texture;
}

static MTLSamplerAddressMode gfx_cm_to_metal(uint32_t val) {
    if (val & G_TX_CLAMP) {
        return MTLSamplerAddressModeClampToEdge;
    }
    return (val & G_TX_MIRROR) ? MTLSamplerAddressModeMirrorRepeat : MTLSamplerAddressModeRepeat;
}

static void gfx_metal_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    MTLSamplerDescriptor *desc = [[MTLSamplerDescriptor alloc] init];
    desc.minFilter = linear_filter ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    desc.magFilter = linear_filter ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    desc.sAddressMode = gfx_cm_to_metal(cms);
    desc.tAddressMode = gfx_cm_to_metal(cmt);
    mtl.samplers[mtl.current_texture_ids[tile]] = [mtl.device newSamplerStateWithDescriptor:desc];
}

static void gfx_metal_set_depth_test(bool depth_test) {
    mtl.depth_test = depth_test;
}

static void gfx_metal_set_depth_mask(bool z_upd) {
    mtl.depth_mask = z_upd;
}

static void gfx_metal_set_zmode_decal(bool zmode_decal) {
    mtl.zmode_decal = zmode_decal;
}

static void apply_viewport(void) {
    if (mtl.encoder != nil && mtl.have_viewport) {
        [mtl.encoder setViewport:mtl.viewport];
    }
}

static void apply_scissor(void) {
    if (mtl.encoder != nil && mtl.have_scissor) {
        // Metal requires the scissor rect to lie within the render target
        MTLScissorRect rect = mtl.scissor;
        if (rect.x >= mtl.render_width) rect.x = mtl.render_width - 1;
        if (rect.y >= mtl.render_height) rect.y = mtl.render_height - 1;
        if (rect.x + rect.width > mtl.render_width) rect.width = mtl.render_width - rect.x;
        if (rect.y + rect.height > mtl.render_height) rect.height = mtl.render_height - rect.y;
        [mtl.encoder setScissorRect:rect];
    }
}

static void gfx_metal_set_viewport(int x, int y, int width, int height) {
    mtl.viewport.originX = x;
    mtl.viewport.originY = (double) mtl.render_height - y - height;
    mtl.viewport.width = width;
    mtl.viewport.height = height;
    mtl.viewport.znear = 0.0;
    mtl.viewport.zfar = 1.0;
    mtl.have_viewport = true;
    apply_viewport();
}

static void gfx_metal_set_scissor(int x, int y, int width, int height) {
    mtl.scissor.x = x < 0 ? 0 : x;
    long flipped_y = (long) mtl.render_height - y - height;
    mtl.scissor.y = flipped_y < 0 ? 0 : flipped_y;
    mtl.scissor.width = width < 0 ? 0 : width;
    mtl.scissor.height = height < 0 ? 0 : height;
    mtl.have_scissor = true;
    apply_scissor();
}

static void gfx_metal_set_use_alpha(bool use_alpha) {
    // Baked into the pipeline state from the shader features
}

static void gfx_metal_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (mtl.encoder == nil) {
        return;
    }

    if (mtl.last_depth_test != (int8_t) mtl.depth_test || mtl.last_depth_mask != (int8_t) mtl.depth_mask) {
        mtl.last_depth_test = mtl.depth_test;
        mtl.last_depth_mask = mtl.depth_mask;
        [mtl.encoder setDepthStencilState:mtl.depth_states[mtl.depth_test][mtl.depth_mask]];
    }

    if (mtl.last_zmode_decal != (int8_t) mtl.zmode_decal) {
        mtl.last_zmode_decal = mtl.zmode_decal;
        [mtl.encoder setDepthBias:0.0f slopeScale:(mtl.zmode_decal ? -2.0f : 0.0f) clamp:0.0f];
    }

    if (mtl.last_program != mtl.shader_program) {
        mtl.last_program = mtl.shader_program;
        [mtl.encoder setRenderPipelineState:mtl.shader_program->pipeline];
    }

    for (int i = 0; i < 2; i++) {
        if (mtl.shader_program->used_textures[i]) {
            id texture = mtl.textures[mtl.current_texture_ids[i]];
            id sampler = mtl.samplers[mtl.current_texture_ids[i]];
            if ((__bridge void *) texture != mtl.last_textures[i]) {
                mtl.last_textures[i] = (__bridge void *) texture;
                [mtl.encoder setFragmentTexture:(id<MTLTexture>) texture atIndex:i];
            }
            if ((__bridge void *) sampler != mtl.last_samplers[i]) {
                mtl.last_samplers[i] = (__bridge void *) sampler;
                [mtl.encoder setFragmentSamplerState:(id<MTLSamplerState>) sampler atIndex:i];
            }
        }
    }

    if (mtl.shader_program->used_noise) {
        [mtl.encoder setFragmentBytes:&mtl.frame_uniforms length:sizeof(mtl.frame_uniforms) atIndex:0];
    }

    size_t offset;
    id<MTLBuffer> buffer = upload_vertices(buf_vbo, buf_vbo_len * sizeof(float), &offset);
    [mtl.encoder setVertexBuffer:buffer offset:offset atIndex:0];

    [mtl.encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:buf_vbo_num_tris * 3];
}

static void gfx_metal_init(void) {
    mtl.layer = (__bridge CAMetalLayer *) gfx_sdl_get_metal_layer();
    mtl.device = MTLCreateSystemDefaultDevice();
    mtl.layer.device = mtl.device;
    mtl.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    mtl.layer.framebufferOnly = YES;

    // Two drawables keep presentation latency at its minimum; nextDrawable
    // blocking is what paces the game loop
    mtl.layer.maximumDrawableCount = 2;

    mtl.native_drawable_size = mtl.layer.drawableSize;

    mtl.queue = [mtl.device newCommandQueue];
    mtl.frame_semaphore = dispatch_semaphore_create(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        mtl.vertex_buffers[i] = [[NSMutableArray alloc] init];
    }
    mtl.textures = [[NSMutableArray alloc] init];
    mtl.samplers = [[NSMutableArray alloc] init];

    for (int test = 0; test < 2; test++) {
        for (int write = 0; write < 2; write++) {
            MTLDepthStencilDescriptor *desc = [[MTLDepthStencilDescriptor alloc] init];
            desc.depthCompareFunction = test ? MTLCompareFunctionLessEqual : MTLCompareFunctionAlways;
            desc.depthWriteEnabled = write ? YES : NO;
            mtl.depth_states[test][write] = [mtl.device newDepthStencilStateWithDescriptor:desc];
        }
    }
}

static void gfx_metal_on_resize(void) {
}

static void gfx_metal_start_frame(void) {
    // Retro mode renders at a 240-line drawable that the layer scales up,
    // approximating the N64's output resolution
    if (mtl.native_drawable_size.height > 240.0) {
        CGSize want = mtl.native_drawable_size;
        if (configRetroMode) {
            want.width = round(mtl.native_drawable_size.width * 240.0 / mtl.native_drawable_size.height);
            want.height = 240.0;
        }
        CGSize cur = mtl.layer.drawableSize;
        if (cur.width != want.width || cur.height != want.height) {
            mtl.layer.drawableSize = want;
        }
    }

    dispatch_semaphore_wait(mtl.frame_semaphore, DISPATCH_TIME_FOREVER);

    mtl.frame_index = (mtl.frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
    mtl.current_vertex_buffer = 0;
    mtl.current_vertex_buffer_offset = 0;

    mtl.frame_uniforms.frame_count++;
    if (mtl.frame_uniforms.frame_count > 150) {
        // No high values, as noise starts to look ugly
        mtl.frame_uniforms.frame_count = 0;
    }

    mtl.drawable = [mtl.layer nextDrawable];
    if (mtl.drawable == nil) {
        // No drawable (e.g. app in background): skip rendering this frame
        mtl.encoder = nil;
        mtl.command_buffer = nil;
        dispatch_semaphore_signal(mtl.frame_semaphore);
        return;
    }

    mtl.render_width = (uint32_t) mtl.drawable.texture.width;
    mtl.render_height = (uint32_t) mtl.drawable.texture.height;
    float aspect_ratio = (float) mtl.render_width / (float) mtl.render_height;
    mtl.frame_uniforms.noise_scale_x = 120 * aspect_ratio; // 120 = N64 height resolution (240) / 2
    mtl.frame_uniforms.noise_scale_y = 120;

    ensure_depth_texture();

    mtl.command_buffer = [mtl.queue commandBuffer];

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = mtl.drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    pass.depthAttachment.texture = mtl.depth_texture;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 1.0;

    mtl.encoder = [mtl.command_buffer renderCommandEncoderWithDescriptor:pass];
    [mtl.encoder setCullMode:MTLCullModeNone];
    [mtl.encoder setTriangleFillMode:(configViewMode == 1 ? MTLTriangleFillModeLines
                                                          : MTLTriangleFillModeFill)];

    // A fresh encoder has no state; re-apply what gfx_pc believes is current
    mtl.last_program = NULL;
    mtl.last_textures[0] = mtl.last_textures[1] = NULL;
    mtl.last_samplers[0] = mtl.last_samplers[1] = NULL;
    mtl.last_depth_test = -1;
    mtl.last_depth_mask = -1;
    mtl.last_zmode_decal = -1;
    apply_viewport();
    apply_scissor();
}

#ifdef TARGET_IOS
static void ensure_overlay_pipeline(void) {
    if (mtl.overlay_pipeline != nil) {
        return;
    }
    static const char overlay_src[] =
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct OverlayOut { float4 position [[position]]; float4 color; };\n"
        "vertex OverlayOut OverlayVS(uint vid [[vertex_id]], device const float *verts [[buffer(0)]]) {\n"
        "    uint base = vid * 6;\n"
        "    OverlayOut out;\n"
        "    out.position = float4(verts[base], verts[base + 1], 0.0, 1.0);\n"
        "    out.color = float4(verts[base + 2], verts[base + 3], verts[base + 4], verts[base + 5]);\n"
        "    return out;\n"
        "}\n"
        "fragment float4 OverlayPS(OverlayOut in [[stage_in]]) {\n"
        "    return in.color;\n"
        "}\n";
    id<MTLLibrary> lib = compile_library(overlay_src);
    mtl.overlay_pipeline = create_pipeline([lib newFunctionWithName:@"OverlayVS"],
                                           [lib newFunctionWithName:@"OverlayPS"], true);
}

static void draw_touch_overlay(void) {
    int num_verts;
    const float *verts = touch_overlay_build(mtl.render_width, mtl.render_height, &num_verts);
    if (num_verts == 0) {
        return;
    }

    ensure_overlay_pipeline();

    MTLViewport full_viewport = { 0.0, 0.0, (double) mtl.render_width, (double) mtl.render_height, 0.0, 1.0 };
    MTLScissorRect full_scissor = { 0, 0, mtl.render_width, mtl.render_height };
    [mtl.encoder setTriangleFillMode:MTLTriangleFillModeFill];
    [mtl.encoder setViewport:full_viewport];
    [mtl.encoder setScissorRect:full_scissor];
    [mtl.encoder setDepthStencilState:mtl.depth_states[0][0]];
    [mtl.encoder setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    [mtl.encoder setRenderPipelineState:mtl.overlay_pipeline];

    size_t offset;
    id<MTLBuffer> buffer = upload_vertices(verts, (size_t) num_verts * 6 * sizeof(float), &offset);
    [mtl.encoder setVertexBuffer:buffer offset:offset atIndex:0];
    [mtl.encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:num_verts];
}
#endif

static void gfx_metal_end_frame(void) {
    if (mtl.encoder == nil) {
        return;
    }

#ifdef TARGET_IOS
    draw_touch_overlay();
#endif

    [mtl.encoder endEncoding];
    mtl.encoder = nil;
}

static void gfx_metal_finish_render(void) {
}

void gfx_metal_present(void) {
    if (mtl.command_buffer == nil) {
        return;
    }

    // Pace presentation to the render rate (logic rate times sub-frames);
    // nextDrawable in start_frame blocks when the queue is full, throttling
    // the game loop
    double render_fps = GAME_FRAMERATE;
#ifdef HIGH_FPS_PC
    render_fps *= gRenderSubframes;
#endif
    [mtl.command_buffer presentDrawable:mtl.drawable afterMinimumDuration:1.0 / render_fps];
    [mtl.command_buffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        dispatch_semaphore_signal(mtl.frame_semaphore);
    }];
    [mtl.command_buffer commit];

    mtl.command_buffer = nil;
    mtl.drawable = nil;
}

struct GfxRenderingAPI gfx_metal_api = {
    gfx_metal_z_is_from_0_to_1,
    gfx_metal_unload_shader,
    gfx_metal_load_shader,
    gfx_metal_create_and_load_new_shader,
    gfx_metal_lookup_shader,
    gfx_metal_shader_get_info,
    gfx_metal_new_texture,
    gfx_metal_select_texture,
    gfx_metal_upload_texture,
    gfx_metal_set_sampler_parameters,
    gfx_metal_set_depth_test,
    gfx_metal_set_depth_mask,
    gfx_metal_set_zmode_decal,
    gfx_metal_set_viewport,
    gfx_metal_set_scissor,
    gfx_metal_set_use_alpha,
    gfx_metal_draw_triangles,
    gfx_metal_init,
    gfx_metal_on_resize,
    gfx_metal_start_frame,
    gfx_metal_end_frame,
    gfx_metal_finish_render
};

#endif

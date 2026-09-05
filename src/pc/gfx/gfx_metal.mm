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
#include <TargetConditionals.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <atomic>
#include "gfx_frame_slot.h"

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

extern "C" {
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_metal.h"
#include "gfx_metal_shader.h"

// Provided by the window backend (gfx_sdl2.c): the CAMetalLayer of the
// window's metal view
void *gfx_sdl_get_metal_layer(void);

#ifdef TARGET_IOS
// Provided by controller_touch.c: on-screen control geometry in NDC,
// interleaved [x, y, r, g, b, a] per vertex
const float *touch_overlay_build(int width, int height, int *num_verts);
#endif

// Also provides GAME_FRAMERATE, which is needed whether or not sub-frame
// interpolation is compiled in
#include "../framerate.h"

#include "../configfile.h"
}

#define MAX_FRAMES_IN_FLIGHT 3
// Bump-allocated per-frame vertex storage; a new buffer of this size is
// added whenever a frame outgrows the current one
#define VERTEX_BUFFER_SIZE (512 * 1024)

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

    // Completion and presentation may both release a frame after an error.
    // Only the callback whose serial still owns an arena can make it free.
    GfxFrameSlot frame_slots[MAX_FRAMES_IN_FLIGHT];
    uint64_t frame_serial; // owned by the render thread

    // Per-frame vertex buffer arenas
    NSMutableArray<id<MTLBuffer>> *vertex_buffers[MAX_FRAMES_IN_FLIGHT];
    int current_vertex_buffer;
    size_t current_vertex_buffer_offset;
    int frame_index;

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
    bool overlay_pipeline_failed;
} mtl;

static std::atomic<uint64_t> sPresentedFrameCount(0);
static std::atomic<uint32_t> sCommandBufferErrorCount(0);

uint64_t gfx_metal_presented_frame_count(void) {
    return sPresentedFrameCount.load(std::memory_order_relaxed);
}

static void release_frame_slot(int frame_index, uint64_t frame_serial) {
    mtl.frame_slots[frame_index].release(frame_serial);
}

static bool acquire_frame_slot(void) {
    uint64_t serial = mtl.frame_serial + 1;
    if (serial == 0) {
        serial = 1;
    }
    // Callbacks need not free arenas in acquisition order. A free-count
    // semaphore alone cannot tell us whether the next arena is still in use.
    for (int i = 1; i <= MAX_FRAMES_IN_FLIGHT; i++) {
        int index = (mtl.frame_index + i) % MAX_FRAMES_IN_FLIGHT;
        if (mtl.frame_slots[index].try_acquire(serial)) {
            mtl.frame_index = index;
            mtl.frame_serial = serial;
            return true;
        }
    }
    return false;
}

static_assert(sizeof(mtl.shader_program_pool) / sizeof(mtl.shader_program_pool[0])
                  >= GFX_MAX_SHADER_PROGRAMS,
              "shader program pool is smaller than gfx_pc will fill");

#ifdef TARGET_IOS
static void ensure_overlay_pipeline(void);
#endif


//==============================================================================
// Pipeline and state helpers
//==============================================================================

static void metal_fatal(const char *message) {
    fprintf(stderr, "Fatal Metal renderer error: %s\n", message);
    abort();
}

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
    }
    return lib;
}

// Builds a pipeline from generated MSL, or nil if it does not compile
static id<MTLRenderPipelineState> build_pipeline(const char *source, bool blend) {
    id<MTLLibrary> lib = compile_library(source);
    id<MTLFunction> vs, fs;

    if (lib == nil) {
        return nil;
    }
    vs = [lib newFunctionWithName:@"VSMain"];
    fs = [lib newFunctionWithName:@"PSMain"];
    if (vs == nil || fs == nil) {
        return nil;
    }
    return create_pipeline(vs, fs, blend);
}

// Draws solid magenta, reading only the vertex position. Used when a
// generated combiner shader fails to compile: the frame is wrong in an
// obvious, reportable way instead of taking the process down with it. The
// stride must still match what gfx_pc.c writes for this combiner, so it is
// baked in from the real vertex layout.
static id<MTLRenderPipelineState> build_fallback_pipeline(size_t num_floats, bool blend) {
    char source[1024];

    snprintf(source, sizeof(source),
             "#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "struct PSInput { float4 position [[position]]; };\n"
             "vertex PSInput VSMain(uint vid [[vertex_id]], device const float *verts [[buffer(0)]]) {\n"
             "    uint base = vid * %d;\n"
             "    PSInput out;\n"
             "    out.position = float4(verts[base], verts[base + 1], verts[base + 2], verts[base + 3]);\n"
             "    return out;\n"
             "}\n"
             "fragment float4 PSMain(PSInput in [[stage_in]]) {\n"
             "    return float4(1.0, 0.0, 1.0, 1.0);\n"
             "}\n",
             (int) num_floats);
    return build_pipeline(source, blend);
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
#ifdef TARGET_IOS
    // The pass clears depth on load and discards it on store, so on a
    // tile-based deferred GPU it never has to leave tile memory: memoryless
    // costs no bandwidth and no allocation at all
    desc.storageMode = MTLStorageModeMemoryless;
#else
    desc.storageMode = MTLStorageModePrivate;
#endif
    desc.usage = MTLTextureUsageRenderTarget;
    mtl.depth_texture = [mtl.device newTextureWithDescriptor:desc];
    if (mtl.depth_texture == nil) {
        metal_fatal("could not allocate the depth buffer");
    }
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
            id<MTLBuffer> new_buffer = [mtl.device newBufferWithLength:needed
                                                               options:MTLResourceStorageModeShared];
            if (new_buffer == nil) {
                metal_fatal("could not allocate a vertex buffer");
            }
            [pool addObject:new_buffer];
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
    (void) old_prg;
}

static void gfx_metal_load_shader(struct ShaderProgram *new_prg) {
    mtl.shader_program = (struct ShaderProgramMetal *) new_prg;
}

static struct ShaderProgram *gfx_metal_create_and_load_new_shader(uint32_t shader_id) {
    struct CCFeatures cc_features;
    gfx_cc_get_features(shader_id, &cc_features);

    static char buf[GFX_METAL_SHADER_BUF_SIZE];
    size_t num_floats;
    gfx_metal_generate_shader_source(buf, &cc_features, &num_floats);

    struct ShaderProgramMetal *prg = &mtl.shader_program_pool[mtl.shader_program_pool_size++];
    prg->shader_id = shader_id;
    prg->pipeline = build_pipeline(buf, cc_features.opt_alpha);
    if (prg->pipeline == nil) {
        // Keep the reported vertex layout intact either way: gfx_pc.c has
        // already decided the stride from these same combiner features
        prg->pipeline = build_fallback_pipeline(num_floats, cc_features.opt_alpha);
        if (prg->pipeline == nil) {
            metal_fatal("could not create either the generated or fallback shader pipeline");
        }
    }
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
    if (width <= 0 || height <= 0) {
        return;
    }
    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    id<MTLTexture> texture = [mtl.device newTextureWithDescriptor:desc];
    if (texture == nil) {
        metal_fatal("could not allocate a texture");
    }
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
    id<MTLSamplerState> sampler = [mtl.device newSamplerStateWithDescriptor:desc];
    if (sampler == nil) {
        metal_fatal("could not create a sampler state");
    }
    mtl.samplers[mtl.current_texture_ids[tile]] = sampler;
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
    (void) use_alpha;
    // Baked into the pipeline state from the shader features
}

static void gfx_metal_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (mtl.encoder == nil || mtl.shader_program == NULL || mtl.shader_program->pipeline == nil) {
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
            if (texture == [NSNull null] || sampler == [NSNull null]) {
                // Nothing has been uploaded for this slot yet
                continue;
            }
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
    if (mtl.layer == nil) {
        metal_fatal("SDL did not provide a CAMetalLayer");
    }
    mtl.device = MTLCreateSystemDefaultDevice();
    if (mtl.device == nil) {
        metal_fatal("this device does not provide a Metal device");
    }
    mtl.layer.device = mtl.device;
    mtl.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    mtl.layer.framebufferOnly = YES;

    // CADisplayLink paces presentation. A third drawable gives the GPU room to
    // finish the previous display frame without forcing nextDrawable to wait.
    mtl.layer.maximumDrawableCount = MAX_FRAMES_IN_FLIGHT;
    mtl.layer.allowsNextDrawableTimeout = YES;

    mtl.queue = [mtl.device newCommandQueue];
    if (mtl.queue == nil) {
        metal_fatal("could not create a command queue");
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        mtl.vertex_buffers[i] = [[NSMutableArray alloc] init];
        if (mtl.vertex_buffers[i] == nil) {
            metal_fatal("could not create a vertex-buffer pool");
        }
    }
    mtl.textures = [[NSMutableArray alloc] init];
    mtl.samplers = [[NSMutableArray alloc] init];
    if (mtl.textures == nil || mtl.samplers == nil) {
        metal_fatal("could not create the texture cache");
    }

    for (int test = 0; test < 2; test++) {
        for (int write = 0; write < 2; write++) {
            MTLDepthStencilDescriptor *desc = [[MTLDepthStencilDescriptor alloc] init];
            desc.depthCompareFunction = test ? MTLCompareFunctionLessEqual : MTLCompareFunctionAlways;
            desc.depthWriteEnabled = write ? YES : NO;
            mtl.depth_states[test][write] = [mtl.device newDepthStencilStateWithDescriptor:desc];
            if (mtl.depth_states[test][write] == nil) {
                metal_fatal("could not create a depth-stencil state");
            }
        }
    }

#ifdef TARGET_IOS
    // Avoid compiling the touch-control pipeline during the first playable
    // frame. Generated game combiners are prewarmed by gfx_pc immediately
    // after this backend initialization.
    ensure_overlay_pipeline();
#endif
}

static void gfx_metal_on_resize(void) {
}

// The layer's size in native pixels. Unlike drawableSize this is not
// affected by retro mode overriding the resolution, so it stays correct
// when the device is rotated or the window is resized.
static CGSize native_layer_size(void) {
    CGSize size = mtl.layer.bounds.size;
    CGFloat scale = mtl.layer.contentsScale;

    if (scale <= 0.0) {
        scale = 1.0;
    }
    return CGSizeMake(round(size.width * scale), round(size.height * scale));
}

static void gfx_metal_start_frame(void) {
    // Retro mode renders at a 240-line drawable that the layer scales up,
    // approximating the N64's output resolution
    CGSize native = native_layer_size();
    if (native.width >= 1.0 && native.height > 240.0) {
        CGSize want = native;
        if (configRetroMode) {
            want.width = round(native.width * 240.0 / native.height);
            want.height = 240.0;
        }
        CGSize cur = mtl.layer.drawableSize;
        if (cur.width != want.width || cur.height != want.height) {
            mtl.layer.drawableSize = want;
        }
    }

    // Never stall UIKit's display callback behind old GPU work. Skipping an
    // interpolation frame is preferable to delaying the next 30 Hz logic tick.
    if (!acquire_frame_slot()) {
        mtl.drawable = nil;
        mtl.command_buffer = nil;
        mtl.encoder = nil;
        return;
    }

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
        release_frame_slot(mtl.frame_index, mtl.frame_serial);
        return;
    }

    mtl.render_width = (uint32_t) mtl.drawable.texture.width;
    mtl.render_height = (uint32_t) mtl.drawable.texture.height;
    float aspect_ratio = (float) mtl.render_width / (float) mtl.render_height;
    mtl.frame_uniforms.noise_scale_x = 120 * aspect_ratio; // 120 = N64 height resolution (240) / 2
    mtl.frame_uniforms.noise_scale_y = 120;

    ensure_depth_texture();

    mtl.command_buffer = [mtl.queue commandBuffer];
    if (mtl.command_buffer == nil) {
        release_frame_slot(mtl.frame_index, mtl.frame_serial);
        metal_fatal("could not create a command buffer");
    }

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
    if (mtl.encoder == nil) {
        release_frame_slot(mtl.frame_index, mtl.frame_serial);
        metal_fatal("could not create a render command encoder");
    }
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
    static const char overlay_src[] =
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct PSInput { float4 position [[position]]; float4 color; };\n"
        "vertex PSInput VSMain(uint vid [[vertex_id]], device const float *verts [[buffer(0)]]) {\n"
        "    uint base = vid * 6;\n"
        "    PSInput out;\n"
        "    out.position = float4(verts[base], verts[base + 1], 0.0, 1.0);\n"
        "    out.color = float4(verts[base + 2], verts[base + 3], verts[base + 4], verts[base + 5]);\n"
        "    return out;\n"
        "}\n"
        "fragment float4 PSMain(PSInput in [[stage_in]]) {\n"
        "    return in.color;\n"
        "}\n";

    if (mtl.overlay_pipeline != nil || mtl.overlay_pipeline_failed) {
        return;
    }
    mtl.overlay_pipeline = build_pipeline(overlay_src, true);
    if (mtl.overlay_pipeline == nil) {
        // Do not retry every frame; without the overlay the game is still
        // playable with a hardware controller
        mtl.overlay_pipeline_failed = true;
    }
}

static void draw_touch_overlay(void) {
    int num_verts;
    const float *verts = touch_overlay_build(mtl.render_width, mtl.render_height, &num_verts);
    if (num_verts == 0) {
        return;
    }

    ensure_overlay_pipeline();
    if (mtl.overlay_pipeline == nil) {
        return;
    }

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

    // A completion handler can run before Core Animation releases its
    // drawable. On device, retain the in-flight slot until presentation (or
    // an error) so the next display callback cannot enter nextDrawable while
    // all layer drawables are still owned.
    const int frame_index = mtl.frame_index;
    const uint64_t frame_serial = mtl.frame_serial;

#if defined(TARGET_IOS) && !TARGET_OS_SIMULATOR
    // Count only frames Core Animation confirms reached the device screen. A
    // handler still runs for a dropped drawable, but presentedTime is zero in
    // that case. The simulator SDK does not expose this presentation feedback.
    [mtl.drawable addPresentedHandler:^(id<MTLDrawable> drawable) {
        if (drawable.presentedTime > 0.0) {
            sPresentedFrameCount.fetch_add(1, std::memory_order_relaxed);
        }
        release_frame_slot(frame_index, frame_serial);
    }];
#endif

#if defined(TARGET_IOS)
    // CADisplayLink already fires on a display boundary and the app submits
    // exactly one drawable from that callback. Present at the next opportunity;
    // do not queue future timestamps that retain the layer's drawable pool.
    [mtl.command_buffer presentDrawable:mtl.drawable];
#else
    [mtl.command_buffer presentDrawable:mtl.drawable];
#endif
    [mtl.command_buffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        if (cb.status == MTLCommandBufferStatusError) {
            uint32_t count = sCommandBufferErrorCount.fetch_add(1, std::memory_order_relaxed) + 1;
            // A handful of errors is enough for a useful device log without
            // risking the high-volume quarantine that hides later diagnostics.
            if (count <= 4) {
                const char *detail = cb.error != nil
                    ? cb.error.localizedDescription.UTF8String : "(unknown)";
                fprintf(stderr, "Metal command buffer failed: %s\n", detail);
            }
            release_frame_slot(frame_index, frame_serial);
        }
#if !defined(TARGET_IOS) || TARGET_OS_SIMULATOR
        // The simulator does not expose reliable presentation feedback, and
        // desktop layers are not driven by UIKit's display callback.
        release_frame_slot(frame_index, frame_serial);
#endif
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

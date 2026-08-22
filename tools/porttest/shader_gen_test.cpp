// Checks the shader source every rendering backend generates for the color
// combiner: Metal (MSL), OpenGL (GLSL, both desktop and ES) and Direct3D
// (HLSL, shared by the DX11 and DX12 backends).
//
// Each backend builds a shader per combiner configuration and hands the text
// to a compiler at runtime. Nothing at build time ever looks at it, so a
// configuration that produces uncompilable source is invisible until it
// renders -- and it does not announce itself when it happens. The Metal
// backend swaps in a fallback pipeline and the affected draws come out solid
// magenta; a silent per-configuration failure that only shows up as wrong
// pixels is exactly what is worth pinning here.
//
// That is not hypothetical, and the shape of the bug is why this test covers
// every backend rather than the one that broke. All three share the same
// accessor logic, which returns a bare input reference when the combiner
// carries no alpha and a .rgb-qualified one otherwise. That is correct only
// if the shader declares its inputs at the matching width -- and GLSL and
// HLSL do:
//
//     "attribute vec%d vInput%d;"      opt_alpha ? 4 : 3   (gfx_opengl)
//     "float%d input%d : INPUT%d;"     opt_alpha ? 4 : 3   (gfx_direct3d_common)
//     "    float4 input%d;"            always 4            (gfx_metal)
//
// Metal declares float4 unconditionally and pads in VSMain, so it alone
// needed the .rgb every time and did not have it. Four combiners came out as
// "float3 texel = texVal0.rgb * in.input1", which MSL rejects by spec rather
// than coercing, and Mario's face on the title screen was a magenta blob.
//
// So the invariant enforced here is a type rule rather than a spelling one,
// and it is checked against each backend's own declared widths: within one
// channel expression every reference is either the width that expression
// evaluates to, or a scalar. Component-wise operators (*, -, +, mix/lerp)
// accept a scalar beside a vector but never a float3 beside a float4. A
// backend that changes how it declares inputs without changing how it reads
// them fails here regardless of which side moved.
//
// Two contracts with no other checker come along for the ride:
//   - the vertex stride each backend reports is the one its shader actually
//     consumes -- as a STRIDE constant and buffer indices for Metal, which
//     pulls vertices by index, and as declared attribute widths for GLSL and
//     HLSL, which take real vertex attributes; and
//   - the source fits the buffer the caller is required to provide, which
//     every one of these generators writes into without bounds checking.
//
// The sweep covers the whole combiner space rather than the subset Super
// Mario 64 happens to use, on the same reasoning as gfx_pool_test.
//
// For a check against a real compiler rather than these rules:
//   make -C tools/porttest check-shaders

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "gfx_cc.h"
#include "gfx_metal_shader.h"
#include "gfx_opengl_shader.h"
#include "gfx_direct3d_common.h"

// Declared by the GLES build of the same translation unit, renamed on the
// command line so both GLSL dialects can be linked in at once.
extern "C" void gfx_opengl_generate_shader_source_gles(char *vs_buf, char *fs_buf,
                                                       size_t *vs_len_out, size_t *fs_len_out,
                                                       const struct CCFeatures *cc,
                                                       size_t *num_floats_out);

// gfx_direct3d_common_build_shader takes a char[4096]; the size is part of
// the signature rather than a named constant, so it is restated here.
#define HLSL_BUF_SIZE 4096

static int failures;
static int reported;
static const char *current_backend = "";

static void fail(const char *what, uint32_t shader_id, const char *detail) {
    failures++;
    if (reported < 10) {
        reported++;
        printf("  FAIL: [%s] %s\n        shader_id 0x%08x: %s\n",
               current_backend, what, shader_id, detail);
    } else if (reported == 10) {
        reported++;
        printf("  ... further failures not listed\n");
    }
}

//==============================================================================
// What one backend looks like
//==============================================================================

struct Shaders {
    const char *vert;       // the vertex shader text
    const char *frag;       // the fragment shader text, which holds `texel`
    size_t num_floats;      // vertex stride the backend reports
    size_t longest;         // longest single buffer written
    size_t capacity;        // what the caller is contractually required to give
};

struct Backend {
    const char *name;
    void (*generate)(const struct CCFeatures *cc, struct Shaders *out);
    // How wide the combiner inputs are declared for this configuration. This
    // is the fact the backends disagree on, so each states its own.
    int (*input_width)(const struct CCFeatures *cc);
    const char *input_ref;  // "in.input", "vInput", "input.input"
    const char *ctor3;      // "float3(" or "vec3("
    const char *ctor4;
    const char *decl3;      // "float3 texel = " or "vec3 texel = "
    const char *decl4;
    // Confirms the shader consumes exactly the stride the backend reported.
    void (*check_layout)(const struct Shaders *s, uint32_t shader_id);
};

static int width_always_four(const struct CCFeatures *cc) {
    (void) cc;
    return 4;
}

static int width_follows_alpha(const struct CCFeatures *cc) {
    return cc->opt_alpha ? 4 : 3;
}

//==============================================================================
// Width checking
//==============================================================================

struct Ref {
    char text[24];
    int width;
};

// Builds the backend's whole vocabulary of value references for this
// configuration, longest spellings first so that vInput1.rgb is never read as
// vInput1.
static size_t build_refs(const struct Backend *b, const struct CCFeatures *cc, struct Ref *refs) {
    size_t n = 0;
    int iw = b->input_width(cc);

    for (int i = 1; i <= 4; i++) {
        snprintf(refs[n].text, sizeof(refs[n].text), "%s%d.rgb", b->input_ref, i);
        refs[n++].width = 3;
        snprintf(refs[n].text, sizeof(refs[n].text), "%s%d.a", b->input_ref, i);
        refs[n++].width = 1;
        snprintf(refs[n].text, sizeof(refs[n].text), "%s%d", b->input_ref, i);
        refs[n++].width = iw;
    }
    for (int t = 0; t <= 1; t++) {
        snprintf(refs[n].text, sizeof(refs[n].text), "texVal%d.rgb", t);
        refs[n++].width = 3;
        snprintf(refs[n].text, sizeof(refs[n].text), "texVal%d.a", t);
        refs[n++].width = 1;
        snprintf(refs[n].text, sizeof(refs[n].text), "texVal%d", t);
        refs[n++].width = 4;
    }
    // Constructors are not references, but they carry a width the same way and
    // a mismatched one is the same defect.
    snprintf(refs[n].text, sizeof(refs[n].text), "%s", b->ctor4);
    refs[n++].width = 4;
    snprintf(refs[n].text, sizeof(refs[n].text), "%s", b->ctor3);
    refs[n++].width = 3;
    return n;
}

// Confirms every reference in expr is either `expect` wide or a scalar.
static bool widths_agree(const struct Ref *refs, size_t nrefs, const char *expr, size_t len,
                         int expect, char *detail, size_t detail_size) {
    for (size_t i = 0; i < len; i++) {
        for (size_t r = 0; r < nrefs; r++) {
            size_t n = strlen(refs[r].text);
            if (i + n > len || memcmp(expr + i, refs[r].text, n) != 0) {
                continue;
            }
            if (refs[r].width != expect && refs[r].width != 1) {
                snprintf(detail, detail_size, "float%d expression contains %s (float%d) -- %.*s",
                         expect, refs[r].text, refs[r].width,
                         (int) (len > 90 ? 90 : len), expr);
                return false;
            }
            i += n - 1;
            break;
        }
    }
    return true;
}

// Finds the comma separating the two arguments of the outermost constructor,
// which is how a combiner whose color and alpha differ is assembled.
static const char *top_level_comma(const char *expr, size_t len) {
    int depth = 0;
    for (size_t i = 0; i < len; i++) {
        if (expr[i] == '(') {
            depth++;
        } else if (expr[i] == ')') {
            depth--;
        } else if (expr[i] == ',' && depth == 1) {
            return expr + i;
        }
    }
    return NULL;
}

//==============================================================================
// Layout checks
//==============================================================================

// Metal pulls vertices by index out of a flat float buffer, so the stride
// lives in the source as a constant and every read is an explicit offset.
static void check_layout_stride(const struct Shaders *s, uint32_t shader_id) {
    char detail[256];

    const char *stride = strstr(s->vert, "#define STRIDE ");
    if (stride == NULL) {
        fail("no STRIDE definition in the generated source", shader_id, "");
        return;
    }
    long declared = strtol(stride + strlen("#define STRIDE "), NULL, 10);
    if ((size_t) declared != s->num_floats) {
        snprintf(detail, sizeof(detail), "STRIDE %ld but num_floats_out %zu",
                 declared, s->num_floats);
        fail("emitted stride disagrees with the reported one", shader_id, detail);
        return;
    }

    long highest = 0;
    for (const char *p = s->vert; (p = strstr(p, "verts[base")) != NULL; ) {
        p += strlen("verts[base");
        long index = (*p == ' ') ? strtol(p + strlen(" + "), NULL, 10) : 0;
        if (index > highest) {
            highest = index;
        }
    }
    if (highest != declared - 1) {
        snprintf(detail, sizeof(detail), "reads up to verts[base + %ld] with STRIDE %ld",
                 highest, declared);
        fail("vertex function does not read exactly one stride", shader_id, detail);
    }
}

// GLSL and HLSL take real vertex attributes, so the stride is the sum of the
// widths they declare. Counts every "<marker>vecN " / "<marker>floatN " in the
// region the caller delimits.
static long sum_declared_widths(const char *text, const char *end, const char *marker) {
    long total = 0;
    size_t mlen = strlen(marker);
    for (const char *p = text; (p = strstr(p, marker)) != NULL && (end == NULL || p < end); ) {
        p += mlen;
        if (*p >= '1' && *p <= '4') {
            total += *p - '0';
        }
    }
    return total;
}

static void check_layout_attributes(const struct Shaders *s, uint32_t shader_id,
                                    const char *marker, const char *params_start) {
    char detail[256];
    const char *start = s->vert;
    const char *end = NULL;

    if (params_start != NULL) {
        // HLSL declares its attributes as the VSMain parameter list, and the
        // same float4/float2 spellings appear in the body, so bound the scan.
        start = strstr(s->vert, params_start);
        if (start == NULL) {
            fail("no vertex entry point in the generated source", shader_id, "");
            return;
        }
        start += strlen(params_start);
        end = strchr(start, ')');
    }

    long total = sum_declared_widths(start, end, marker);
    if ((size_t) total != s->num_floats) {
        snprintf(detail, sizeof(detail), "attributes total %ld floats but num_floats_out is %zu",
                 total, s->num_floats);
        fail("declared attributes disagree with the reported stride", shader_id, detail);
    }
}

static void check_layout_glsl(const struct Shaders *s, uint32_t shader_id) {
    check_layout_attributes(s, shader_id, "attribute vec", NULL);
}

static void check_layout_hlsl(const struct Shaders *s, uint32_t shader_id) {
    check_layout_attributes(s, shader_id, "float", "PSInput VSMain(");
}

//==============================================================================
// Generators
//==============================================================================

static char metal_buf[GFX_METAL_SHADER_BUF_SIZE];
static char gl_vs[GFX_OPENGL_SHADER_BUF_SIZE];
static char gl_fs[GFX_OPENGL_SHADER_BUF_SIZE];
// One byte over the contract so the test's own terminator has somewhere to go
// without eating into the space the generator is entitled to.
static char hlsl_buf[HLSL_BUF_SIZE + 1];

static void gen_metal(const struct CCFeatures *cc, struct Shaders *out) {
    size_t num_floats = 0;
    size_t len = gfx_metal_generate_shader_source(metal_buf, (struct CCFeatures *) cc, &num_floats);
    out->vert = out->frag = metal_buf;
    out->num_floats = num_floats;
    out->longest = len + 1;
    out->capacity = sizeof(metal_buf);
}

static void gen_gl_common(const struct CCFeatures *cc, struct Shaders *out, bool gles) {
    size_t vs_len = 0, fs_len = 0, num_floats = 0;
    if (gles) {
        gfx_opengl_generate_shader_source_gles(gl_vs, gl_fs, &vs_len, &fs_len, cc, &num_floats);
    } else {
        gfx_opengl_generate_shader_source(gl_vs, gl_fs, &vs_len, &fs_len, cc, &num_floats);
    }
    out->vert = gl_vs;
    out->frag = gl_fs;
    out->num_floats = num_floats;
    out->longest = (vs_len > fs_len ? vs_len : fs_len) + 1;
    out->capacity = GFX_OPENGL_SHADER_BUF_SIZE;
}

static void gen_gl(const struct CCFeatures *cc, struct Shaders *out) {
    gen_gl_common(cc, out, false);
}

static void gen_gles(const struct CCFeatures *cc, struct Shaders *out) {
    gen_gl_common(cc, out, true);
}

// The two HLSL knobs that change the emitted text, swept alongside the
// combiner space rather than pinned to one setting.
static bool hlsl_root_signature;
static bool hlsl_three_point;

static void gen_hlsl(const struct CCFeatures *cc, struct Shaders *out) {
    size_t len = 0, num_floats = 0;
    gfx_direct3d_common_build_shader(hlsl_buf, len, num_floats, *cc,
                                     hlsl_root_signature, hlsl_three_point);
    // Unlike the Metal and GLSL generators this one does not terminate what it
    // writes -- gfx_direct3d11/12 hand (buf, len) straight to D3DCompile, which
    // takes an explicit length. So the budget is len rather than len + 1, and
    // the terminator the rest of this test needs goes in the spare byte.
    hlsl_buf[len] = '\0';
    out->vert = out->frag = hlsl_buf;
    out->num_floats = num_floats;
    out->longest = len;
    out->capacity = HLSL_BUF_SIZE;
}

//==============================================================================
// Dump mode, for putting the output through the real shader compilers
//==============================================================================

// Open-addressed set of source hashes, so a dump writes one file per distinct
// shader rather than one per configuration.
#define SEEN_BITS 22
#define SEEN_SIZE (1u << SEEN_BITS)
static uint64_t *seen;

// Dumping runs the sweep twice: once counting how many distinct shaders a
// backend produces, then again writing an evenly spread sample, so that a
// limit does not simply take the lowest shader ids.
enum { DUMP_OFF, DUMP_COUNTING, DUMP_WRITING };
static int dump_mode = DUMP_OFF;
static const char *dump_dir;
static long dump_stride = 1;    // write every Nth distinct shader
static long distinct_seen;      // distinct shaders met so far, this backend
static long dump_written;

static bool seen_before(const char *a, const char *b) {
    uint64_t h = 1469598103934665603ull;
    for (const char *s2 = a; s2 != NULL; s2 = (s2 == a ? b : NULL)) {
        for (const unsigned char *q = (const unsigned char *) s2; *q != '\0'; q++) {
            h = (h ^ *q) * 1099511628211ull;
        }
    }
    if (h == 0) {
        h = 1;
    }
    for (uint32_t i = (uint32_t) h & (SEEN_SIZE - 1); ; i = (i + 1) & (SEEN_SIZE - 1)) {
        if (seen[i] == 0) {
            seen[i] = h;
            return false;
        }
        if (seen[i] == h) {
            return true;
        }
    }
}

static void write_file(const char *dir, const char *backend, uint32_t shader_id,
                       const char *ext, const char *text) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/%08x.%s", dir, backend, shader_id, ext);
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fputs(text, f);
    fclose(f);
}

// Metal and HLSL put both entry points in one file; GLSL keeps them apart.
static void dump_one(const struct Backend *b, uint32_t shader_id, const struct Shaders *s) {
    if (seen_before(s->vert, s->vert == s->frag ? NULL : s->frag)) {
        return;
    }
    long index = distinct_seen++;
    if (dump_mode == DUMP_COUNTING || index % dump_stride != 0) {
        return;
    }
    if (s->vert == s->frag) {
        write_file(dump_dir, b->name, shader_id,
                   strcmp(b->name, "d3d") == 0 ? "hlsl" : "metal", s->vert);
    } else {
        write_file(dump_dir, b->name, shader_id, "vert", s->vert);
        write_file(dump_dir, b->name, shader_id, "frag", s->frag);
    }
    dump_written++;
}

//==============================================================================
// Per-configuration check
//==============================================================================

static size_t longest_source;
static uint32_t longest_source_id;

static void check_one(const struct Backend *b, uint32_t shader_id) {
    struct CCFeatures cc;
    struct Shaders s;
    struct Ref refs[32];
    char detail[256];

    gfx_cc_get_features(shader_id, &cc);
    b->generate(&cc, &s);

    if (s.longest > longest_source) {
        longest_source = s.longest;
        longest_source_id = shader_id;
    }
    if (s.longest > s.capacity) {
        snprintf(detail, sizeof(detail), "wrote %zu bytes into a %zu byte buffer",
                 s.longest, s.capacity);
        fail("generated source overruns the caller's buffer", shader_id, detail);
        return;
    }

    // --- the texel expression -------------------------------------------------
    const char *decl4 = strstr(s.frag, b->decl4);
    const char *decl3 = strstr(s.frag, b->decl3);
    if ((decl4 == NULL) == (decl3 == NULL)) {
        fail("texel is declared neither three- nor four-wide, or both", shader_id, "");
        return;
    }
    bool is4 = decl4 != NULL;
    if (is4 != cc.opt_alpha) {
        snprintf(detail, sizeof(detail), "opt_alpha=%d but texel is %d-wide",
                 (int) cc.opt_alpha, is4 ? 4 : 3);
        fail("texel width does not follow opt_alpha", shader_id, detail);
        return;
    }

    const char *expr = is4 ? decl4 + strlen(b->decl4) : decl3 + strlen(b->decl3);
    const char *end = strchr(expr, ';');
    if (end == NULL) {
        fail("texel assignment is unterminated", shader_id, "");
        return;
    }
    size_t expr_len = (size_t) (end - expr);
    size_t nrefs = build_refs(b, &cc, refs);

    // A combiner whose color and alpha differ is assembled as
    // vec4(<color>, <alpha>): the wrapper is four wide, but its first argument
    // is three and its second is a scalar, so the halves are checked apart.
    size_t ctor4_len = strlen(b->ctor4);
    if (cc.opt_alpha && !cc.color_alpha_same && strncmp(expr, b->ctor4, ctor4_len) == 0) {
        const char *comma = top_level_comma(expr, expr_len);
        if (comma == NULL) {
            fail("the color/alpha constructor has no top-level comma", shader_id, "");
            return;
        }
        const char *color = expr + ctor4_len;
        if (!widths_agree(refs, nrefs, color, (size_t) (comma - color), 3, detail, sizeof(detail))) {
            fail("color half of the texel expression mixes vector widths", shader_id, detail);
        }
        const char *alpha = comma + 2;
        size_t alpha_len = expr_len > 1 ? (size_t) (expr + expr_len - 1 - alpha) : 0;
        if (!widths_agree(refs, nrefs, alpha, alpha_len, 1, detail, sizeof(detail))) {
            fail("alpha half of the texel expression is not scalar", shader_id, detail);
        }
    } else if (!widths_agree(refs, nrefs, expr, expr_len, is4 ? 4 : 3, detail, sizeof(detail))) {
        fail("texel expression mixes vector widths", shader_id, detail);
    }

    b->check_layout(&s, shader_id);

    if (dump_mode != DUMP_OFF) {
        dump_one(b, shader_id, &s);
    }
}

//==============================================================================
// Sweep
//==============================================================================

static const uint32_t OPT_BITS[4] = {
    SHADER_OPT_ALPHA, SHADER_OPT_FOG, SHADER_OPT_TEXTURE_EDGE, SHADER_OPT_NOISE,
};
#define HALF_COUNT 4096

// Fixed halves for the passes that vary one side at a time. 0x249 puts a
// different input in every slot; 0x5ad mixes texels with inputs.
#define FIXED_COLOR 0x249
#define FIXED_ALPHA 0x5ad

static long sweep(const struct Backend *b) {
    long count = 0;
    for (int opts = 0; opts < 16; opts++) {
        uint32_t bits = 0;
        for (int i = 0; i < 4; i++) {
            if (opts & (1 << i)) {
                bits |= OPT_BITS[i];
            }
        }
        for (uint32_t half = 0; half < HALF_COUNT; half++) {
            // Mirrored, which is what makes color_alpha_same true
            check_one(b, bits | half | (half << 12));
            // Each half varied against a fixed other, so both are exercised
            // independently and color_alpha_same is false
            check_one(b, bits | half | (FIXED_ALPHA << 12));
            check_one(b, bits | FIXED_COLOR | (half << 12));
            count += 3;
        }
    }
    return count;
}

static const struct Backend BACKENDS[] = {
    { "metal",  gen_metal, width_always_four,  "in.input",
      "float3(", "float4(", "float3 texel = ", "float4 texel = ", check_layout_stride },
    { "opengl", gen_gl,    width_follows_alpha, "vInput",
      "vec3(",   "vec4(",   "vec3 texel = ",    "vec4 texel = ",   check_layout_glsl },
    { "gles",   gen_gles,  width_follows_alpha, "vInput",
      "vec3(",   "vec4(",   "vec3 texel = ",    "vec4 texel = ",   check_layout_glsl },
    { "d3d",    gen_hlsl,  width_follows_alpha, "input.input",
      "float3(", "float4(", "float3 texel = ",  "float4 texel = ", check_layout_hlsl },
};

static void reset_seen(void) {
    memset(seen, 0, SEEN_SIZE * sizeof(*seen));
    distinct_seen = 0;
}

int main(int argc, char **argv) {
    long limit = 0;

    if (argc >= 3 && strcmp(argv[1], "--dump") == 0) {
        dump_dir = argv[2];
        limit = argc >= 4 ? strtol(argv[3], NULL, 10) : 0;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [--dump <dir> [limit-per-backend]]\n", argv[0]);
        return 2;
    }

    seen = (uint64_t *) calloc(SEEN_SIZE, sizeof(*seen));
    if (seen == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    if (dump_dir != NULL) {
        for (size_t i = 0; i < sizeof(BACKENDS) / sizeof(BACKENDS[0]); i++) {
            const struct Backend *b = &BACKENDS[i];

            current_backend = b->name;
            long before = failures;

            dump_mode = DUMP_COUNTING;
            dump_stride = 1;
            reset_seen();
            sweep(b);
            long distinct = distinct_seen;
            failures = before;          // the counting pass reports nothing

            dump_mode = DUMP_WRITING;
            dump_stride = (limit > 0 && distinct > limit) ? (distinct + limit - 1) / limit : 1;
            reset_seen();
            dump_written = 0;
            sweep(b);
            failures = before;
            printf("%-6s %ld distinct, %ld written", b->name, distinct, dump_written);
            if (dump_stride > 1) {
                printf(" (every %ldth -- %ld not written)", dump_stride, distinct - dump_written);
            }
            printf("\n");
        }
        free(seen);
        return 0;
    }

    printf("combiner shader generation\n");

    long grand_total = 0;
    for (size_t i = 0; i < sizeof(BACKENDS) / sizeof(BACKENDS[0]); i++) {
        const struct Backend *b = &BACKENDS[i];
        current_backend = b->name;
        int before = failures;
        long total = 0;
        size_t longest_here = 0;

        longest_source = 0;
        if (strcmp(b->name, "d3d") == 0) {
            // Both knobs that change the emitted text, rather than one setting
            for (int variant = 0; variant < 4; variant++) {
                hlsl_root_signature = (variant & 1) != 0;
                hlsl_three_point = (variant & 2) != 0;
                total += sweep(b);
            }
        } else {
            total = sweep(b);
        }
        longest_here = longest_source;
        grand_total += total;

        printf("  %-6s %s  %7ld configurations, longest source %zu bytes\n",
               b->name, failures == before ? "ok:  " : "FAIL:", total, longest_here);
    }

    if (failures == 0) {
        printf("\n  ok:   every texel expression holds one vector width throughout\n");
        printf("  ok:   each backend's accessors match the widths it declares\n");
        printf("  ok:   reported vertex stride matches what each shader consumes\n");
        printf("  ok:   no generated source overruns its caller's buffer\n");
        printf("  ok:   %ld configurations swept across %zu backends\n",
               grand_total, sizeof(BACKENDS) / sizeof(BACKENDS[0]));
    }

    free(seen);
    printf(failures == 0 ? "\nPASS\n" : "\nFAILED (%d)\n", failures);
    return failures != 0;
}

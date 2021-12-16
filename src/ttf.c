#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "ttf.h"


/* ------------------------- */
/* Initialization Operations */
/* ------------------------- */
static TTF_bool ttf__read_file_into_buffer            (TTF* font, const char* path);
static TTF_bool ttf__extract_info_from_table_directory(TTF* font);
static TTF_bool ttf__extract_char_encoding            (TTF* font);
static TTF_bool ttf__format_is_supported              (TTF_uint16 format);
static TTF_bool ttf__alloc_mem_for_ins_processing     (TTF* font);


/* -------------------- */
/* Rendering Operations */
/* -------------------- */
#define TTF_SUBDIVIDE_SQRD_ERROR 0x1  /* 26.6 */
#define TTF_EDGES_PER_CHUNK      10
#define TTF_PIXELS_PER_SCANLINE  0x10 /* 26.6 */

typedef struct {
    TTF_F26Dot6_V2 p0;
    TTF_F26Dot6_V2 p1; /* Control point */
    TTF_F26Dot6_V2 p2;
} TTF_Curve;

typedef struct {
    TTF_F26Dot6_V2 p0;
    TTF_F26Dot6_V2 p1;
    TTF_F26Dot6    yMin;
    TTF_F26Dot6    yMax;
    TTF_F26Dot6    xMin;
    TTF_F16Dot16   invSlope; /* TODO: Should this be 26.6? */
    TTF_int8       dir;
} TTF_Edge;

typedef struct TTF_Active_Edge {
    TTF_Edge*               edge;
    TTF_F26Dot6             xIntersection;
    struct TTF_Active_Edge* next;
} TTF_Active_Edge;

typedef struct TTF_Active_Chunk {
    TTF_Active_Edge          edges[TTF_EDGES_PER_CHUNK];
    TTF_uint32               numEdges;
    struct TTF_Active_Chunk* next;
} TTF_Active_Chunk;

typedef struct {
    TTF_Active_Chunk* headChunk;
    TTF_Active_Edge*  headEdge;
    TTF_Active_Edge*  reusableEdges;
} TTF_Active_Edge_List;

static TTF_Edge*    ttf__get_glyph_edges                 (TTF* font, TTF_uint32* numEdges);
static void         ttf__convert_points_to_bitmap_space  (TTF* font, TTF_F26Dot6_V2* points);
static TTF_Curve*   ttf__convert_points_into_curves      (TTF* font, TTF_F26Dot6_V2* points, TTF_Point_Type* pointTypes, TTF_uint32* numCurves);
static TTF_Edge*    ttf__subdivide_curves_into_edges     (TTF* font, TTF_Curve* curves, TTF_uint32 numCurves, TTF_uint32* numEdges);
static void         ttf__subdivide_curve_into_edges      (TTF_F26Dot6_V2* p0, TTF_F26Dot6_V2* p1, TTF_F26Dot6_V2* p2, TTF_int8 dir, TTF_Edge* edges, TTF_uint32* numEdges);
static void         ttf__edge_init                       (TTF_Edge* edge, TTF_F26Dot6_V2* p0, TTF_F26Dot6_V2* p1, TTF_int8 dir);
static TTF_F10Dot22 ttf__get_inv_slope                   (TTF_F26Dot6_V2* p0, TTF_F26Dot6_V2* p1);
static int          ttf__compare_edges                   (const void* edge0, const void* edge1);
static TTF_F26Dot6  ttf__get_edge_scanline_x_intersection(TTF_Edge* edge, TTF_F26Dot6 scanline);

static TTF_bool         ttf__active_edge_list_init     (TTF_Active_Edge_List* list);
static void             ttf__active_edge_list_free     (TTF_Active_Edge_List* list);
static TTF_Active_Edge* ttf__get_available_active_edge (TTF_Active_Edge_List* list);
static TTF_Active_Edge* ttf__insert_active_edge_first  (TTF_Active_Edge_List* list);
static TTF_Active_Edge* ttf__insert_active_edge_after  (TTF_Active_Edge_List* list, TTF_Active_Edge* after);
static void             ttf__remove_active_edge        (TTF_Active_Edge_List* list, TTF_Active_Edge* prev, TTF_Active_Edge* remove);
static void             ttf__swap_active_edge_with_next(TTF_Active_Edge_List* list, TTF_Active_Edge* prev, TTF_Active_Edge* edge);


/* ------------------- */
/* Glyph Index Mapping */
/* ------------------- */
static TTF_uint16 ttf__get_glyph_index_format_4(TTF_uint8* subtable, TTF_uint32 cp);


/* --------------------- */
/* glyf Table Operations */
/* --------------------- */
enum {
    TTF_GLYF_ON_CURVE_POINT = 0x01,
    TTF_GLYF_X_SHORT_VECTOR = 0x02,
    TTF_GLYF_Y_SHORT_VECTOR = 0x04,
    TTF_GLYF_REPEAT_FLAG    = 0x08,
    TTF_GLYF_X_DUAL         = 0x10, /* X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR */
    TTF_GLYF_Y_DUAL         = 0x20, /* Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR */
    TTF_GLYF_OVERLAP_SIMPLE = 0x40,
    TTF_GLYF_RESERVED       = 0x80,
};

static TTF_bool  ttf__get_glyf_data_block   (TTF* font, TTF_uint8** block, TTF_uint32 glyphIdx);
static TTF_int16 ttf__get_num_glyph_contours(TTF* font);
static TTF_int32 ttf__get_num_glyph_points  (TTF* font);
static TTF_bool  ttf__extract_glyph_points  (TTF* font);
static TTF_int32 ttf__get_next_coord_off    (TTF_uint8** data, TTF_uint8 dualFlag, TTF_uint8 shortFlag, TTF_uint8 flags);


/* ---------------------------- */
/* Interpreter Stack Operations */
/* ---------------------------- */
#define ttf__stack_push_F2Dot14(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_push_F26Dot6(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_pop_F2Dot14(font)       ttf__stack_pop_int32(font)
#define ttf__stack_pop_F26Dot6(font)       ttf__stack_pop_int32(font)

static void       ttf__stack_push_uint32   (TTF* font, TTF_uint32 val);
static void       ttf__stack_push_int32    (TTF* font, TTF_int32  val);
static TTF_uint32 ttf__stack_pop_uint32    (TTF* font);
static TTF_int32  ttf__stack_pop_int32     (TTF* font);
static void       ttf__stack_clear         (TTF* font);
static TTF_uint8  ttf__get_num_vals_to_push(TTF_uint8 ins); /* For PUSHB and PUSHW */


/* ----------------------------- */
/* Instruction Stream Operations */
/* ----------------------------- */
typedef struct {
    TTF_uint8* bytes;
    TTF_uint32 off;
} TTF_Ins_Stream;

static void      ttf__ins_stream_init(TTF_Ins_Stream* stream, TTF_uint8* bytes);
static TTF_uint8 ttf__ins_stream_next(TTF_Ins_Stream* stream);
static void      ttf__ins_stream_jump(TTF_Ins_Stream* stream, TTF_int32 count);


/* --------------------- */
/* Instruction Execution */
/* --------------------- */
#define TTF_SCALAR_VERSION     35
#define TTF_NUM_PHANTOM_POINTS  4

enum {
    TTF_ABS       = 0x64,
    TTF_ADD       = 0x60,
    TTF_ALIGNRP   = 0x3C,
    TTF_AND       = 0x5A,
    TTF_CALL      = 0x2B,
    TTF_CINDEX    = 0x25,
    TTF_DELTAC1   = 0x73,
    TTF_DELTAC2   = 0x74,
    TTF_DELTAC3   = 0x75,
    TTF_DELTAP1   = 0x5D,
    TTF_DELTAP2   = 0x71,
    TTF_DELTAP3   = 0x72,
    TTF_DEPTH     = 0x24,
    TTF_DIV       = 0x62,
    TTF_DUP       = 0x20,
    TTF_EIF       = 0x59,
    TTF_ELSE      = 0x1B,
    TTF_ENDF      = 0x2D,
    TTF_EQ        = 0x54,
    TTF_FDEF      = 0x2C,
    TTF_FLOOR     = 0x66,
    TTF_GC        = 0x46,
    TTF_GC_MAX    = 0x47,
    TTF_GETINFO   = 0x88,
    TTF_GPV       = 0x0C,
    TTF_GT        = 0x52,
    TTF_GTEQ      = 0x53,
    TTF_IDEF      = 0x89,
    TTF_IF        = 0x58,
    TTF_IP        = 0x39,
    TTF_IUP       = 0x30,
    TTF_IUP_MAX   = 0x31,
    TTF_JROT      = 0x78,
    TTF_JMPR      = 0x1C,
    TTF_LOOPCALL  = 0x2A,
    TTF_LT        = 0x50,
    TTF_LTEQ      = 0x51,
    TTF_MD        = 0x49,
    TTF_MD_MAX    = 0x4A,
    TTF_MDAP      = 0x2E,
    TTF_MDAP_MAX  = 0x2F,
    TTF_MDRP      = 0xC0,
    TTF_MDRP_MAX  = 0xDF,
    TTF_MIAP      = 0x3E,
    TTF_MIAP_MAX  = 0x3F,
    TTF_MINDEX    = 0x26,
    TTF_MIRP      = 0xE0,
    TTF_MIRP_MAX  = 0xFF,
    TTF_MPPEM     = 0x4B,
    TTF_MUL       = 0x63,
    TTF_NEG       = 0x65,
    TTF_NEQ       = 0x55,
    TTF_NPUSHB    = 0x40,
    TTF_NPUSHW    = 0x41,
    TTF_OR        = 0x5B,
    TTF_POP       = 0x21,
    TTF_PUSHB     = 0xB0,
    TTF_PUSHB_MAX = 0xB7,
    TTF_PUSHW     = 0xB8,
    TTF_PUSHW_MAX = 0xBF,
    TTF_RCVT      = 0x45,
    TTF_RDTG      = 0x7D,
    TTF_ROLL      = 0x8A,
    TTF_ROUND     = 0x68,
    TTF_ROUND_MAX = 0x6B,
    TTF_RS        = 0x43,
    TTF_RTG       = 0x18,
    TTF_RTHG      = 0x19,
    TTF_RUTG      = 0x7C,
    TTF_SCANCTRL  = 0x85,
    TTF_SCANTYPE  = 0x8D,
    TTF_SCVTCI    = 0x1D,
    TTF_SDB       = 0x5E,
    TTF_SDS       = 0x5F,
    TTF_SHPIX     = 0x38,
    TTF_SLOOP     = 0x17,
    TTF_SRP0      = 0x10,
    TTF_SRP1      = 0x11,
    TTF_SRP2      = 0x12,
    TTF_SUB       = 0x61,
    TTF_SVTCA     = 0x00,
    TTF_SVTCA_MAX = 0x01,
    TTF_SWAP      = 0x23,
    TTF_SZPS      = 0x16,
    TTF_SZP0      = 0x13,
    TTF_SZP1      = 0x14,
    TTF_SZP2      = 0x15,
    TTF_WCVTF     = 0x70,
    TTF_WCVTP     = 0x44,
    TTF_WS        = 0x42,
};

enum {
    TTF_ROUND_TO_HALF_GRID  ,
    TTF_ROUND_TO_GRID       ,
    TTF_ROUND_TO_DOUBLE_GRID,
    TTF_ROUND_DOWN_TO_GRID  ,
    TTF_ROUND_UP_TO_GRID    ,
    TTF_ROUND_OFF           ,
};

static void     ttf__execute_font_program (TTF* font);
static void     ttf__execute_cv_program   (TTF* font);
static TTF_bool ttf__execute_glyph_program(TTF* font);
static void     ttf__execute_ins          (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);

static void ttf__ABS     (TTF* font);
static void ttf__ADD     (TTF* font);
static void ttf__ALIGNRP (TTF* font);
static void ttf__AND     (TTF* font);
static void ttf__CALL    (TTF* font);
static void ttf__CINDEX  (TTF* font);
static void ttf__DELTAC1 (TTF* font);
static void ttf__DELTAC2 (TTF* font);
static void ttf__DELTAC3 (TTF* font);
static void ttf__DELTAC  (TTF* font, TTF_uint8 range);
static void ttf__DELTAP1 (TTF* font);
static void ttf__DELTAP2 (TTF* font);
static void ttf__DELTAP3 (TTF* font);
static void ttf__DELTAP  (TTF* font, TTF_uint8 range);
static void ttf__DEPTH   (TTF* font);
static void ttf__DIV     (TTF* font);
static void ttf__DUP     (TTF* font);
static void ttf__EQ      (TTF* font);
static void ttf__FDEF    (TTF* font, TTF_Ins_Stream* stream);
static void ttf__FLOOR   (TTF* font);
static void ttf__GC      (TTF* font, TTF_uint8 ins);
static void ttf__GETINFO (TTF* font);
static void ttf__GPV     (TTF* font);
static void ttf__GT      (TTF* font);
static void ttf__GTEQ    (TTF* font);
static void ttf__IDEF    (TTF* font, TTF_Ins_Stream* stream);
static void ttf__IF      (TTF* font, TTF_Ins_Stream* stream);
static void ttf__IP      (TTF* font);
static void ttf__IUP     (TTF* font, TTF_uint8 ins);
static void ttf__JROT    (TTF* font, TTF_Ins_Stream* stream);
static void ttf__JMPR    (TTF* font, TTF_Ins_Stream* stream);
static void ttf__LOOPCALL(TTF* font);
static void ttf__LT      (TTF* font);
static void ttf__LTEQ    (TTF* font);
static void ttf__MD      (TTF* font, TTF_uint8 ins);
static void ttf__MDAP    (TTF* font, TTF_uint8 ins);
static void ttf__MDRP    (TTF* font, TTF_uint8 ins);
static void ttf__MIAP    (TTF* font, TTF_uint8 ins);
static void ttf__MINDEX  (TTF* font);
static void ttf__MIRP    (TTF* font, TTF_uint8 ins);
static void ttf__MPPEM   (TTF* font);
static void ttf__MUL     (TTF* font);
static void ttf__NEG     (TTF* font);
static void ttf__NEQ     (TTF* font);
static void ttf__NPUSHB  (TTF* font, TTF_Ins_Stream* stream);
static void ttf__NPUSHW  (TTF* font, TTF_Ins_Stream* stream);
static void ttf__OR      (TTF* font);
static void ttf__POP     (TTF* font);
static void ttf__PUSHB   (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void ttf__PUSHW   (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void ttf__RCVT    (TTF* font);
static void ttf__RDTG    (TTF* font);
static void ttf__ROLL    (TTF* font);
static void ttf__ROUND   (TTF* font, TTF_uint8 ins);
static void ttf__RS      (TTF* font);
static void ttf__RTG     (TTF* font);
static void ttf__RTHG    (TTF* font);
static void ttf__RUTG    (TTF* font);
static void ttf__SCANCTRL(TTF* font);
static void ttf__SCANTYPE(TTF* font);
static void ttf__SCVTCI  (TTF* font);
static void ttf__SDB     (TTF* font);
static void ttf__SDS     (TTF* font);
static void ttf__SHPIX   (TTF* font);
static void ttf__SLOOP   (TTF* font);
static void ttf__SRP0    (TTF* font);
static void ttf__SRP1    (TTF* font);
static void ttf__SRP2    (TTF* font);
static void ttf__SUB     (TTF* font);
static void ttf__SVTCA   (TTF* font, TTF_uint8 ins);
static void ttf__SWAP    (TTF* font);
static void ttf__SZPS    (TTF* font);
static void ttf__SZP0    (TTF* font);
static void ttf__SZP1    (TTF* font);
static void ttf__SZP2    (TTF* font);
static void ttf__WCVTF   (TTF* font);
static void ttf__WCVTP   (TTF* font);
static void ttf__WS      (TTF* font);

static void        ttf__initialize_graphics_state(TTF* font);
static void        ttf__call_func                (TTF* font, TTF_uint32 funcId, TTF_uint32 times);
static TTF_uint8   ttf__jump_to_else_or_eif      (TTF_Ins_Stream* stream);
static TTF_F26Dot6 ttf__round                    (TTF* font, TTF_F26Dot6 val);
static void        ttf__move_point               (TTF* font, TTF_Zone* zone, TTF_uint32 idx, TTF_F26Dot6 amount);
static TTF_F26Dot6 ttf__apply_single_width_cut_in(TTF* font, TTF_F26Dot6 value);
static TTF_F26Dot6 ttf__apply_min_dist           (TTF* font, TTF_F26Dot6 value);
static TTF_bool    ttf__get_delta_value          (TTF* font, TTF_uint32 exc, TTF_uint8 range, TTF_F26Dot6* deltaVal);
static void        ttf__IUP_interpolate_or_shift (TTF_Zone* zone1, TTF_Touch_Flag touchFlag, TTF_uint16 startPointIdx, TTF_uint16 endPointIdx, TTF_uint16 touch0, TTF_uint16 touch1);


/* ------------------ */
/* Utility Operations */
/* ------------------ */
#define ttf__get_Offset16(data)       ttf__get_uint16(data)
#define ttf__get_Offset32(data)       ttf__get_uint32(data)
#define ttf__get_Version16Dot16(data) ttf__get_uint32(data)

static TTF_uint16 ttf__get_uint16                 (TTF_uint8* data);
static TTF_uint32 ttf__get_uint32                 (TTF_uint8* data);
static TTF_int16  ttf__get_int16                  (TTF_uint8* data);
static void       ttf__max_min                    (TTF_int32 a, TTF_int32 b, TTF_int32* max, TTF_int32* min);
static TTF_int32  ttf__min                        (TTF_int32 a, TTF_int32 b);
static TTF_int32  ttf__max                        (TTF_int32 a, TTF_int32 b);
static TTF_uint16 ttf__get_upem                   (TTF* font);
static TTF_uint16 ttf__get_glyph_x_advance        (TTF* font, TTF_uint32 glyphIdx);
static TTF_int16  ttf__get_glyph_left_side_bearing(TTF* font, TTF_uint32 glyphIdx);
static void       ttf__get_glyph_vmetrics         (TTF* font, TTF_uint8* glyfBlock, TTF_int32* ah, TTF_int32* tsb);
static TTF_uint16 ttf__get_cvt_cap                (TTF* font);
static TTF_uint16 ttf__get_storage_area_cap       (TTF* font);
static TTF_Zone*  ttf__get_zone_pointer           (TTF* font, TTF_uint32 zone);


/* ---------------- */
/* Fixed-point Math */
/* ---------------- */

/* The proof:
      round(x/y) = floor(x/y + 0.5) = floor((x + y/2)/y) = shift-of-n(x + 2^(n-1))
      https://en.wikipedia.org/wiki/Fixed-point_arithmetic */
#define TTF_ROUNDED_DIV_POW2(a, shift) \
    (((a) + (1l << ((shift) - 1))) >> (shift))

/* The result has a scale factor of 1 << (shift(a) + shift(b) - shift) */
#define TTF_FIX_MUL(a, b, shift)\
    TTF_ROUNDED_DIV_POW2((TTF_uint64)(a) * (TTF_uint64)(b), shift)

static TTF_int64   ttf__rounded_div   (TTF_int64 a, TTF_int64 b);
static TTF_int32   ttf__fix_div       (TTF_int32 a, TTF_int32 b, TTF_uint8 shift);
static void        ttf__fix_v2_add    (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result);
static void        ttf__fix_v2_sub    (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result);
static void        ttf__fix_v2_mul    (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result, TTF_uint8 shift);
static void        ttf__fix_v2_div    (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result, TTF_uint8 shift);
static TTF_int32   ttf__fix_v2_dot    (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_uint8 shift);
static TTF_int32   ttf__fix_v2_sub_dot(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* c, TTF_uint8 shift);
static void        ttf__fix_v2_scale  (TTF_Fix_V2* v, TTF_int32 scale, TTF_uint8 shift);
static TTF_F26Dot6 ttf__f26dot6_round (TTF_F26Dot6 val);
static TTF_F26Dot6 ttf__f26dot6_ceil  (TTF_F26Dot6 val);
static TTF_F26Dot6 ttf__f26dot6_floor (TTF_F26Dot6 val);


#define TTF_DEBUG

#define TTF_LOG_LEVEL_VERBOSE  0
#define TTF_LOG_LEVEL_MINIMAL  1
#define TTF_LOG_LEVEL_CRITICAL 2
#define TTF_LOG_LEVEL_NONE     3
#define TTF_LOG_LEVEL          TTF_LOG_LEVEL_VERBOSE

#ifdef TTF_DEBUG
    static int count = 0;

    #define TTF_LOG_UNKNOWN_INS(ins)\
        if (TTF_LOG_LEVEL_CRITICAL >= TTF_LOG_LEVEL) printf("Unknown instruction: %#X\n", ins)

    #define TTF_LOG_PROGRAM(program)\
        if (TTF_LOG_LEVEL_MINIMAL >= TTF_LOG_LEVEL) printf("\n--- %s ---\n", program)

    #define TTF_LOG_INS(level)\
        if (level >= TTF_LOG_LEVEL) printf("%d) %s\n", count++, __func__ + 5)

    #define TTF_LOG_POINT(point, level)\
        if (level >= TTF_LOG_LEVEL) printf("\t(%d, %d)\n", (point).x, (point).y)

    #define TTF_LOG_VALUE(value, level)\
        if (level >= TTF_LOG_LEVEL) printf("\t%d\n", value)

    #define TTF_LOG_CUSTOM_F(level, format, ...)\
        if (level >= TTF_LOG_LEVEL) printf("\t"format"\n", __VA_ARGS__)

    #define TTF_FIX_TO_FLOAT(val, shift) ttf__fix_to_float(val, shift)
    
    float ttf__fix_to_float(TTF_int32 val, TTF_int32 shift) {
        float value = val >> shift;
        float power = 0.5f;
        TTF_int32 mask = 1 << (shift - 1);
        for (TTF_uint32 i = 0; i < shift; i++) {
            if (val & mask) {
                value += power;
            }
            mask >>= 1;
            power /= 2.0f;
        }
        return value;
    }
#else
    #define TTF_LOG_UNKNOWN_INS(ins)
    #define TTF_LOG_PROGRAM(program)
    #define TTF_LOG_INS(level)
    #define TTF_LOG_POINT(point, level)
    #define TTF_LOG_VALUE(value, level)
    #define TTF_FIX_TO_FLOAT(val, shift)
#endif


TTF_bool ttf_init(TTF* font, const char* path) {
    memset(font, 0, sizeof(TTF));

    if (!ttf__read_file_into_buffer(font, path)) {
        goto init_failure;
    }

    // sfntVersion is 0x00010000 for fonts that contain TrueType outlines
    if (ttf__get_uint32(font->data) != 0x00010000) {
        goto init_failure;
    }

    if (!ttf__extract_info_from_table_directory(font)) {
        goto init_failure;
    }

    if (!ttf__extract_char_encoding(font)) {
        goto init_failure;
    }

    if (font->OS2.exists) {
        font->ascender  = ttf__get_int16(font->data + font->OS2.off + 68);
        font->descender = ttf__get_int16(font->data + font->OS2.off + 70);
    }
    else {
        // TODO: Get defaultAscender and defaultDescender from hhea
        assert(0);
    }

    if (font->hasHinting) {
        if (!ttf__alloc_mem_for_ins_processing(font)) {
            goto init_failure;
        }

        ttf__execute_font_program(font);
    }

    return TTF_TRUE;

init_failure:
    ttf_free(font);
    return TTF_FALSE;
}

TTF_bool ttf_instance_init(TTF* font, TTF_Instance* instance, TTF_uint32 ppem) {
    // Scale is 10.22 since upem already has a scale factor of 1
    instance->scale       = ttf__rounded_div((TTF_int64)ppem << 22, ttf__get_upem(font));
    instance->ppem        = ppem;
    instance->isRotated   = TTF_FALSE;
    instance->isStretched = TTF_FALSE;

    if (font->hasHinting) {
        {
            instance->zone0.cap = 
                TTF_NUM_PHANTOM_POINTS + ttf__get_uint16(font->data + font->maxp.off + 16);

            size_t ptsSize   = instance->zone0.cap * sizeof(TTF_F26Dot6_V2);
            size_t touchSize = instance->zone0.cap * sizeof(TTF_Touch_Flag);
            size_t off       = 0;

            instance->zone0.mem = malloc(2 * ptsSize + touchSize);
            if (instance->zone0.mem == NULL) {
                return TTF_FALSE;
            }

            instance->zone0.cur        = (TTF_F26Dot6_V2*)(instance->zone0.mem);
            instance->zone0.orgScaled  = (TTF_F26Dot6_V2*)(instance->zone0.mem + (off += ptsSize));
            instance->zone0.touchFlags = (TTF_Touch_Flag*)(instance->zone0.mem + (off += ptsSize));
            instance->zone0.org        = NULL;
            instance->zone0.pointTypes = NULL;
        }

        {
            size_t cvtSize   = ttf__get_cvt_cap(font)          * sizeof(TTF_F26Dot6);
            size_t storeSize = ttf__get_storage_area_cap(font) * sizeof(TTF_int32);

            instance->mem = malloc(storeSize + cvtSize);
            if (instance->mem == NULL) {
                return TTF_FALSE;
            }

            instance->cvt         = (TTF_F26Dot6*)(instance->mem);
            instance->storageArea = (TTF_int32*)  (instance->mem + cvtSize);
        }
        
        {
            // Convert default CVT values, given in FUnits, to 26.6 fixed point
            // pixel units
            TTF_uint32 idx = 0;
            TTF_uint8* cvt = font->data + font->cvt.off;

            for (TTF_uint32 off = 0; off < font->cvt.size; off += 2) {
                TTF_int32 funits = ttf__get_int16(cvt + off);
                instance->cvt[idx++] = TTF_FIX_MUL(funits << 6, instance->scale, 22);
            }
        }

        font->cur.instance = instance;
        ttf__execute_cv_program(font);
    }
    else {
        instance->cvt = NULL;
    }

    return TTF_TRUE;
}

TTF_bool ttf_image_init(TTF_Image* image, TTF_uint8* pixels, TTF_uint32 w, TTF_uint32 h) {
    if (pixels == NULL) {
        pixels = calloc(w * h, 1);
        if (pixels == NULL) {
            return TTF_FALSE;
        }
    }

    image->pixels = pixels;
    image->w      = w;
    image->h      = h;
    return TTF_TRUE;
}

void ttf_glyph_init(TTF* font, TTF_Glyph* glyph, TTF_uint32 glyphIdx) {
    glyph->idx         = glyphIdx;
    glyph->bitmapPos.x = 0;
    glyph->bitmapPos.y = 0;
    glyph->offset.x    = 0;
    glyph->offset.y    = 0;
    glyph->size.x      = 0;
    glyph->size.y      = 0;
    glyph->xAdvance    = ttf__get_glyph_x_advance(font, glyphIdx);
}

void ttf_free(TTF* font) {
    if (font) {
        free(font->data);
        free(font->insMem);
    }
}

void ttf_free_instance(TTF* font, TTF_Instance* instance) {
    if (instance) {
        free(instance->zone0.mem);
        free(instance->mem);
    }
}

void ttf_free_image(TTF_Image* image) {
    if (image) {
        free(image->pixels);
    }
}

TTF_uint32 ttf_get_glyph_index(TTF* font, TTF_uint32 cp) {
    TTF_uint8* subtable = font->data + font->cmap.off + font->encoding.off;

    switch (ttf__get_uint16(subtable)) {
        case 4:
            return ttf__get_glyph_index_format_4(subtable, cp);
        case 6:
            // TODO
            return 0;
        case 8:
            // TODO
            return 0;
        case 10:
            // TODO
            return 0;
        case 12:
            // TODO
            return 0;
        case 13:
            // TODO
            return 0;
        case 14:
            // TODO
            return 0;
    }

    assert(0);
    return 0;
}

TTF_uint16 ttf__get_num_glyphs(TTF* font) {
    return ttf__get_uint16(font->data + font->maxp.off + 4);
}

TTF_int32 ttf_get_ascender(TTF* font, TTF_Instance* instance) {
    return ttf__f26dot6_ceil(TTF_FIX_MUL(font->ascender << 6, instance->scale, 22)) >> 6;
}

TTF_int32 ttf_get_advance_width(TTF_Instance* instance, TTF_Glyph* glyph) {
    return ttf__f26dot6_round(TTF_FIX_MUL(glyph->xAdvance << 6, instance->scale, 22)) >> 6;
}

TTF_bool ttf_render_glyph(TTF* font, TTF_Image* image, TTF_uint32 cp) {
    // TODO
    assert(0);
    return TTF_FALSE;
}

TTF_bool ttf_render_glyph_to_existing_image(TTF* font, TTF_Instance* instance, TTF_Image* image, TTF_Glyph* glyph, TTF_uint32 x, TTF_uint32 y) {
    if (!ttf__get_glyf_data_block(font, &font->cur.glyfBlock, glyph->idx)) {
        // The glyph doesn't have any outlines (' ' for example)
        return TTF_TRUE;
    }

    font->cur.instance    = instance;
    font->cur.glyph       = glyph;
    
    // TODO: handle composite glyphs
    font->cur.numContours = ttf__get_num_glyph_contours(font);
    assert(font->cur.numContours > 0);

    font->cur.numPoints   = ttf__get_num_glyph_points(font);


    TTF_uint32 numEdges;
    TTF_Edge* edges = ttf__get_glyph_edges(font, &numEdges);
    if (edges == NULL) {
        return TTF_FALSE;
    }

    // Sort edges from topmost to bottom most (smallest to largest y)
    qsort(edges, numEdges, sizeof(TTF_Edge), ttf__compare_edges);


    TTF_Active_Edge_List activeEdgeList;
    if (!ttf__active_edge_list_init(&activeEdgeList)) {
        free(edges);
        return TTF_FALSE;
    }
    
    
    TTF_F26Dot6* pixelRow = calloc(glyph->size.x, sizeof(TTF_F26Dot6));
    if (pixelRow == NULL) {
        ttf__active_edge_list_free(&activeEdgeList);
        free(edges);
        return TTF_FALSE;
    }
    

    TTF_F26Dot6 yRel    = 0;
    TTF_F26Dot6 yAbs    = y << 6;
    TTF_F26Dot6 yEndAbs = ttf__min(glyph->size.y + y, image->h - 1) << 6;
    TTF_uint32  edgeIdx = 0;
    
    while (yAbs <= yEndAbs) {
        {
            // If an edge is no longer active, remove it from the list, else
            // update its x-intersection with the current scanline

            TTF_Active_Edge* activeEdge     = activeEdgeList.headEdge;
            TTF_Active_Edge* prevActiveEdge = NULL;

            while (activeEdge != NULL) {
                TTF_Active_Edge* next = activeEdge->next;
                
                if (activeEdge->edge->yMax <= yRel) {
                    ttf__remove_active_edge(&activeEdgeList, prevActiveEdge, activeEdge);
                }
                else {
                    activeEdge->xIntersection = 
                        ttf__get_edge_scanline_x_intersection(activeEdge->edge, yRel);
                    
                    prevActiveEdge = activeEdge;
                }
                
                activeEdge = next;
            }
        }

        {
            // Make sure that the active edges are still sorted from smallest 
            // to largest x-intersection

            TTF_Active_Edge* activeEdge     = activeEdgeList.headEdge;
            TTF_Active_Edge* prevActiveEdge = NULL;
            
            while (activeEdge != NULL) {
                TTF_Active_Edge* current = activeEdge;

                while (TTF_TRUE) {
                    if (current->next != NULL) {
                        if (current->xIntersection > current->next->xIntersection) {
                            ttf__swap_active_edge_with_next(
                                &activeEdgeList, prevActiveEdge, current);

                            continue;
                        }
                    }
                    break;
                }

                prevActiveEdge = activeEdge;
                activeEdge     = activeEdge->next;
            }
        }


        // Find any edges that intersect the current scanline and insert them
        // into the active edge list
        while (edgeIdx < numEdges) {
            if (edges[edgeIdx].yMin > yRel) {
                // All further edges start below the scanline
                break;
            }
            
            if (edges[edgeIdx].yMax > yRel) {
                TTF_F26Dot6 xIntersection = 
                    ttf__get_edge_scanline_x_intersection(edges + edgeIdx, yRel);
                
                TTF_Active_Edge* activeEdge     = activeEdgeList.headEdge;
                TTF_Active_Edge* prevActiveEdge = NULL;
                
                while (activeEdge != NULL) {
                    if (xIntersection < activeEdge->xIntersection) {
                        break;
                    }
                    else if (xIntersection == activeEdge->xIntersection) {
                        if (edges[edgeIdx].xMin < activeEdge->edge->xMin) {
                            break;
                        }
                    }
                    prevActiveEdge = activeEdge;
                    activeEdge     = activeEdge->next;
                }
                
                TTF_Active_Edge* newActiveEdge = 
                    prevActiveEdge == NULL ?
                    ttf__insert_active_edge_first(&activeEdgeList) :
                    ttf__insert_active_edge_after(&activeEdgeList, prevActiveEdge);
                
                if (newActiveEdge == NULL) {
                    ttf__active_edge_list_free(&activeEdgeList);
                    free(edges);
                    free(pixelRow);
                    return TTF_FALSE;
                }
                
                newActiveEdge->edge          = edges + edgeIdx;
                newActiveEdge->xIntersection = xIntersection;
            }
            
            edgeIdx++;
        }


        if (activeEdgeList.headEdge != NULL) {
            // Set the opacity of the pixels along the scanline

            TTF_Active_Edge* activeEdge    = activeEdgeList.headEdge;
            TTF_int32        windingNumber = 0;
            TTF_F26Dot6      weightedAlpha = TTF_FIX_MUL(0x3FC0, TTF_PIXELS_PER_SCANLINE, 6);
            TTF_F26Dot6      xRel          = ttf__f26dot6_ceil(activeEdge->xIntersection);

            while (TTF_TRUE) {
                {
                    TTF_uint32 xIdx = xRel == 0 ? 0 : (xRel >> 6) - 1;
                    assert(xIdx < glyph->size.x);

                    TTF_F26Dot6 coverage =
                        windingNumber == 0 ?
                        xRel - activeEdge->xIntersection :
                        activeEdge->xIntersection - xRel + 0x40;

                    pixelRow[xIdx] += TTF_FIX_MUL(weightedAlpha, coverage, 6);
                }

            set_next_active_edge:
                {
                    if (activeEdge->next == NULL) {
                        break;
                    }

                    TTF_bool repeat = 
                        xRel >= activeEdge->next->xIntersection ||
                        activeEdge->next->xIntersection == activeEdge->xIntersection;

                    windingNumber += activeEdge->edge->dir;
                    activeEdge     = activeEdge->next;

                    if (repeat) {
                        goto set_next_active_edge;
                    }
                }

                xRel += 0x40;

                if (xRel < activeEdge->xIntersection) {
                    if (windingNumber == 0) {
                        xRel = ttf__f26dot6_ceil(activeEdge->xIntersection);
                    }
                    else {
                        do {
                            TTF_uint32 xIdx = (xRel >> 6) - 1;
                            assert(xIdx < glyph->size.x);

                            pixelRow[xIdx] += weightedAlpha;
                            xRel           += 0x40;
                        } while (xRel < activeEdge->xIntersection);
                    }
                }
            }
        }

        yAbs += TTF_PIXELS_PER_SCANLINE;
        yRel += TTF_PIXELS_PER_SCANLINE;
        
        if ((yRel & 0x3F) == 0) {
            // A new pixel has been reached, reset the pixel row.
            TTF_uint32 rowOff = ((yAbs - 0x40) >> 6) * image->w;
            
            for (TTF_uint32 i = 0; i < glyph->size.x; i++) {
                assert(pixelRow[i] >> 6 <= 255);
                image->pixels[rowOff + x + i] = pixelRow[i] >> 6;
            }
            
            memset(pixelRow, 0, glyph->size.x * sizeof(TTF_F26Dot6));
        }
    }

    
    ttf__active_edge_list_free(&activeEdgeList);
    free(edges);
    free(pixelRow);
    return TTF_TRUE;
}


/* ------------------------- */
/* Initialization Operations */
/* ------------------------- */
static TTF_bool ttf__read_file_into_buffer(TTF* font, const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return TTF_FALSE;
    }
    
    fseek(f, 0, SEEK_END);
    font->size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    font->data = calloc(font->size, 1);
    if (font->data == NULL) {
        fclose(f);
        return TTF_FALSE;
    }

    fread(font->data, 1, font->size, f);
    fclose(f);

    return TTF_TRUE;
}

static TTF_bool ttf__extract_info_from_table_directory(TTF* font) {
    TTF_uint16 numTables = ttf__get_uint16(font->data + 4);

    for (TTF_uint16 i = 0; i < numTables; i++) {
        TTF_uint8* record = font->data + (12 + 16 * i);
        TTF_Table* table  = NULL;

        TTF_uint8 tag[4];
        memcpy(tag, record, 4);

        if (!font->cmap.exists && !memcmp(tag, "cmap", 4)) {
            table = &font->cmap;
        }
        else if (!font->cvt.exists && !memcmp(tag, "cvt ", 4)) {
            table = &font->cvt;
        }
        else if (!font->fpgm.exists && !memcmp(tag, "fpgm", 4)) {
            table = &font->fpgm;
        }
        else if (!font->glyf.exists && !memcmp(tag, "glyf", 4)) {
            table = &font->glyf;
        }
        else if (!font->head.exists && !memcmp(tag, "head", 4)) {
            table = &font->head;
        }
        else if (!font->hhea.exists && !memcmp(tag, "hhea", 4)) {
            table = &font->hhea;
        }
        else if (!font->hmtx.exists && !memcmp(tag, "hmtx", 4)) {
            table = &font->hmtx;
        }
        else if (!font->loca.exists && !memcmp(tag, "loca", 4)) {
            table = &font->loca;
        }
        else if (!font->maxp.exists && !memcmp(tag, "maxp", 4)) {
            table = &font->maxp;
        }
        else if (!font->OS2.exists && !memcmp(tag, "OS/2", 4)) {
            table = &font->OS2;
        }
        else if (!font->prep.exists && !memcmp(tag, "prep", 4)) {
            table = &font->prep;
        }
        else if (!font->vmtx.exists && !memcmp(tag, "vmtx", 4)) {
            table = &font->vmtx;
        }

        if (table) {
            table->exists = TTF_TRUE;
            table->off    = ttf__get_Offset32(record + 8);
            table->size   = ttf__get_uint32(record + 12);
        }
    }

    font->hasHinting = font->cvt.exists && font->fpgm.exists && font->prep.exists;

    return 
        font->cmap.exists && 
        font->glyf.exists && 
        font->head.exists && 
        font->maxp.exists && 
        font->loca.exists &&
        font->hhea.exists &&
        font->hmtx.exists;
}

static TTF_bool ttf__extract_char_encoding(TTF* font) {
    TTF_uint16 numTables = ttf__get_uint16(font->data + font->cmap.off + 2);
    
    for (TTF_uint16 i = 0; i < numTables; i++) {
        TTF_uint8* data = font->data + font->cmap.off + 4 + i * 8;

        TTF_uint16 platformID = ttf__get_uint16(data);
        TTF_uint16 encodingID = ttf__get_uint16(data + 2);
        TTF_bool   foundValid = TTF_FALSE;

        switch (platformID) {
            case 0:
                foundValid = encodingID >= 3 && encodingID <= 6;
                break;
            case 3:
                foundValid = encodingID == 1 || encodingID == 10;
                break;
        }

        if (foundValid) {
            font->encoding.platformID = platformID;
            font->encoding.encodingID = encodingID;
            font->encoding.off        = ttf__get_Offset32(data + 4);

            TTF_uint8* subtable = font->data + font->cmap.off + font->encoding.off;
            TTF_uint16 format   = ttf__get_uint16(subtable);

            if (ttf__format_is_supported(format)) {
                return TTF_TRUE;
            }
        }
    }

    return TTF_FALSE;
}

static TTF_bool ttf__format_is_supported(TTF_uint16 format) {
    switch (format) {
        case 4:
        case 6:
        case 8:
        case 10:
        case 12:
        case 13:
        case 14:
            return TTF_TRUE;
    }
    return TTF_FALSE;
}

static TTF_bool ttf__alloc_mem_for_ins_processing(TTF* font) {
    font->stack.cap     = ttf__get_uint16(font->data + font->maxp.off + 24);
    font->funcArray.cap = ttf__get_uint16(font->data + font->maxp.off + 20);

    size_t stackSize = sizeof(TTF_Stack_Frame) * font->stack.cap;
    size_t funcsSize = sizeof(TTF_Func)        * font->funcArray.cap;

    font->insMem = calloc(stackSize + funcsSize, 1);
    if (font->insMem == NULL) {
        return TTF_FALSE;
    }

    font->stack.frames    = (TTF_Stack_Frame*)(font->insMem);
    font->funcArray.funcs = (TTF_Func*)       (font->insMem + stackSize);
    return TTF_TRUE;
}


/* ------------------- */
/* Glyph Index Mapping */
/* ------------------- */
static TTF_uint16 ttf__get_glyph_index_format_4(TTF_uint8* subtable, TTF_uint32 cp) {
    #define TTF_GET_END_CODE(index) ttf__get_uint16(subtable + 14 + 2 * (index))
    
    TTF_uint16 segCount = ttf__get_uint16(subtable + 6) >> 1;
    TTF_uint16 left     = 0;
    TTF_uint16 right    = segCount - 1;

    while (left <= right) {
        TTF_uint16 mid     = (left + right) / 2;
        TTF_uint16 endCode = TTF_GET_END_CODE(mid);

        if (endCode >= cp) {
            if (mid == 0 || TTF_GET_END_CODE(mid - 1) < cp) {
                TTF_uint32 off            = 16 + 2 * mid;
                TTF_uint8* idRangeOffsets = subtable + 6 * segCount + off;
                TTF_uint16 idRangeOffset  = ttf__get_uint16(idRangeOffsets);
                TTF_uint16 startCode      = ttf__get_uint16(subtable + 2 * segCount + off);

                if (startCode > cp) {
                    return 0;
                }
                
                if (idRangeOffset == 0) {
                    TTF_uint16 idDelta = ttf__get_int16(subtable + 4 * segCount + off);
                    return cp + idDelta;
                }

                return ttf__get_uint16(idRangeOffset + 2 * (cp - startCode) + idRangeOffsets);
            }
            right = mid - 1;
        }
        else {
            left = mid + 1;
        }
    }

    return 0;

    #undef TTF_GET_END_CODE
}


/* -------------------- */
/* Rendering Operations */
/* -------------------- */
static void ttf__convert_points_to_bitmap_space(TTF* font, TTF_F26Dot6_V2* points) {
    // This function ensures that the x and y minimums are 0 and inverts the 
    // y-axis so that y values increase downwards.

    TTF_F26Dot6 xMax = points[0].x;
    TTF_F26Dot6 xMin = points[0].x;
    TTF_F26Dot6 yMin = points[0].y;
    TTF_F26Dot6 yMax = points[0].y;

    for (TTF_uint32 i = 1; i < font->cur.numPoints; i++) {
        if (points[i].x < xMin) {
            xMin = points[i].x;
        }
        else if (points[i].x > xMax) {
            xMax = points[i].x;
        }

        if (points[i].y < yMin) {
            yMin = points[i].y;
        }
        else if (points[i].y > yMax) {
            yMax = points[i].y;
        }
    }

    font->cur.glyph->size.x = labs(xMax - xMin);
    font->cur.glyph->size.y = labs(yMax - yMin);

    for (TTF_uint32 i = 0; i < font->cur.numPoints; i++) {
        points[i].x -= xMin;
        points[i].y  = font->cur.glyph->size.y - (points[i].y - yMin);
    }

    font->cur.glyph->offset.x = ttf__f26dot6_round(xMin) >> 6;
    font->cur.glyph->offset.y = ttf__f26dot6_round(yMax) >> 6;

    font->cur.glyph->size.x = ttf__f26dot6_ceil(font->cur.glyph->size.x) >> 6;
    font->cur.glyph->size.y = ttf__f26dot6_ceil(font->cur.glyph->size.y) >> 6;
}

static TTF_Edge* ttf__get_glyph_edges(TTF* font, TTF_uint32* numEdges) {
    // Get the points of the glyph
    TTF_F26Dot6_V2* points;
    TTF_Point_Type* pointTypes;

    if (font->hasHinting) {
        if (!ttf__execute_glyph_program(font)) {
            return NULL;
        }
        points     = font->cur.zone1.cur;
        pointTypes = font->cur.zone1.pointTypes;
    }
    else {
        if (!ttf__extract_glyph_points(font)) {
            return NULL;
        }
        points     = font->cur.unhinted.points;
        pointTypes = font->cur.unhinted.pointTypes;
    }

    ttf__convert_points_to_bitmap_space(font, points);


    // Convert the glyph points into curves
    TTF_uint32 numCurves;
    TTF_Curve* curves = ttf__convert_points_into_curves(font, points, pointTypes, &numCurves);

    if (font->hasHinting) {
        free(font->cur.zone1.mem);
    }
    else {
        free(font->cur.unhinted.mem);
    }

    if (curves == NULL) {
        return NULL;
    }


    // Approximate the glyph curves using edges
    //
    // This is done because the intersection of a scanline and an edge is 
    // simpler and cheaper to calculate than the intersection of a scanline and 
    // a curve.
    TTF_Edge* edges = ttf__subdivide_curves_into_edges(font, curves, numCurves, numEdges);
    
    free(curves);

    if (edges == NULL) {
        return NULL;
    }

    return edges;
}

static TTF_Curve* ttf__convert_points_into_curves(TTF* font, TTF_F26Dot6_V2* points, TTF_Point_Type* pointTypes, TTF_uint32* numCurves) {
    TTF_Curve* curves = malloc(font->cur.numPoints * sizeof(TTF_Curve));
    if (curves == NULL) {
        return NULL;
    }

    TTF_uint32 startPointIdx = 0;
    *numCurves = 0;

    for (TTF_uint32 i = 0; i < font->cur.numContours; i++) {
        TTF_uint16 endPointIdx   = ttf__get_uint16(font->cur.glyfBlock + 10 + 2 * i);
        TTF_bool   addFinalCurve = TTF_TRUE;

        TTF_F26Dot6_V2* startPoint = points + startPointIdx;
        TTF_F26Dot6_V2* nextP0     = startPoint;
        
        for (TTF_uint32 j = startPointIdx + 1; j <= endPointIdx; j++) {
            TTF_Curve* curve = curves + *numCurves;
            curve->p0 = *nextP0;
            curve->p1 = points[j];

            if (pointTypes[j] == TTF_ON_CURVE_POINT) {
                curve->p2 = curve->p1;
            }
            else if (j == endPointIdx) {
                curve->p2     = *startPoint;
                addFinalCurve = TTF_FALSE;
            }
            else if (pointTypes[j + 1] == TTF_ON_CURVE_POINT) {
                curve->p2 = points[++j];
            }
            else { // Implied on-curve point
                TTF_F26Dot6_V2* nextPoint = points + j + 1;
                ttf__fix_v2_sub(&curve->p1, nextPoint, &curve->p2);
                ttf__fix_v2_scale(&curve->p2, 0x20, 6);
                ttf__fix_v2_add(nextPoint, &curve->p2, &curve->p2);
            }

            nextP0 = &curve->p2;
            (*numCurves)++;
        }

        if (addFinalCurve) {
            TTF_Curve* finalCurve = curves + *numCurves;
            finalCurve->p0 = *nextP0;
            finalCurve->p1 = *startPoint;
            finalCurve->p2 = *startPoint;
            (*numCurves)++;
        }

        startPointIdx = endPointIdx + 1;
    }

    return curves;
}

static TTF_Edge* ttf__subdivide_curves_into_edges(TTF* font, TTF_Curve* curves, TTF_uint32 numCurves, TTF_uint32* numEdges) {
    // Count the number of edges that are needed
    *numEdges = 0;
    for (TTF_uint32 i = 0; i < numCurves; i++) {
        if (curves[i].p1.x == curves[i].p2.x && curves[i].p1.y == curves[i].p2.y) {
            // The curve is a straight line, no need to flatten it
            (*numEdges)++;
        }
        else {
            ttf__subdivide_curve_into_edges(
                &curves[i].p0, &curves[i].p1, &curves[i].p2, 0, NULL, numEdges);
        }
    }

    TTF_Edge* edges = malloc(sizeof(TTF_Edge) * *numEdges);
    if (edges == NULL) {
        return NULL;
    }

    *numEdges = 0;
    for (TTF_uint32 i = 0; i < numCurves; i++) {
        TTF_int8 dir = curves[i].p2.y < curves[i].p0.y ? 1 : -1;

        if (curves[i].p1.x == curves[i].p2.x && curves[i].p1.y == curves[i].p2.y) {
            // The curve is a straight line, no need to flatten it
            ttf__edge_init(edges + *numEdges, &curves[i].p0, &curves[i].p2, dir);
            (*numEdges)++;
        }
        else {
            ttf__subdivide_curve_into_edges(
                &curves[i].p0, &curves[i].p1, &curves[i].p2, dir, edges, numEdges);
        }
    }

    return edges;
}

static void ttf__subdivide_curve_into_edges(TTF_F26Dot6_V2* p0, TTF_F26Dot6_V2* p1, TTF_F26Dot6_V2* p2, TTF_int8 dir, TTF_Edge* edges, TTF_uint32* numEdges) {
    #define TTF_DIVIDE(a, b)                        \
        { TTF_FIX_MUL(((a)->x + (b)->x), 0x20, 6), \
          TTF_FIX_MUL(((a)->y + (b)->y), 0x20, 6) }

    TTF_F26Dot6_V2 mid0 = TTF_DIVIDE(p0, p1);
    TTF_F26Dot6_V2 mid1 = TTF_DIVIDE(p1, p2);
    TTF_F26Dot6_V2 mid2 = TTF_DIVIDE(&mid0, &mid1);

    {
        TTF_F26Dot6_V2 d = TTF_DIVIDE(p0, p2);
        d.x -= mid2.x;
        d.y -= mid2.y;

        TTF_F26Dot6 sqrdError = TTF_FIX_MUL(d.x, d.x, 6) + TTF_FIX_MUL(d.y, d.y, 6);
        if (sqrdError <= TTF_SUBDIVIDE_SQRD_ERROR) {
            if (edges != NULL) {
                ttf__edge_init(edges + *numEdges, p0, p2, dir);
            }
            (*numEdges)++;
            return;
        }
    }

    ttf__subdivide_curve_into_edges(p0, &mid0, &mid2, dir, edges, numEdges);
    ttf__subdivide_curve_into_edges(&mid2, &mid1, p2, dir, edges, numEdges);

    #undef TTF_DIVIDE
}

static void ttf__edge_init(TTF_Edge* edge, TTF_F26Dot6_V2* p0, TTF_F26Dot6_V2* p1, TTF_int8 dir) {
    edge->p0       = *p0;
    edge->p1       = *p1;
    edge->invSlope = ttf__get_inv_slope(p0, p1);
    edge->dir      = dir;
    edge->xMin     = ttf__min(p0->x, p1->x);
    ttf__max_min(p0->y, p1->y, &edge->yMax, &edge->yMin);
}

static TTF_F16Dot16 ttf__get_inv_slope(TTF_F26Dot6_V2* p0, TTF_F26Dot6_V2* p1) {
    if (p0->x == p1->x) {
        return 0;
    }
    if (p0->y == p1->y) {
        return 0;
    }
    
    TTF_F16Dot16 slope = ttf__fix_div(p1->y - p0->y, p1->x - p0->x, 16);
    return ttf__fix_div(1l << 16, slope, 16);
}

static int ttf__compare_edges(const void* edge0, const void* edge1) {
    return ((TTF_Edge*)edge0)->yMin - ((TTF_Edge*)edge1)->yMin;
}

static TTF_F26Dot6 ttf__get_edge_scanline_x_intersection(TTF_Edge* edge, TTF_F26Dot6 scanline) {
    return TTF_FIX_MUL(scanline - edge->p0.y, edge->invSlope, 16) + edge->p0.x;
}

static TTF_bool ttf__active_edge_list_init(TTF_Active_Edge_List* list) {
    list->headChunk = calloc(1, sizeof(TTF_Active_Chunk));
    if (list->headChunk != NULL) {
        list->headEdge      = NULL;
        list->reusableEdges = NULL;
        return TTF_TRUE;
    }
    return TTF_FALSE;
}

static void ttf__active_edge_list_free(TTF_Active_Edge_List* list) {
    TTF_Active_Chunk* chunk = list->headChunk;
    while (chunk != NULL) {
        TTF_Active_Chunk* next = chunk->next;
        free(chunk);
        chunk = next;
    }
}

static TTF_Active_Edge* ttf__get_available_active_edge(TTF_Active_Edge_List* list) {
    if (list->reusableEdges != NULL) {
        // Reuse the memory from a previously removed edge
        TTF_Active_Edge* edge = list->reusableEdges;
        list->reusableEdges = edge->next;
        edge->next          = NULL;
        return edge;
    }
    
    if (list->headChunk->numEdges == TTF_EDGES_PER_CHUNK) {
        // The current chunk is full, so allocate a new one
        TTF_Active_Chunk* chunk = calloc(1, sizeof(TTF_Active_Chunk));
        if (chunk == NULL) {
            return NULL;
        }
        chunk->next     = list->headChunk;
        list->headChunk = chunk;
    }

    TTF_Active_Edge* edge = list->headChunk->edges + list->headChunk->numEdges;
    list->headChunk->numEdges++;
    return edge;
}

static TTF_Active_Edge* ttf__insert_active_edge_first(TTF_Active_Edge_List* list) {
    TTF_Active_Edge* edge = ttf__get_available_active_edge(list);
    edge->next     = list->headEdge;
    list->headEdge = edge;
    return edge;
}

static TTF_Active_Edge* ttf__insert_active_edge_after(TTF_Active_Edge_List* list, TTF_Active_Edge* after) {
    TTF_Active_Edge* edge = ttf__get_available_active_edge(list);
    edge->next  = after->next;
    after->next = edge;
    return edge;
}

static void ttf__remove_active_edge(TTF_Active_Edge_List* list, TTF_Active_Edge* prev, TTF_Active_Edge* remove) {
    if (prev == NULL) {
        list->headEdge = list->headEdge->next;
    }
    else {
        prev->next = remove->next;
    }
    
    // Add the edge to the list of reusable edges so its memory can be reused
    remove->edge          = NULL;
    remove->xIntersection = 0;
    remove->next          = list->reusableEdges;
    list->reusableEdges   = remove;
}

static void ttf__swap_active_edge_with_next(TTF_Active_Edge_List* list, TTF_Active_Edge* prev, TTF_Active_Edge* edge) {
    assert(edge->next != NULL);
    
    if (prev != NULL) {
        prev->next = edge->next;
    }

    TTF_Active_Edge* temp = edge->next->next;
    edge->next->next = edge;
    edge->next       = temp;
}


/* --------------------- */
/* glyf Table Operations */
/* --------------------- */
static TTF_bool ttf__get_glyf_data_block(TTF* font, TTF_uint8** block, TTF_uint32 glyphIdx) {
    #define TTF_GET_OFF_16(idx)\
        (ttf__get_Offset16(font->data + font->loca.off + (2 * (idx))) * 2)

    #define TTF_GET_OFF_32(idx)\
        ttf__get_Offset32(font->data + font->loca.off + (4 * (idx)))

    TTF_int16  version   = ttf__get_int16(font->data + font->head.off + 50);
    TTF_uint16 numGlyphs = ttf__get_num_glyphs(font);

    TTF_Offset32 blockOff, nextBlockOff;

    if (glyphIdx == numGlyphs - 1) {
        blockOff = version == 0 ? TTF_GET_OFF_16(glyphIdx) : TTF_GET_OFF_32(glyphIdx);
        *block   = font->data + font->glyf.off + blockOff;
        return TTF_TRUE;
    }

    if (version == 0) {
        blockOff     = TTF_GET_OFF_16(glyphIdx);
        nextBlockOff = TTF_GET_OFF_16(glyphIdx + 1);
    }
    else {
        blockOff     = TTF_GET_OFF_32(glyphIdx);
        nextBlockOff = TTF_GET_OFF_32(glyphIdx + 1);
    }
    
    if (blockOff == nextBlockOff) {
        // "If a glyph has no outline, then loca[n] = loca [n+1]"
        return TTF_FALSE;
    }

    *block = font->data + font->glyf.off + blockOff;
    return TTF_TRUE;
    
    #undef TTF_GET_OFF_16
    #undef TTF_GET_OFF_32
}

static TTF_int16 ttf__get_num_glyph_contours(TTF* font) {
    return ttf__get_int16(font->cur.glyfBlock);
}

static TTF_int32 ttf__get_num_glyph_points(TTF* font) {
    return 1 + ttf__get_uint16(font->cur.glyfBlock + 8 + 2 * font->cur.numContours);
}

static TTF_bool ttf__extract_glyph_points(TTF* font) {
    TTF_uint32      pointIdx = 0;
    TTF_V2*         points;
    TTF_Point_Type* pointTypes;

    if (font->hasHinting) {
        font->cur.zone1.cap = TTF_NUM_PHANTOM_POINTS + font->cur.numPoints;

        size_t ptsSize   = font->cur.zone1.cap * sizeof(TTF_F26Dot6_V2);
        size_t touchSize = font->cur.zone1.cap * sizeof(TTF_Touch_Flag);
        size_t typesSize = font->cur.zone1.cap * sizeof(TTF_Point_Type);
        size_t off       = 0;
        
        font->cur.zone1.mem = malloc(3 * ptsSize + touchSize + typesSize);
        if (font->cur.zone1.mem == NULL) {
            return TTF_FALSE;
        }

        font->cur.zone1.org        = (TTF_V2*)        (font->cur.zone1.mem);
        font->cur.zone1.orgScaled  = (TTF_F26Dot6_V2*)(font->cur.zone1.mem + (off += ptsSize));
        font->cur.zone1.cur        = (TTF_F26Dot6_V2*)(font->cur.zone1.mem + (off += ptsSize));
        font->cur.zone1.touchFlags = (TTF_Touch_Flag*)(font->cur.zone1.mem + (off += ptsSize));
        font->cur.zone1.pointTypes = (TTF_Point_Type*)(font->cur.zone1.mem + (off += touchSize));

        points     = font->cur.zone1.org;
        pointTypes = font->cur.zone1.pointTypes;
    }
    else {
        size_t pointsSize     = sizeof(TTF_F26Dot6_V2) * font->cur.numPoints;
        size_t pointTypesSize = sizeof(TTF_Point_Type) * font->cur.numPoints;

        font->cur.unhinted.mem = malloc(pointsSize + pointTypesSize);
        if (font->cur.unhinted.mem == NULL) {
            return TTF_FALSE;
        }

        font->cur.unhinted.points     = (TTF_F26Dot6_V2*)(font->cur.unhinted.mem);
        font->cur.unhinted.pointTypes = (TTF_Point_Type*)(font->cur.unhinted.mem + pointsSize);

        points     = font->cur.unhinted.points;
        pointTypes = font->cur.unhinted.pointTypes;
    }


    // Get the contour points from the glyf table data
    {
        TTF_uint32 flagsSize = 0;
        TTF_uint32 xDataSize = 0;
        TTF_V2     absPos    = { 0 };

        TTF_uint8* flagData, *xData, *yData;

        flagData  = font->cur.glyfBlock + (10 + 2 * font->cur.numContours);
        flagData += 2 + ttf__get_uint16(flagData);
        
        for (TTF_uint32 i = 0; i < font->cur.numPoints;) {
            TTF_uint8 flags = flagData[flagsSize];
            
            TTF_uint8 xSize = 
                flags & TTF_GLYF_X_SHORT_VECTOR ? 1 : flags & TTF_GLYF_X_DUAL ? 0 : 2;
            
            TTF_uint8 flagsReps;

            if (flags & TTF_GLYF_REPEAT_FLAG) {
                flagsReps = 1 + flagData[flagsSize + 1];
                flagsSize += 2;
            }
            else {
                flagsReps = 1;
                flagsSize++;
            }

            i += flagsReps;

            while (flagsReps > 0) {
                xDataSize += xSize;
                flagsReps--;
            }
        }
        
        xData = flagData + flagsSize;
        yData = xData    + xDataSize;

        while (pointIdx < font->cur.numPoints) {
            TTF_uint8 flags = *flagData;

            TTF_uint8 flagsReps;
            if (flags & TTF_GLYF_REPEAT_FLAG) {
                flagsReps = 1 + flagData[1];
                flagData += 2;
            }
            else {
                flagsReps = 1;
                flagData++;
            }

            while (flagsReps > 0) {
                TTF_int32 xOff = ttf__get_next_coord_off(
                    &xData, TTF_GLYF_X_DUAL, TTF_GLYF_X_SHORT_VECTOR, flags);

                TTF_int32 yOff = ttf__get_next_coord_off(
                    &yData, TTF_GLYF_Y_DUAL, TTF_GLYF_Y_SHORT_VECTOR, flags);

                if (flags & TTF_GLYF_ON_CURVE_POINT) {
                    pointTypes[pointIdx] = TTF_ON_CURVE_POINT;
                }
                else {
                    pointTypes[pointIdx] = TTF_OFF_CURVE_POINT;
                }

                points[pointIdx].x = absPos.x + xOff;
                points[pointIdx].y = absPos.y + yOff;
                absPos             = points[pointIdx];

                pointIdx++;
                flagsReps--;
            }
        }
    }


    if (!font->hasHinting) {
        for (TTF_uint32 i = 0; i < font->cur.numPoints; i++) {
            points[i].x = TTF_FIX_MUL(points[i].x << 6, font->cur.instance->scale, 22);
            points[i].y = TTF_FIX_MUL(points[i].y << 6, font->cur.instance->scale, 22);
        }
        return TTF_TRUE;
    }


    // Get the phantom points
    {
        TTF_int16 xMin            = ttf__get_int16(font->cur.glyfBlock + 2);
        TTF_int16 yMax            = ttf__get_int16(font->cur.glyfBlock + 8);
        TTF_int16 leftSideBearing = ttf__get_glyph_left_side_bearing(font, font->cur.glyph->idx);

        TTF_int32 yAdvance, topSideBearing;
        ttf__get_glyph_vmetrics(font, font->cur.glyfBlock, &yAdvance, &topSideBearing);

        points[pointIdx].x = xMin - leftSideBearing;
        points[pointIdx].y = 0;

        pointIdx++;
        points[pointIdx].x = points[pointIdx - 1].x + font->cur.glyph->xAdvance;
        points[pointIdx].y = 0;

        pointIdx++;
        points[pointIdx].y = yMax + topSideBearing;
        points[pointIdx].x = 0;

        pointIdx++;
        points[pointIdx].y = points[pointIdx - 1].y - yAdvance;
        points[pointIdx].x = 0;

        pointIdx++;
        assert(pointIdx == font->cur.zone1.cap);
    }


    // Set the scaled points
    for (TTF_uint32 i = 0; i < font->cur.zone1.cap; i++) {
        font->cur.zone1.orgScaled[i].x = 
            TTF_FIX_MUL(points[i].x << 6, font->cur.instance->scale, 22);

        font->cur.zone1.orgScaled[i].y = 
            TTF_FIX_MUL(points[i].y << 6, font->cur.instance->scale, 22);
    }


    // Set the current points, phantom points are rounded
    for (TTF_uint32 i = 0; i < font->cur.zone1.cap; i++) {
        font->cur.zone1.cur[i]        = font->cur.zone1.orgScaled[i];
        font->cur.zone1.touchFlags[i] = TTF_UNTOUCHED;
    }

    font->cur.zone1.cur[font->cur.numPoints].x =
        ttf__round(font, font->cur.zone1.cur[font->cur.numPoints].x);

    font->cur.zone1.cur[font->cur.numPoints + 1].x = 
        ttf__round(font, font->cur.zone1.cur[font->cur.numPoints + 1].x);

    font->cur.zone1.cur[font->cur.numPoints + 2].y = 
        ttf__round(font, font->cur.zone1.cur[font->cur.numPoints + 2].y);

    font->cur.zone1.cur[font->cur.numPoints + 3].y = 
        ttf__round(font, font->cur.zone1.cur[font->cur.numPoints + 3].y);

    return TTF_TRUE;
}

static TTF_int32 ttf__get_next_coord_off(TTF_uint8** data, TTF_uint8 dualFlag, TTF_uint8 shortFlag, TTF_uint8 flags) {
    TTF_int32 coord;

    if (flags & shortFlag) {
        coord = !(flags & dualFlag) ? -(**data) : **data;
        (*data)++;
    }
    else if (flags & dualFlag) {
        coord = 0;
    }
    else {
        coord = ttf__get_int16(*data);
        (*data) += 2;
    }

    return coord;
}


/* ---------------------------- */
/* Interpreter Stack Operations */
/* ---------------------------- */
static void ttf__stack_push_uint32(TTF* font, TTF_uint32 val) {
    assert(font->stack.count < font->stack.cap);
    font->stack.frames[font->stack.count++].uValue = val;
}

static void ttf__stack_push_int32(TTF* font, TTF_int32 val) {
    assert(font->stack.count < font->stack.cap);
    font->stack.frames[font->stack.count++].sValue = val;
}

static TTF_uint32 ttf__stack_pop_uint32(TTF* font) {
    assert(font->stack.count > 0);
    return font->stack.frames[--font->stack.count].uValue;
}

static TTF_int32 ttf__stack_pop_int32(TTF* font) {
    assert(font->stack.count > 0);
    return font->stack.frames[--font->stack.count].sValue;
}

static void ttf__stack_clear(TTF* font) {
    font->stack.count = 0;
}

static TTF_uint8 ttf__get_num_vals_to_push(TTF_uint8 ins) {
    return 1 + (ins & 0x7);
}


/* ----------------------------- */
/* Instruction Stream Operations */
/* ----------------------------- */
static void ttf__ins_stream_init(TTF_Ins_Stream* stream, TTF_uint8* bytes) {
    stream->bytes = bytes;
    stream->off   = 0;
}

static TTF_uint8 ttf__ins_stream_next(TTF_Ins_Stream* stream) {
    return stream->bytes[stream->off++];
}

static void ttf__ins_stream_jump(TTF_Ins_Stream* stream, TTF_int32 count) {
    stream->off += count;
}


/* --------------------- */
/* Instruction Execution */
/* --------------------- */
static void ttf__execute_font_program(TTF* font) {
    TTF_LOG_PROGRAM("Font Program");

    TTF_Ins_Stream stream;
    ttf__ins_stream_init(&stream, font->data + font->fpgm.off);
    
    while (stream.off < font->fpgm.size) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        assert(ins == TTF_PUSHB || ins == TTF_FDEF || ins == TTF_IDEF);
        ttf__execute_ins(font, &stream, ins);
    }
}

static void ttf__execute_cv_program(TTF* font) {
    TTF_LOG_PROGRAM("CV Program");

    {
        // "Every time the control value program is run, the zone 0 contour 
        // data is initialized to 0s."
        size_t ptsSize   = font->cur.instance->zone0.cap * sizeof(TTF_F26Dot6_V2);
        size_t touchSize = font->cur.instance->zone0.cap * sizeof(TTF_Touch_Flag);
        memset(font->cur.instance->zone0.mem, 0, 2 * ptsSize + touchSize);
    }

    TTF_Ins_Stream stream;
    ttf__ins_stream_init(&stream, font->data + font->prep.off);

    ttf__initialize_graphics_state(font);
    ttf__stack_clear(font);
    
    while (stream.off < font->prep.size) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        ttf__execute_ins(font, &stream, ins);
    }

    font->cur.instance->gStateDefault = font->gState;
}

static TTF_bool ttf__execute_glyph_program(TTF* font) {
    TTF_LOG_PROGRAM("Glyph Program");

    ttf__stack_clear(font);
    font->gState = font->cur.instance->gStateDefault;

    // TODO: handle composite glyphs
    assert(font->cur.numContours >= 0);

    TTF_uint32 insOff = 10 + font->cur.numContours * 2;
    TTF_uint16 numIns = ttf__get_int16(font->cur.glyfBlock + insOff);

    TTF_Ins_Stream stream;
    ttf__ins_stream_init(&stream, font->cur.glyfBlock + insOff + 2);
    
    if (!ttf__extract_glyph_points(font)) {
        return TTF_FALSE;
    }

    while (stream.off < numIns) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        ttf__execute_ins(font, &stream, ins);
    }

    return TTF_TRUE;
}

static void ttf__execute_ins(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    switch (ins) {
        case TTF_ABS:
            ttf__ABS(font);
            return;
        case TTF_ADD:
            ttf__ADD(font);
            return;
        case TTF_ALIGNRP:
            ttf__ALIGNRP(font);
            return;
        case TTF_AND:
            ttf__AND(font);
            return;
        case TTF_CALL:
            ttf__CALL(font);
            return;
        case TTF_CINDEX:
            ttf__CINDEX(font);
            return;
        case TTF_DELTAC1:
            ttf__DELTAC1(font);
            return;
        case TTF_DELTAC2:
            ttf__DELTAC2(font);
            return;
        case TTF_DELTAC3:
            ttf__DELTAC3(font);
            return;
        case TTF_DELTAP1:
            ttf__DELTAP1(font);
            return;
        case TTF_DELTAP2:
            ttf__DELTAP2(font);
            return;
        case TTF_DELTAP3:
            ttf__DELTAP3(font);
            return;
        case TTF_DEPTH:
            ttf__DEPTH(font);
            return;
        case TTF_DIV:
            ttf__DIV(font);
            return;
        case TTF_DUP:
            ttf__DUP(font);
            return;
        case TTF_EQ:
            ttf__EQ(font);
            return;
        case TTF_FDEF:
            ttf__FDEF(font, stream);
            return;
        case TTF_FLOOR:
            ttf__FLOOR(font);
            return;
        case TTF_GETINFO:
            ttf__GETINFO(font);
            return;
        case TTF_GPV:
            ttf__GPV(font);
            return;
        case TTF_GT:
            ttf__GT(font);
            return;
        case TTF_GTEQ:
            ttf__GTEQ(font);
            return;
        case TTF_IDEF:
            ttf__IDEF(font, stream);
            return;
        case TTF_IF:
            ttf__IF(font, stream);
            return;
        case TTF_IP:
            ttf__IP(font);
            return;
        case TTF_JROT:
            ttf__JROT(font, stream);
            return;
        case TTF_JMPR:
            ttf__JMPR(font, stream);
            return;
        case TTF_LOOPCALL:
            ttf__LOOPCALL(font);
            return;
        case TTF_LT:
            ttf__LT(font);
            return;
        case TTF_LTEQ:
            ttf__LTEQ(font);
            return;
        case TTF_MINDEX:
            ttf__MINDEX(font);
            return;
        case TTF_MPPEM:
            ttf__MPPEM(font);
            return;
        case TTF_MUL:
            ttf__MUL(font);
            return;
        case TTF_NEG:
            ttf__NEG(font);
            return;
        case TTF_NEQ:
            ttf__NEQ(font);
            return;
        case TTF_NPUSHB:
            ttf__NPUSHB(font, stream);
            return;
        case TTF_NPUSHW:
            ttf__NPUSHW(font, stream);
            return;
        case TTF_OR:
            ttf__OR(font);
            return;
        case TTF_POP:
            ttf__POP(font);
            return;
        case TTF_RCVT:
            ttf__RCVT(font);
            return;
        case TTF_RDTG:
            ttf__RDTG(font);
            return;
        case TTF_ROLL:
            ttf__ROLL(font);
            return;
        case TTF_RS:
            ttf__RS(font);
            return;
        case TTF_RTG:
            ttf__RTG(font);
            return;
        case TTF_RTHG:
            ttf__RTHG(font);
            return;
        case TTF_RUTG:
            ttf__RUTG(font);
            return;
        case TTF_SCANCTRL:
            ttf__SCANCTRL(font);
            return;
        case TTF_SCANTYPE:
            ttf__SCANTYPE(font);
            return;
        case TTF_SCVTCI:
            ttf__SCVTCI(font);
            return;
        case TTF_SDB:
            ttf__SDB(font);
            return;
        case TTF_SDS:
            ttf__SDS(font);
            return;
        case TTF_SHPIX:
            ttf__SHPIX(font);
            return;
        case TTF_SLOOP:
            ttf__SLOOP(font);
            return;
        case TTF_SRP0:
            ttf__SRP0(font);
            return;
        case TTF_SRP1:
            ttf__SRP1(font);
            return;
        case TTF_SRP2:
            ttf__SRP2(font);
            return;
        case TTF_SUB:
            ttf__SUB(font);
            return;
        case TTF_SWAP:
            ttf__SWAP(font);
            return;
        case TTF_SZPS:
            ttf__SZPS(font);
            return;
        case TTF_SZP0:
            ttf__SZP0(font);
            return;
        case TTF_SZP1:
            ttf__SZP1(font);
            return;
        case TTF_SZP2:
            ttf__SZP2(font);
            return;
        case TTF_WCVTF:
            ttf__WCVTF(font);
            return;
        case TTF_WCVTP:
            ttf__WCVTP(font);
            return;
        case TTF_WS:
            ttf__WS(font);
            return;
    }

    if (ins >= TTF_GC && ins <= TTF_GC_MAX) {
        ttf__GC(font, ins);
        return;
    }
    else if (ins >= TTF_IUP && ins <= TTF_IUP_MAX) {
        ttf__IUP(font, ins);
        return;
    }
    else if (ins >= TTF_MD && ins <= TTF_MD_MAX) {
        ttf__MD(font, ins);
        return;
    }
    else if (ins >= TTF_MDAP && ins <= TTF_MDAP_MAX) {
        ttf__MDAP(font, ins);
        return;
    }
    else if (ins >= TTF_MDRP && ins <= TTF_MDRP_MAX) {
        ttf__MDRP(font, ins);
        return;
    }
    else if (ins >= TTF_MIAP && ins <= TTF_MIAP_MAX) {
        ttf__MIAP(font, ins);
        return;
    }
    else if (ins >= TTF_MIRP && ins <= TTF_MIRP_MAX) {
        ttf__MIRP(font, ins);
        return;
    }
    else if (ins >= TTF_PUSHB && ins <= TTF_PUSHB_MAX) {
        ttf__PUSHB(font, stream, ins);
        return;
    }
    else if (ins >= TTF_PUSHW && ins <= TTF_PUSHW_MAX) {
        ttf__PUSHW(font, stream, ins);
        return;
    }
    else if (ins >= TTF_ROUND && ins <= TTF_ROUND_MAX) {
        ttf__ROUND(font, ins);
        return;
    }
    else if (ins >= TTF_SVTCA && ins <= TTF_SVTCA_MAX) {
        ttf__SVTCA(font, ins);
        return;
    }

    TTF_LOG_UNKNOWN_INS(ins);
    assert(0);
}

static void ttf__ABS(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_F26Dot6 val = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, labs(val));
    TTF_LOG_VALUE((int)labs(val), TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__ADD(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_F26Dot6 n1 = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 n2 = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, n1 + n2);
    TTF_LOG_VALUE(n1 + n2, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__ALIGNRP(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    TTF_F26Dot6_V2* rp0Cur = font->gState.zp0->cur + font->gState.rp0;

    for (TTF_uint32 i = 0; i < font->gState.loop; i++) {
        TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);
        assert(pointIdx < font->gState.zp1->cap);

        TTF_F26Dot6 dist = ttf__fix_v2_sub_dot(
            rp0Cur, font->gState.zp1->cur + pointIdx, &font->gState.projVec, 14);

        ttf__move_point(font, font->gState.zp1, pointIdx, dist);

        TTF_LOG_POINT(font->gState.zp1->cur[pointIdx], TTF_LOG_LEVEL_MINIMAL);
    }

    font->gState.loop = 1;
}

static void ttf__AND(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 != 0 && e2 != 0 ? 1 : 0);
    TTF_LOG_VALUE(e1 != 0 && e2 != 0, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__CALL(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    ttf__call_func(font, ttf__stack_pop_uint32(font), 1);
}

static void ttf__CINDEX(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 pos = ttf__stack_pop_uint32(font);
    TTF_uint32 val = font->stack.frames[font->stack.count - pos].uValue;
    ttf__stack_push_uint32(font, val);
    TTF_LOG_VALUE(val, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__DELTAC1(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    ttf__DELTAC(font, 0);
}

static void ttf__DELTAC2(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    ttf__DELTAC(font, 16);
}

static void ttf__DELTAC3(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    ttf__DELTAC(font, 32);
}

static void ttf__DELTAC(TTF* font, TTF_uint8 range) {
    TTF_uint32 count = ttf__stack_pop_uint32(font);

    while (count > 0) {
        TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
        TTF_uint32 exc    = ttf__stack_pop_uint32(font);

        TTF_F26Dot6 deltaVal;
        if (ttf__get_delta_value(font, exc, range, &deltaVal)) {
            font->cur.instance->cvt[cvtIdx] += deltaVal;
            TTF_LOG_VALUE(deltaVal, TTF_LOG_LEVEL_MINIMAL);
        }

        count--;
    }
}

static void ttf__DELTAP1(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    ttf__DELTAP(font, 0);
}

static void ttf__DELTAP2(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    ttf__DELTAP(font, 16);
}

static void ttf__DELTAP3(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    ttf__DELTAP(font, 32);
}

static void ttf__DELTAP(TTF* font, TTF_uint8 range) {
    TTF_uint32 count = ttf__stack_pop_uint32(font);

    while (count > 0) {
        TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);
        TTF_uint32 exc      = ttf__stack_pop_uint32(font);

        TTF_F26Dot6 deltaVal;
        if (ttf__get_delta_value(font, exc, range, &deltaVal)) {
            ttf__move_point(font, font->gState.zp0, pointIdx, deltaVal);
            TTF_LOG_VALUE(deltaVal, TTF_LOG_LEVEL_MINIMAL);
        }

        count--;
    }
}

static void ttf__DEPTH(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_LOG_VALUE(font->stack.count, TTF_LOG_LEVEL_VERBOSE);
    ttf__stack_push_uint32(font, font->stack.count);
}

static void ttf__DIV(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    TTF_F26Dot6 n1 = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 n2 = ttf__stack_pop_F26Dot6(font);
    assert(n1 != 0);

    TTF_bool isNeg = TTF_FALSE;
    
    if (n2 < 0) {
        n2    = -n2;
        isNeg = TTF_TRUE;
    }
    if (n1 < 0) {
        n1    = -n1;
        isNeg = !isNeg;
    }

    TTF_F26Dot6 result = ((TTF_int64)n2 << 6) / n1;
    if (isNeg) {
        result = -result;
    }

    ttf__stack_push_F26Dot6(font, result);
    TTF_LOG_VALUE(result, TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__DUP(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 e = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e);
    ttf__stack_push_uint32(font, e);
    TTF_LOG_VALUE(e, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__EQ(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_int32 e2 = ttf__stack_pop_uint32(font);
    TTF_int32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 == e2 ? 1 : 0);
    TTF_LOG_VALUE(e1 == e2, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__FDEF(TTF* font, TTF_Ins_Stream* stream) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    assert(font->funcArray.count < font->funcArray.cap);

    TTF_uint32 funcId = ttf__stack_pop_uint32(font);
    assert(funcId < font->funcArray.cap);

    font->funcArray.funcs[funcId].firstIns = stream->bytes + stream->off;
    font->funcArray.count++;

    while (ttf__ins_stream_next(stream) != TTF_ENDF);
}

static void ttf__FLOOR(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    TTF_F26Dot6 val = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, ttf__f26dot6_floor(val));
    TTF_LOG_VALUE(ttf__f26dot6_floor(val), TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__GC(TTF* font, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    TTF_uint32  pointIdx = ttf__stack_pop_uint32(font);
    TTF_F26Dot6 value;

    assert(font->gState.zp2 != NULL);
    assert(pointIdx < font->gState.zp2->cap);

    if (ins & 0x1) {
        value = ttf__fix_v2_dot(
            font->gState.zp2->orgScaled + pointIdx, &font->gState.dualProjVec, 14);
    }
    else {
        value = ttf__fix_v2_dot(font->gState.zp2->cur + pointIdx, &font->gState.projVec, 14);
    }

    ttf__stack_push_F26Dot6(font, value);

    TTF_LOG_VALUE(value, TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__GETINFO(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    // These are the only supported selector bits for scalar version 35
    enum {
        TTF_VERSION                  = 0x01,
        TTF_GLYPH_ROTATED            = 0x02,
        TTF_GLYPH_STRETCHED          = 0x04,
        TTF_FONT_SMOOTHING_GRAYSCALE = 0x20,
    };

    TTF_uint32 result   = 0;
    TTF_uint32 selector = ttf__stack_pop_uint32(font);

    if (selector & TTF_VERSION) {
        result = TTF_SCALAR_VERSION;
    }
    if (selector & TTF_GLYPH_ROTATED) {
        if (font->cur.instance->isRotated) {
            result |= 0x100;
        }
    }
    if (selector & TTF_GLYPH_STRETCHED) {
        if (font->cur.instance->isStretched) {
            result |= 0x200;
        }
    }
    if (selector & TTF_FONT_SMOOTHING_GRAYSCALE) {
        // result |= 0x1000;
    }

    TTF_LOG_VALUE(result, TTF_LOG_LEVEL_MINIMAL);
    ttf__stack_push_uint32(font, result);
}

static void ttf__GPV(TTF* font) {
    // TODO
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    assert(0);
}

static void ttf__GT(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_int32 e2 = ttf__stack_pop_uint32(font);
    TTF_int32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 > e2 ? 1 : 0);
    TTF_LOG_VALUE(e1 > e2, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__GTEQ(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_int32 e2 = ttf__stack_pop_uint32(font);
    TTF_int32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 >= e2 ? 1 : 0);
    TTF_LOG_VALUE(e1 >= e2, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__IDEF(TTF* font, TTF_Ins_Stream* stream) {
    // TODO
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    assert(0);
}

static void ttf__IF(TTF* font, TTF_Ins_Stream* stream) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    if (ttf__stack_pop_uint32(font) == 0) {
        TTF_LOG_VALUE(0, TTF_LOG_LEVEL_VERBOSE);
        if (ttf__jump_to_else_or_eif(stream) == TTF_EIF) {
            // Condition is false and there is no else instruction
            return;
        }
    }
    else {
        TTF_LOG_VALUE(1, TTF_LOG_LEVEL_VERBOSE);
    }

    while (TTF_TRUE) {
        TTF_uint8 ins = ttf__ins_stream_next(stream);

        if (ins == TTF_ELSE) {
            ttf__jump_to_else_or_eif(stream);
            return;
        }

        if (ins == TTF_EIF) {
            return;
        }

        ttf__execute_ins(font, stream, ins);
    }
}

static void ttf__IP(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    assert(font->gState.rp1 < font->gState.zp0->cap);
    assert(font->gState.rp2 < font->gState.zp1->cap);

    TTF_F26Dot6_V2* rp1Cur = font->gState.zp0->cur + font->gState.rp1;
    TTF_F26Dot6_V2* rp2Cur = font->gState.zp1->cur + font->gState.rp2;

    TTF_bool isTwilightZone = 
        (font->gState.zp0 == &font->cur.instance->zone0) ||
        (font->gState.zp1 == &font->cur.instance->zone0) ||
        (font->gState.zp2 == &font->cur.instance->zone0);

    TTF_F26Dot6_V2* rp1Org, *rp2Org;

    if (isTwilightZone) {
        // Twilight zone doesn't have unscaled coordinates
        rp1Org = font->gState.zp0->orgScaled + font->gState.rp1;
        rp2Org = font->gState.zp1->orgScaled + font->gState.rp2;
    }
    else {
        // Use unscaled coordinates for more precision
        rp1Org = font->gState.zp0->org + font->gState.rp1;
        rp2Org = font->gState.zp1->org + font->gState.rp2;
    }

    TTF_F26Dot6 totalDistCur = ttf__fix_v2_sub_dot(rp2Cur, rp1Cur, &font->gState.projVec, 14);
    TTF_F26Dot6 totalDistOrg = ttf__fix_v2_sub_dot(rp2Org, rp1Org, &font->gState.dualProjVec, 14);

    for (TTF_uint32 i = 0; i < font->gState.loop; i++) {
        TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);
        assert(pointIdx < font->gState.zp2->cap);

        TTF_F26Dot6_V2* pointCur = font->gState.zp2->cur + pointIdx;
        TTF_F26Dot6_V2* pointOrg = 
            (isTwilightZone ? font->gState.zp2->orgScaled : font->gState.zp2->org) + pointIdx;

        TTF_F26Dot6 distCur = ttf__fix_v2_sub_dot(pointCur, rp1Cur, &font->gState.projVec, 14);
        TTF_F26Dot6 distOrg = ttf__fix_v2_sub_dot(pointOrg, rp1Org, &font->gState.dualProjVec, 14);

        // Scale distOrg by however many times bigger totalDistCur is than
        // totalDistOrg. 
        //
        // This ensures D(p,rp1)/D(p',rp1') = D(p,rp2)/D(p',rp2') holds true.
        TTF_F26Dot6 distNew = 
            ttf__fix_div(TTF_FIX_MUL(distOrg, totalDistCur, 6), totalDistOrg, 6);

        ttf__move_point(font, font->gState.zp2, pointIdx, distNew - distCur);

        TTF_LOG_POINT(*pointCur, TTF_LOG_LEVEL_MINIMAL);
    }

    font->gState.loop = 1;
}

static void ttf__IUP(TTF* font, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    // Applying IUP to zone0 is an error
    // TODO: How are composite glyphs handled?
    assert(font->gState.zp2 == &font->cur.zone1);
    assert(font->cur.numContours >= 0);

    if (font->cur.numContours == 0) {
        return;
    }

    TTF_Touch_Flag  touchFlag  = ins & 0x1 ? TTF_TOUCH_X : TTF_TOUCH_Y;
    TTF_Touch_Flag* touchFlags = font->cur.zone1.touchFlags;
    TTF_uint32      pointIdx   = 0;

    for (TTF_uint16 i = 0; i < font->cur.numContours; i++) {
        TTF_uint16 startPointIdx = pointIdx;
        TTF_uint16 endPointIdx   = ttf__get_uint16(font->cur.glyfBlock + 10 + 2 * i);
        TTF_uint16 touch0        = 0;
        TTF_bool   findingTouch1 = TTF_FALSE;

        while (pointIdx <= endPointIdx) {
            if (touchFlags[pointIdx] & touchFlag) {
                if (findingTouch1) {
                    ttf__IUP_interpolate_or_shift(
                        &font->cur.zone1, touchFlag, startPointIdx, endPointIdx, touch0, 
                        pointIdx);

                    findingTouch1 = 
                        pointIdx != endPointIdx || (touchFlags[pointIdx + 1] & touchFlag) == 0;

                    if (findingTouch1) {
                        touch0 = pointIdx;
                    }
                }
                else {
                    touch0        = pointIdx;
                    findingTouch1 = TTF_TRUE;
                }
            }

            pointIdx++;
        }

        if (findingTouch1) {
            // The index of the second touched point wraps back to the 
            // beginning.
            for (TTF_uint32 i = startPointIdx; i <= touch0; i++) {
                if (touchFlags[i] & touchFlag) {
                    ttf__IUP_interpolate_or_shift(
                        &font->cur.zone1, touchFlag, startPointIdx, endPointIdx, touch0, i);
                    break;
                }
            }
        }
    }

    for (int i = 0; i < font->cur.numPoints; i++) {
        printf("%d) (%d, %d)\n", i, font->cur.zone1.cur[i].x, font->cur.zone1.cur[i].y);
    }
}

static void ttf__JROT(TTF* font, TTF_Ins_Stream* stream) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    
    TTF_uint32 val = ttf__stack_pop_uint32(font);
    TTF_int32  off = ttf__stack_pop_int32(font); 

    if (val != 0) {
        ttf__ins_stream_jump(stream, off - 1);
        TTF_LOG_VALUE(off - 1, TTF_LOG_LEVEL_VERBOSE);
    }
    else {
        TTF_LOG_VALUE(0, TTF_LOG_LEVEL_VERBOSE);
    }
}

static void ttf__JMPR(TTF* font, TTF_Ins_Stream* stream) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_int32 off = ttf__stack_pop_int32(font);
    ttf__ins_stream_jump(stream, off - 1);
    TTF_LOG_VALUE(off - 1, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__LOOPCALL(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 funcId = ttf__stack_pop_uint32(font);
    TTF_uint32 times  = ttf__stack_pop_uint32(font);
    ttf__call_func(font, funcId, times);
}

static void ttf__LT(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_int32 e2 = ttf__stack_pop_uint32(font);
    TTF_int32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 < e2 ? 1 : 0);
    TTF_LOG_VALUE(e1 < e2, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__LTEQ(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_int32 e2 = ttf__stack_pop_uint32(font);
    TTF_int32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 <= e2 ? 1 : 0);
    TTF_LOG_VALUE(e1 <= e2, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__MD(TTF* font, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    
    TTF_uint32  pointIdx0 = ttf__stack_pop_uint32(font);
    TTF_uint32  pointIdx1 = ttf__stack_pop_uint32(font);
    TTF_F26Dot6 dist;

    // TODO: Spec says if ins & 0x1 = 1 then use original outline, but FreeType
    //       uses current outline.

    if (ins & 0x1) {
        dist = ttf__fix_v2_sub_dot(
            font->gState.zp0->cur + pointIdx1, font->gState.zp1->cur + pointIdx0, 
            &font->gState.projVec, 14);
    }
    else {
        TTF_bool isTwilightZone =
            font->gState.zp0 == &font->cur.instance->zone0 ||
            font->gState.zp1 == &font->cur.instance->zone0;

        if (isTwilightZone) {
            dist = ttf__fix_v2_sub_dot(
                font->gState.zp0->orgScaled + pointIdx1, font->gState.zp1->orgScaled + pointIdx0,
                &font->gState.projVec, 14);
        }
        else {
            dist = ttf__fix_v2_sub_dot(
                font->gState.zp0->org + pointIdx1, font->gState.zp1->org + pointIdx0,
                &font->gState.dualProjVec, 14);

            dist = TTF_FIX_MUL(dist, font->cur.instance->scale, 22);
        }
    }

    ttf__stack_push_F26Dot6(font, dist);

    TTF_LOG_VALUE(dist, TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__MDAP(TTF* font, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    TTF_uint32      pointIdx = ttf__stack_pop_uint32(font);
    TTF_F26Dot6_V2* point    = font->gState.zp0->cur + pointIdx;

    if (ins & 0x1) {
        // Move the point to its rounded position
        TTF_F26Dot6 curDist     = ttf__fix_v2_dot(point, &font->gState.projVec, 14);
        TTF_F26Dot6 roundedDist = ttf__round(font, curDist);
        ttf__move_point(font, font->gState.zp0, pointIdx, roundedDist - curDist);
    }
    else {
        // Don't move the point, just mark it as touched
        font->gState.zp0->touchFlags[pointIdx] |= font->gState.touchFlags;
    }

    font->gState.rp0 = pointIdx;
    font->gState.rp1 = pointIdx;

    TTF_LOG_POINT(*point, TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__MDRP(TTF* font, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    assert(font->gState.rp0 < font->gState.zp0->cap);

    TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);
    assert(pointIdx < font->gState.zp1->cap);

    TTF_F26Dot6_V2* rp0Cur   = font->gState.zp0->cur + font->gState.rp0;
    TTF_F26Dot6_V2* pointCur = font->gState.zp1->cur + pointIdx;

    TTF_bool isTwilightZone = 
        (font->gState.zp0 == &font->cur.instance->zone0) || 
        (font->gState.zp1 == &font->cur.instance->zone0);

    TTF_F26Dot6_V2* rp0Org, *pointOrg;

    if (isTwilightZone) {
        // Twilight zone doesn't have unscaled coordinates
        rp0Org   = font->gState.zp0->orgScaled + font->gState.rp0;
        pointOrg = font->gState.zp1->orgScaled + pointIdx;
    }
    else {
        // Use unscaled coordinates for more precision
        rp0Org   = font->gState.zp0->org + font->gState.rp0;
        pointOrg = font->gState.zp1->org + pointIdx;
    }

    TTF_F26Dot6 distCur = ttf__fix_v2_sub_dot(pointCur, rp0Cur, &font->gState.projVec, 14);
    TTF_F26Dot6 distOrg = ttf__fix_v2_sub_dot(pointOrg, rp0Org, &font->gState.dualProjVec, 14);

    if (!isTwilightZone) {
        // Make distOrg a 26.6 pixel value
        distOrg = TTF_FIX_MUL(distOrg << 6, font->cur.instance->scale, 22);
    }

    distOrg = ttf__apply_single_width_cut_in(font, distOrg);

    if (ins & 0x04) {
        distOrg = ttf__round(font, distOrg);
    }

    if (ins & 0x08) {
        distOrg = ttf__apply_min_dist(font, distOrg);
    }

    if (ins & 0x10) {
        font->gState.rp0 = pointIdx;
    }

    ttf__move_point(font, font->gState.zp1, pointIdx, distOrg - distCur);

    TTF_LOG_POINT(*pointCur, TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__MIAP(TTF* font, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    TTF_uint32 cvtIdx   = ttf__stack_pop_uint32(font);
    TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);

    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));
    assert(pointIdx < font->gState.zp0->cap);

    TTF_F26Dot6 newDist = font->cur.instance->cvt[cvtIdx];

    if (font->gState.zp0 == &font->cur.instance->zone0) {
        font->gState.zp0->orgScaled[pointIdx].x = 
            TTF_FIX_MUL(newDist, font->gState.freedomVec.x, 14);

        font->gState.zp0->orgScaled[pointIdx].y = 
            TTF_FIX_MUL(newDist, font->gState.freedomVec.y, 14);

        font->gState.zp0->cur[pointIdx] = font->gState.zp0->orgScaled[pointIdx];
    }

    TTF_F26Dot6 curDist = 
        ttf__fix_v2_dot(font->gState.zp0->cur + pointIdx, &font->gState.projVec, 14);
    
    if (ins & 0x1) {
        if (labs(newDist - curDist) > font->gState.controlValueCutIn) {
            newDist = curDist;
        }
        newDist = ttf__round(font, newDist);
    }

    ttf__move_point(font, font->gState.zp0, pointIdx, newDist - curDist);
    
    font->gState.rp0 = pointIdx;
    font->gState.rp1 = pointIdx;

    TTF_LOG_POINT(font->gState.zp0->cur[pointIdx], TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__MINDEX(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    TTF_uint32 idx  = font->stack.count - font->stack.frames[font->stack.count - 1].uValue - 1;
    size_t     size = sizeof(TTF_Stack_Frame) * (font->stack.count - idx - 1);

    font->stack.count--;
    font->stack.frames[font->stack.count] = font->stack.frames[idx];
    memmove(font->stack.frames + idx, font->stack.frames + idx + 1, size);
}

static void ttf__MIRP(TTF* font, TTF_uint8 ins) {
    // Note: There is a lot of undocumented stuff involving this instruction

    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    TTF_uint32 cvtIdx   = ttf__stack_pop_uint32(font);
    TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);

    assert(cvtIdx   < font->cvt.size / sizeof(TTF_FWORD));
    assert(pointIdx < font->gState.zp1->cap);

    TTF_F26Dot6 cvtVal = font->cur.instance->cvt[cvtIdx];
    cvtVal = ttf__apply_single_width_cut_in(font, cvtVal);

    TTF_F26Dot6_V2* rp0Org = font->gState.zp0->orgScaled + font->gState.rp0;
    TTF_F26Dot6_V2* rp0Cur = font->gState.zp0->cur       + font->gState.rp0;

    TTF_F26Dot6_V2* pointOrg = font->gState.zp1->orgScaled + pointIdx;
    TTF_F26Dot6_V2* pointCur = font->gState.zp1->cur       + pointIdx;

    if (font->gState.zp1 == &font->cur.instance->zone0) {
        pointOrg->x = rp0Org->x + TTF_FIX_MUL(cvtVal, font->gState.freedomVec.x, 14);
        pointOrg->y = rp0Org->y + TTF_FIX_MUL(cvtVal, font->gState.freedomVec.y, 14);
        *pointCur   = *pointOrg;
    }

    TTF_int32 distOrg = ttf__fix_v2_sub_dot(pointOrg, rp0Org, &font->gState.dualProjVec, 14);
    TTF_int32 distCur = ttf__fix_v2_sub_dot(pointCur, rp0Cur, &font->gState.projVec, 14);

    if (font->gState.autoFlip) {
        if ((distOrg ^ cvtVal) < 0) {
            // Match the sign of distOrg
            cvtVal = -cvtVal;
        }
    }

    TTF_int32 distNew;

    if (ins & 0x04) {
        if (font->gState.zp0 == font->gState.zp1) {
            if (labs(cvtVal - distOrg) > font->gState.controlValueCutIn) {
                cvtVal = distOrg;
            }
        }
        distNew = ttf__round(font, cvtVal);
    }
    else {
        distNew = cvtVal;
    }

    if (ins & 0x08) {
        distNew = ttf__apply_min_dist(font, distNew);
    }

    ttf__move_point(font, font->gState.zp1, pointIdx, distNew - distCur);

    font->gState.rp1 = font->gState.rp0;
    font->gState.rp2 = pointIdx;

    if (ins & 0x10) {
        font->gState.rp0 = pointIdx;
    }

    TTF_LOG_POINT(*pointCur, TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__MPPEM(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    ttf__stack_push_uint32(font, font->cur.instance->ppem);
    TTF_LOG_VALUE(font->cur.instance->ppem, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__MUL(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_F26Dot6 n1     = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 n2     = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 result = TTF_FIX_MUL(n1, n2, 6);
    ttf__stack_push_F26Dot6(font, result);
    TTF_LOG_VALUE(result, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__NEG(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_F26Dot6 val = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, -val);
    TTF_LOG_VALUE(-val, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__NEQ(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_int32 e2 = ttf__stack_pop_uint32(font);
    TTF_int32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 != e2 ? 1 : 0);
    TTF_LOG_VALUE(e1 != e2, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__NPUSHB(TTF* font, TTF_Ins_Stream* stream) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    TTF_uint8 count = ttf__ins_stream_next(stream);

    do {
        TTF_uint8 byte = ttf__ins_stream_next(stream);
        ttf__stack_push_uint32(font, byte);
    } while (--count);
}

static void ttf__NPUSHW(TTF* font, TTF_Ins_Stream* stream) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    TTF_uint8 count = ttf__ins_stream_next(stream);

    do {
        TTF_uint8 ms  = ttf__ins_stream_next(stream);
        TTF_uint8 ls  = ttf__ins_stream_next(stream);
        TTF_int32 val = (ms << 8) | ls;
        ttf__stack_push_int32(font, val);
    } while (--count);
}

static void ttf__OR(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_int32 e1 = ttf__stack_pop_int32(font);
    TTF_int32 e2 = ttf__stack_pop_int32(font);
    ttf__stack_push_uint32(font, (e1 != 0 || e2 != 0) ? 1 : 0);
    TTF_LOG_VALUE((e1 != 0 || e2 != 0), TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__POP(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    ttf__stack_pop_uint32(font);
}

static void ttf__PUSHB(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    TTF_uint8 count = ttf__get_num_vals_to_push(ins);

    do {
        TTF_uint8 byte = ttf__ins_stream_next(stream);
        ttf__stack_push_uint32(font, byte);
    } while (--count);
}

static void ttf__PUSHW(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    TTF_uint8 count = ttf__get_num_vals_to_push(ins);

    do {
        TTF_uint8 ms  = ttf__ins_stream_next(stream);
        TTF_uint8 ls  = ttf__ins_stream_next(stream);
        TTF_int32 val = (ms << 8) | ls;
        ttf__stack_push_int32(font, val);
    } while (--count);
}

static void ttf__RCVT(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    ttf__stack_push_F26Dot6(font, font->cur.instance->cvt[cvtIdx]);
    TTF_LOG_VALUE(font->cur.instance->cvt[cvtIdx], TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__RDTG(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.roundState = TTF_ROUND_DOWN_TO_GRID;
}

static void ttf__ROLL(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 a = ttf__stack_pop_uint32(font);
    TTF_uint32 b = ttf__stack_pop_uint32(font);
    TTF_uint32 c = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, b);
    ttf__stack_push_uint32(font, a);
    ttf__stack_push_uint32(font, c);
}

static void ttf__ROUND(TTF* font, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    TTF_F26Dot6 dist = ttf__stack_pop_F26Dot6(font);
    dist = ttf__round(font, dist);
    ttf__stack_push_F26Dot6(font, dist);
    TTF_LOG_VALUE(dist, TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__RS(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 idx = ttf__stack_pop_uint32(font);
    ttf__stack_push_int32(font, font->cur.instance->storageArea[idx]);
    TTF_LOG_VALUE(font->cur.instance->storageArea[idx], TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__RTG(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.roundState = TTF_ROUND_TO_GRID;
}

static void ttf__RTHG(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.roundState = TTF_ROUND_TO_HALF_GRID;
}

static void ttf__RUTG(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.roundState = TTF_ROUND_UP_TO_GRID;
}

static void ttf__SCANCTRL(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);

    TTF_uint16 flags  = ttf__stack_pop_uint32(font);
    TTF_uint8  thresh = flags & 0xFF;
    
    if (thresh == 0xFF) {
        font->gState.scanControl = TTF_TRUE;
    }
    else if (thresh == 0x0) {
        font->gState.scanControl = TTF_FALSE;
    }
    else {
        if (flags & 0x100) {
            if (font->cur.instance->ppem <= thresh) {
                font->gState.scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x200) {
            if (font->cur.instance->isRotated) {
                font->gState.scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x400) {
            if (font->cur.instance->isStretched) {
                font->gState.scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x800) {
            if (thresh > font->cur.instance->ppem) {
                font->gState.scanControl = TTF_FALSE;
            }
        }

        if (flags & 0x1000) {
            if (!font->cur.instance->isRotated) {
                font->gState.scanControl = TTF_FALSE;
            }
        }

        if (flags & 0x2000) {
            if (!font->cur.instance->isStretched) {
                font->gState.scanControl = TTF_FALSE;
            }
        }
    }

    TTF_LOG_VALUE(font->gState.scanControl, TTF_LOG_LEVEL_MINIMAL);
}

static void ttf__SCANTYPE(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.scanType = ttf__stack_pop_uint32(font);
    TTF_LOG_VALUE(font->gState.scanType, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SCVTCI(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.controlValueCutIn = ttf__stack_pop_F26Dot6(font);
    TTF_LOG_VALUE(font->gState.controlValueCutIn, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SDB(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.deltaBase = ttf__stack_pop_uint32(font);
    TTF_LOG_VALUE(font->gState.deltaBase, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SDS(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.deltaShift = ttf__stack_pop_uint32(font);
    TTF_LOG_VALUE(font->gState.deltaShift, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SHPIX(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_MINIMAL);
    
    TTF_F26Dot6_V2 dist;
    {
        TTF_F26Dot6 amt = ttf__stack_pop_F26Dot6(font);
        dist.x = TTF_FIX_MUL(amt, font->gState.freedomVec.x, 14);
        dist.y = TTF_FIX_MUL(amt, font->gState.freedomVec.y, 14);
    }

    for (TTF_uint32 i = 0; i < font->gState.loop; i++) {
        TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);
        assert(pointIdx <= font->gState.zp2->cap);

        font->gState.zp2->cur[pointIdx].x      += dist.x;
        font->gState.zp2->cur[pointIdx].y      += dist.y;
        font->gState.zp2->touchFlags[pointIdx] |= font->gState.touchFlags;

        TTF_LOG_POINT(font->gState.zp2->cur[pointIdx], TTF_LOG_LEVEL_MINIMAL);
    }

    font->gState.loop = 1;
}

static void ttf__SLOOP(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.loop = ttf__stack_pop_uint32(font);
    TTF_LOG_VALUE(font->gState.loop, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SRP0(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.rp0 = ttf__stack_pop_uint32(font);
    TTF_LOG_VALUE(font->gState.rp0, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SRP1(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.rp1 = ttf__stack_pop_uint32(font);
    TTF_LOG_VALUE(font->gState.rp1, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SRP2(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    font->gState.rp2 = ttf__stack_pop_uint32(font);
    TTF_LOG_VALUE(font->gState.rp2, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SUB(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_F26Dot6 n1 = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 n2 = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, n2 - n1);
    TTF_LOG_VALUE(n2 - n1, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SVTCA(TTF* font, TTF_uint8 ins) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    if (ins & 0x1) {
        font->gState.freedomVec.x = 1l << 14;
        font->gState.freedomVec.y = 0;
        font->gState.touchFlags   = TTF_TOUCH_X;
    }
    else {
        font->gState.freedomVec.x = 0;
        font->gState.freedomVec.y = 1l << 14;
        font->gState.touchFlags   = TTF_TOUCH_Y;
    }

    font->gState.projVec     = font->gState.freedomVec;
    font->gState.dualProjVec = font->gState.freedomVec;
}

static void ttf__SWAP(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e2);
    ttf__stack_push_uint32(font, e1);
}

static void ttf__SZPS(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 zone = ttf__stack_pop_uint32(font);
    font->gState.zp0 = ttf__get_zone_pointer(font, zone);
    font->gState.zp1 = font->gState.zp0;
    font->gState.zp2 = font->gState.zp0;
    TTF_LOG_VALUE(zone, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SZP0(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 zone = ttf__stack_pop_uint32(font);
    font->gState.zp0 = ttf__get_zone_pointer(font, zone);
    TTF_LOG_VALUE(zone, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SZP1(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 zone = ttf__stack_pop_uint32(font);
    font->gState.zp1 = ttf__get_zone_pointer(font, zone);
    TTF_LOG_VALUE(zone, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__SZP2(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_uint32 zone = ttf__stack_pop_uint32(font);
    font->gState.zp2 = ttf__get_zone_pointer(font, zone);
    TTF_LOG_VALUE(zone, TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__WCVTF(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    TTF_uint32 funits = ttf__stack_pop_uint32(font);
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    font->cur.instance->cvt[cvtIdx] = TTF_FIX_MUL(funits << 6, font->cur.instance->scale, 22);

    TTF_LOG_VALUE(font->cur.instance->cvt[cvtIdx], TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__WCVTP(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);

    TTF_uint32 pixels = ttf__stack_pop_uint32(font);
    
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    font->cur.instance->cvt[cvtIdx] = pixels;
    TTF_LOG_VALUE(font->cur.instance->cvt[cvtIdx], TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__WS(TTF* font) {
    TTF_LOG_INS(TTF_LOG_LEVEL_VERBOSE);
    TTF_int32  value = ttf__stack_pop_int32(font);
    TTF_uint32 idx   = ttf__stack_pop_uint32(font);
    font->cur.instance->storageArea[idx] = value;
    TTF_LOG_VALUE(font->cur.instance->storageArea[idx], TTF_LOG_LEVEL_VERBOSE);
}

static void ttf__initialize_graphics_state(TTF* font) {
    font->gState.autoFlip          = TTF_TRUE;
    font->gState.controlValueCutIn = 68;
    font->gState.deltaBase         = 9;
    font->gState.deltaShift        = 3;
    font->gState.dualProjVec.x     = 1l << 14;
    font->gState.dualProjVec.y     = 0;
    font->gState.freedomVec.x      = 1l << 14;
    font->gState.freedomVec.y      = 0;
    font->gState.loop              = 1;
    font->gState.minDist           = 1l << 6;
    font->gState.projVec.x         = 1l << 14;
    font->gState.projVec.y         = 0;
    font->gState.rp0               = 0;
    font->gState.rp1               = 0;
    font->gState.rp2               = 0;
    font->gState.roundState        = TTF_ROUND_TO_GRID;
    font->gState.scanControl       = TTF_FALSE;
    font->gState.singleWidthCutIn  = 0;
    font->gState.singleWidthValue  = 0;
    font->gState.touchFlags        = TTF_TOUCH_X;
    font->gState.zp0               = &font->cur.zone1;
    font->gState.zp1               = &font->cur.zone1;
    font->gState.zp2               = &font->cur.zone1;
}

static void ttf__call_func(TTF* font, TTF_uint32 funcId, TTF_uint32 times) {
    assert(funcId < font->funcArray.count);

    while (times > 0) {
        TTF_Ins_Stream stream;
        ttf__ins_stream_init(&stream, font->funcArray.funcs[funcId].firstIns);

        while (TTF_TRUE) {
            TTF_uint8 ins = ttf__ins_stream_next(&stream);
            
            if (ins == TTF_ENDF) {
                break;
            }

            ttf__execute_ins(font, &stream, ins);
        };

        times--;
    }
}

static TTF_uint8 ttf__jump_to_else_or_eif(TTF_Ins_Stream* stream) {
    TTF_uint32 numNested = 0;

    while (TTF_TRUE) {
        TTF_uint8 ins = ttf__ins_stream_next(stream);

        if (ins >= TTF_PUSHB && ins <= TTF_PUSHB_MAX) {
            ttf__ins_stream_jump(stream, ttf__get_num_vals_to_push(ins));
        }
        else if (ins >= TTF_PUSHW && ins <= TTF_PUSHW_MAX) {
            ttf__ins_stream_jump(stream, 2 * ttf__get_num_vals_to_push(ins));
        }
        else if (ins == TTF_NPUSHB) {
            ttf__ins_stream_jump(stream, ttf__ins_stream_next(stream));
        }
        else if (ins == TTF_NPUSHW) {
            ttf__ins_stream_jump(stream, 2 * ttf__ins_stream_next(stream));
        }
        else if (ins == TTF_IF) {
            numNested++;
        }
        else if (numNested == 0) {
            if (ins == TTF_EIF || ins == TTF_ELSE) {
                return ins;
            }
        }
        else if (ins == TTF_EIF) {
            numNested--;
        }
    }

    assert(0);
    return 0;
}

static TTF_F26Dot6 ttf__round(TTF* font, TTF_F26Dot6 val) {
    // TODO: No idea how to apply "engine compensation" described in the spec

    switch (font->gState.roundState) {
        case TTF_ROUND_TO_HALF_GRID:
            return (val & 0x3F) | 0x20;
        case TTF_ROUND_TO_GRID:
            return ttf__f26dot6_round(val);
        case TTF_ROUND_TO_DOUBLE_GRID:
            // TODO
            assert(0);
            break;
        case TTF_ROUND_DOWN_TO_GRID:
            return ttf__f26dot6_floor(val);
        case TTF_ROUND_UP_TO_GRID:
            return ttf__f26dot6_ceil(val);
        case TTF_ROUND_OFF:
            // TODO
            assert(0);
            break;
    }
    assert(0);
    return 0;
}

static void ttf__move_point(TTF* font, TTF_Zone* zone, TTF_uint32 idx, TTF_F26Dot6 amount) {
    zone->cur[idx].x         += TTF_FIX_MUL(amount, font->gState.freedomVec.x, 14);
    zone->cur[idx].y         += TTF_FIX_MUL(amount, font->gState.freedomVec.y, 14);
    zone->touchFlags[idx] |= font->gState.touchFlags;
}

static TTF_F26Dot6 ttf__apply_single_width_cut_in(TTF* font, TTF_F26Dot6 value) {
    TTF_F26Dot6 absDiff = labs(value - font->gState.singleWidthValue);
    if (absDiff < font->gState.singleWidthCutIn) {
        if (value < 0) {
            return -font->gState.singleWidthValue;
        }
        return font->gState.singleWidthValue;
    }
    return value;
}

static TTF_F26Dot6 ttf__apply_min_dist(TTF* font, TTF_F26Dot6 value) {
    if (labs(value) < font->gState.minDist) {
        if (value < 0) {
            return -font->gState.minDist;
        }
        return font->gState.minDist;
    }
    return value;
}

static TTF_bool ttf__get_delta_value(TTF* font, TTF_uint32 exc, TTF_uint8 range, TTF_F26Dot6* deltaVal) {
    TTF_uint32 ppem = ((exc & 0xF0) >> 4) + font->gState.deltaBase + range;

    if (font->cur.instance->ppem != ppem) {
        return TTF_FALSE;
    }

    TTF_int8 numSteps = (exc & 0xF) - 8;
    if (numSteps > 0) {
        numSteps++;
    }

    // The result is 26.6 since numSteps already has a scale factor of 1
    *deltaVal = numSteps * (1l << (6 - font->gState.deltaShift));
    return TTF_TRUE;
}

static void ttf__IUP_interpolate_or_shift(TTF_Zone* zone1, TTF_Touch_Flag touchFlag, TTF_uint16 startPointIdx, TTF_uint16 endPointIdx, TTF_uint16 touch0, TTF_uint16 touch1) {
    #define TTF_IUP_INTERPOLATE(coord)                                                       \
        TTF_F26Dot6 totalDistCur = zone1->cur[touch1].coord - zone1->cur[touch0].coord;      \
        TTF_int32   totalDistOrg = zone1->org[touch1].coord - zone1->org[touch0].coord;      \
        TTF_int32   orgDist      = zone1->org[i].coord      - zone1->org[touch0].coord;      \
                                                                                             \
        TTF_F10Dot22 scale   = ttf__rounded_div((TTF_int64)totalDistCur << 16, totalDistOrg);\
        TTF_F26Dot6  newDist = TTF_FIX_MUL(orgDist << 6, scale, 22);                         \
        zone1->cur[i].coord  = zone1->cur[touch0].coord + newDist;                           \
                                                                                             \
        TTF_LOG_CUSTOM_F(TTF_LOG_LEVEL_MINIMAL, "Interp %3d: %5d", i, zone1->cur[i].coord);

    #define TTF_IUP_SHIFT(coord)\
        TTF_int32 diff0 = labs(zone1->org[touch0].coord - zone1->org[i].coord);           \
        TTF_int32 diff1 = labs(zone1->org[touch1].coord - zone1->org[i].coord);           \
                                                                                          \
        if (diff0 < diff1) {                                                              \
            TTF_int32 diff = zone1->cur[touch0].coord - zone1->orgScaled[touch0].coord;   \
            zone1->cur[i].coord += diff;                                                  \
        }                                                                                 \
        else {                                                                            \
            TTF_int32 diff = zone1->cur[touch1].coord - zone1->orgScaled[touch1].coord;   \
            zone1->cur[i].coord += diff;                                                  \
        }                                                                                 \
        TTF_LOG_CUSTOM_F(TTF_LOG_LEVEL_MINIMAL, "Shift %3d: %5d", i, zone1->cur[i].coord);

    #define TTF_IUP_INTERPOLATE_OR_SHIFT                                 \
        if (touchFlag == TTF_TOUCH_X) {                                  \
            if (coord0 <= zone1->org[i].x && zone1->org[i].x <= coord1) {\
                TTF_IUP_INTERPOLATE(x);                                  \
            }                                                            \
            else {                                                       \
                TTF_IUP_SHIFT(x)                                         \
            }                                                            \
        }                                                                \
        else {                                                           \
            if (coord0 <= zone1->org[i].y && zone1->org[i].y <= coord1) {\
                TTF_IUP_INTERPOLATE(y);                                  \
            }                                                            \
            else {                                                       \
                TTF_IUP_SHIFT(y)                                         \
            }                                                            \
        }

    TTF_int32 coord0, coord1;

    if (touchFlag == TTF_TOUCH_X) {
        ttf__max_min(zone1->org[touch0].x, zone1->org[touch1].x, &coord1, &coord0);
    }
    else {
        ttf__max_min(zone1->org[touch0].y, zone1->org[touch1].y, &coord1, &coord0);
    }

    if (touch0 >= touch1) {
        for (TTF_uint32 i = touch0 + 1; i <= endPointIdx; i++) {
            TTF_IUP_INTERPOLATE_OR_SHIFT
        }

        for (TTF_uint32 i = startPointIdx; i < touch1; i++) {
            TTF_IUP_INTERPOLATE_OR_SHIFT
        } 
    }
    else {
        for (TTF_uint32 i = touch0 + 1; i < touch1; i++) {
            TTF_IUP_INTERPOLATE_OR_SHIFT
        }
    }

    #undef TTF_IUP_SHIFT
    #undef TTF_IUP_INTERPOLATE_OR_SHIFT
}


/* ------------------ */
/* Utility Operations */
/* ------------------ */
static TTF_uint16 ttf__get_uint16(TTF_uint8* data) {
    return data[0] << 8 | data[1];
}

static TTF_uint32 ttf__get_uint32(TTF_uint8* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static TTF_int16 ttf__get_int16(TTF_uint8* data) {
    return data[0] << 8 | data[1];
}

static void ttf__max_min(TTF_int32 a, TTF_int32 b, TTF_int32* max, TTF_int32* min) {
    if (a > b) {
        *max = a;
        *min = b;
    }
    else {
        *max = b;
        *min = a;
    }
}

static TTF_int32 ttf__min(TTF_int32 a, TTF_int32 b) {
    return a < b ? a : b;
}

static TTF_int32 ttf__max(TTF_int32 a, TTF_int32 b) {
    return a > b ? a : b;
}

static TTF_uint16 ttf__get_upem(TTF* font) {
    return ttf__get_uint16(font->data + font->head.off + 18);
}

static TTF_uint16 ttf__get_glyph_x_advance(TTF* font, TTF_uint32 glyphIdx) {
    TTF_uint8* hmtxData    = font->data + font->hmtx.off;
    TTF_uint16 numHMetrics = ttf__get_uint16(font->data + font->hhea.off + 48);
    if (glyphIdx < numHMetrics) {
        return ttf__get_uint16(hmtxData + 4 * glyphIdx);
    }
    return 0;
}

static TTF_int16 ttf__get_glyph_left_side_bearing(TTF* font, TTF_uint32 glyphIdx) {
    TTF_uint8* hmtxData    = font->data + font->hmtx.off;
    TTF_uint16 numHMetrics = ttf__get_uint16(font->data + font->hhea.off + 48);
    if (glyphIdx < numHMetrics) {
        return ttf__get_int16(hmtxData + 4 * glyphIdx + 2);
    }
    return ttf__get_int16(hmtxData + 4 * numHMetrics + 2 * (numHMetrics - glyphIdx));
}

static void ttf__get_glyph_vmetrics(TTF* font, TTF_uint8* glyfBlock, TTF_int32* ah, TTF_int32* tsb) {
    if (font->vmtx.exists) {
        // TODO: Get vertical phantom points from vmtx
        assert(0);
    }
    else  {
        TTF_int16 yMax = ttf__get_int16(glyfBlock + 8);
        *ah  = font->ascender - font->descender;
        *tsb = font->ascender - yMax;
    }
}

static TTF_uint16 ttf__get_cvt_cap(TTF* font) {
    return font->cvt.size / sizeof(TTF_FWORD);
}

static TTF_uint16 ttf__get_storage_area_cap(TTF* font) {
    // TODO: make sure maxp has version 1.0
    return ttf__get_uint16(font->data + font->maxp.off + 18);   
}

static TTF_Zone* ttf__get_zone_pointer(TTF* font, TTF_uint32 zone) {
    switch (zone) {
        case 0:
            return &font->cur.instance->zone0;
        case 1:
            return &font->cur.zone1;
    }
    assert(0);
    return NULL;
}


/* ---------------- */
/* Fixed-point Math */
/* ---------------- */
static TTF_int64 ttf__rounded_div(TTF_int64 a, TTF_int64 b) {
    // https://stackoverflow.com/a/18067292
    if (b == 0) {
        return 0;
    }
    return (a < 0) ^ (b < 0) ? (a - b / 2) / b : (a + b / 2) / b;
}

/* The result has a scale factor of 1 << (shift(a) - shift(b) + shift) */
static TTF_int32 ttf__fix_div(TTF_int32 a, TTF_int32 b, TTF_uint8 shift) {
    TTF_int64 q = ttf__rounded_div((TTF_int64)a << 31, b);
    shift = 31 - shift;
    return TTF_ROUNDED_DIV_POW2(q, shift);
}

/* a and b must have the same scale factor */
static void ttf__fix_v2_add(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result) {
    result->x = a->x + b->x;
    result->y = a->y + b->y;
}

/* a and b must have the same scale factor */
static void ttf__fix_v2_sub(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result) {
    result->x = a->x - b->x;
    result->y = a->y - b->y;
}

static void ttf__fix_v2_mul(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result, TTF_uint8 shift) {
    result->x = TTF_FIX_MUL(a->x, b->x, shift);
    result->y = TTF_FIX_MUL(a->y, b->y, shift);
}

static void ttf__fix_v2_div(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result, TTF_uint8 shift) {
    result->x = ttf__fix_div(a->x, b->x, shift);
    result->y = ttf__fix_div(a->y, b->y, shift);
}

static TTF_int32 ttf__fix_v2_dot(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_uint8 shift) {
    return TTF_FIX_MUL(a->x, b->x, shift) + TTF_FIX_MUL(a->y, b->y, shift);
}

/* dot(a - b, c) */
static TTF_int32 ttf__fix_v2_sub_dot(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* c, TTF_uint8 shift) {
    TTF_Fix_V2 diff;
    ttf__fix_v2_sub(a, b, &diff);
    return ttf__fix_v2_dot(&diff, c, shift);
}

static void ttf__fix_v2_scale(TTF_Fix_V2* v, TTF_int32 scale, TTF_uint8 shift) {
    v->x = TTF_FIX_MUL(v->x, scale, shift);
    v->y = TTF_FIX_MUL(v->y, scale, shift);
}

static TTF_F26Dot6 ttf__f26dot6_round(TTF_F26Dot6 val) {
    return ((val & 0x20) << 1) + (val & 0xFFFFFFC0);
}

static TTF_F26Dot6 ttf__f26dot6_ceil(TTF_F26Dot6 val) {
    return (val & 0x3F) ? (val & 0xFFFFFFC0) + 0x40 : val;
}

static TTF_F26Dot6 ttf__f26dot6_floor(TTF_F26Dot6 val) {
    return val & 0xFFFFFFC0;
}

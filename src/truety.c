#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "truety.h"

/* ------------------ */
/* Convenience Macros */
/* ------------------ */
#define TTY_TEMP     font->temp
#define TTY_INSTANCE font->temp.instance


/* ------------------------- */
/* Initialization Operations */
/* ------------------------- */
static TTY_bool tty__read_file_into_buffer(TTY* font, const char* path);

static TTY_bool tty__extract_info_from_table_directory(TTY* font);

static TTY_bool tty__extract_char_encoding(TTY* font);

static TTY_bool tty__format_is_supported(TTY_uint16 format);

static TTY_bool tty__alloc_mem_for_ins_processing(TTY* font);


/* -------------------- */
/* Rendering Operations */
/* -------------------- */
#define TTY_SUBDIVIDE_SQRD_ERROR 0x1  /* 26.6 */
#define TTY_EDGES_PER_CHUNK      10
#define TTY_PIXELS_PER_SCANLINE  0x10 /* 26.6 */

typedef struct {
    TTY_F26Dot6_V2 p0;
    TTY_F26Dot6_V2 p1; /* Control point */
    TTY_F26Dot6_V2 p2;
} TTY_Curve;

typedef struct {
    TTY_F26Dot6_V2 p0;
    TTY_F26Dot6_V2 p1;
    TTY_F26Dot6    yMin;
    TTY_F26Dot6    yMax;
    TTY_F26Dot6    xMin;
    TTY_F16Dot16   invSlope; /* TODO: Should this be 26.6? */
    TTY_int8       dir;
} TTY_Edge;

typedef struct TTY_Active_Edge {
    TTY_Edge*               edge;
    TTY_F26Dot6             xIntersection;
    struct TTY_Active_Edge* next;
} TTY_Active_Edge;

typedef struct TTY_Active_Chunk {
    TTY_Active_Edge          edges[TTY_EDGES_PER_CHUNK];
    TTY_uint32               numEdges;
    struct TTY_Active_Chunk* next;
} TTY_Active_Chunk;

typedef struct {
    TTY_Active_Chunk* headChunk;
    TTY_Active_Edge*  headEdge;
    TTY_Active_Edge*  reusableEdges;
} TTY_Active_Edge_List;

static TTY_Edge* tty__get_glyph_edges(TTY* font, TTY_uint32* numEdges);

static void tty__convert_points_to_bitmap_space(TTY* font, TTY_F26Dot6_V2* points);

static TTY_Curve* tty__convert_points_into_curves(TTY*            font, 
                                                  TTY_F26Dot6_V2* points, 
                                                  TTY_Point_Type* pointTypes, 
                                                  TTY_uint32*     numCurves);

static TTY_Edge* tty__subdivide_curves_into_edges(TTY*        font, 
                                                  TTY_Curve*  curves, 
                                                  TTY_uint32  numCurves, 
                                                  TTY_uint32* numEdges);

static void tty__subdivide_curve_into_edges(TTY_F26Dot6_V2* p0, 
                                            TTY_F26Dot6_V2* p1, 
                                            TTY_F26Dot6_V2* p2, 
                                            TTY_int8        dir, 
                                            TTY_Edge*       edges, 
                                            TTY_uint32*     numEdges);

static void tty__edge_init(TTY_Edge* edge, TTY_F26Dot6_V2* p0, TTY_F26Dot6_V2* p1, TTY_int8 dir);

static TTY_F10Dot22 tty__get_inv_slope(TTY_F26Dot6_V2* p0, TTY_F26Dot6_V2* p1);

static int tty__compare_edges(const void* edge0, const void* edge1);

static TTY_F26Dot6 tty__get_edge_scanline_x_intersection(TTY_Edge* edge, TTY_F26Dot6 scanline);

static TTY_bool tty__active_edge_list_init(TTY_Active_Edge_List* list);

static void tty__active_edge_list_free(TTY_Active_Edge_List* list);

static TTY_Active_Edge* tty__get_available_active_edge(TTY_Active_Edge_List* list);

static TTY_Active_Edge* tty__insert_active_edge_first(TTY_Active_Edge_List* list);

static TTY_Active_Edge* tty__insert_active_edge_after(TTY_Active_Edge_List* list, 
                                                      TTY_Active_Edge*      after);

static void tty__remove_active_edge(TTY_Active_Edge_List* list, 
                                    TTY_Active_Edge*      prev, 
                                    TTY_Active_Edge*      remove);

static void tty__swap_active_edge_with_next(TTY_Active_Edge_List* list, 
                                            TTY_Active_Edge*      prev, 
                                            TTY_Active_Edge*      edge);


/* ------------------- */
/* Glyph Index Mapping */
/* ------------------- */
static TTY_uint16 tty__get_glyph_index_format_4(TTY_uint8* subtable, TTY_uint32 cp);


/* --------------------- */
/* glyf Table Operations */
/* --------------------- */
enum {
    TTY_GLYF_ON_CURVE_POINT = 0x01,
    TTY_GLYF_X_SHORT_VECTOR = 0x02,
    TTY_GLYF_Y_SHORT_VECTOR = 0x04,
    TTY_GLYF_REPEAT_FLAG    = 0x08,
    TTY_GLYF_X_DUAL         = 0x10, /* X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR */
    TTY_GLYF_Y_DUAL         = 0x20, /* Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR */
    TTY_GLYF_OVERLAP_SIMPLE = 0x40,
    TTY_GLYF_RESERVED       = 0x80,
};

static TTY_bool tty__get_glyf_data_block(TTY* font, TTY_uint8** block, TTY_uint32 glyphIdx);

static TTY_bool tty__extract_glyph_points(TTY* font);

static TTY_int32 tty__get_next_coord_off(TTY_uint8** data, 
                                         TTY_uint8   dualFlag, 
                                         TTY_uint8   shortFlag, 
                                         TTY_uint8   flags);


/* ---------------------------- */
/* Interpreter Stack Operations */
/* ---------------------------- */
#define tty__stack_push_F2Dot14(font, val) tty__stack_push_int32(font, val)
#define tty__stack_push_F26Dot6(font, val) tty__stack_push_int32(font, val)
#define tty__stack_pop_F2Dot14(font)       tty__stack_pop_int32(font)
#define tty__stack_pop_F26Dot6(font)       tty__stack_pop_int32(font)

static void tty__stack_push_uint32(TTY* font, TTY_uint32 val);

static void tty__stack_push_int32(TTY* font, TTY_int32  val);

static TTY_uint32 tty__stack_pop_uint32(TTY* font);

static TTY_int32 tty__stack_pop_int32(TTY* font);

static void tty__stack_clear(TTY* font);

/* For PUSHB and PUSHW */
static TTY_uint8 tty__get_num_vals_to_push(TTY_uint8 ins);


/* ----------------------------- */
/* Instruction Stream Operations */
/* ----------------------------- */
typedef struct {
    TTY_uint8* bytes;
    TTY_uint32 off;
} TTY_Ins_Stream;

static void tty__ins_stream_init(TTY_Ins_Stream* stream, TTY_uint8* bytes);

static TTY_uint8 tty__ins_stream_next(TTY_Ins_Stream* stream);

static void tty__ins_stream_jump(TTY_Ins_Stream* stream, TTY_int32 count);


/* --------------------- */
/* Instruction Execution */
/* --------------------- */
#define TTY_SCALAR_VERSION     35
#define TTY_NUM_PHANTOM_POINTS  4

enum {
    TTY_ABS       = 0x64,
    TTY_ADD       = 0x60,
    TTY_ALIGNRP   = 0x3C,
    TTY_AND       = 0x5A,
    TTY_CALL      = 0x2B,
    TTY_CINDEX    = 0x25,
    TTY_DELTAC1   = 0x73,
    TTY_DELTAC2   = 0x74,
    TTY_DELTAC3   = 0x75,
    TTY_DELTAP1   = 0x5D,
    TTY_DELTAP2   = 0x71,
    TTY_DELTAP3   = 0x72,
    TTY_DEPTH     = 0x24,
    TTY_DIV       = 0x62,
    TTY_DUP       = 0x20,
    TTY_EIF       = 0x59,
    TTY_ELSE      = 0x1B,
    TTY_ENDF      = 0x2D,
    TTY_EQ        = 0x54,
    TTY_FDEF      = 0x2C,
    TTY_FLOOR     = 0x66,
    TTY_GC        = 0x46,
    TTY_GC_MAX    = 0x47,
    TTY_GETINFO   = 0x88,
    TTY_GPV       = 0x0C,
    TTY_GT        = 0x52,
    TTY_GTEQ      = 0x53,
    TTY_IDEF      = 0x89,
    TTY_IF        = 0x58,
    TTY_IP        = 0x39,
    TTY_IUP       = 0x30,
    TTY_IUP_MAX   = 0x31,
    TTY_JROT      = 0x78,
    TTY_JMPR      = 0x1C,
    TTY_LOOPCALL  = 0x2A,
    TTY_LT        = 0x50,
    TTY_LTEQ      = 0x51,
    TTY_MD        = 0x49,
    TTY_MD_MAX    = 0x4A,
    TTY_MDAP      = 0x2E,
    TTY_MDAP_MAX  = 0x2F,
    TTY_MDRP      = 0xC0,
    TTY_MDRP_MAX  = 0xDF,
    TTY_MIAP      = 0x3E,
    TTY_MIAP_MAX  = 0x3F,
    TTY_MINDEX    = 0x26,
    TTY_MIRP      = 0xE0,
    TTY_MIRP_MAX  = 0xFF,
    TTY_MPPEM     = 0x4B,
    TTY_MUL       = 0x63,
    TTY_NEG       = 0x65,
    TTY_NEQ       = 0x55,
    TTY_NPUSHB    = 0x40,
    TTY_NPUSHW    = 0x41,
    TTY_OR        = 0x5B,
    TTY_POP       = 0x21,
    TTY_PUSHB     = 0xB0,
    TTY_PUSHB_MAX = 0xB7,
    TTY_PUSHW     = 0xB8,
    TTY_PUSHW_MAX = 0xBF,
    TTY_RCVT      = 0x45,
    TTY_RDTG      = 0x7D,
    TTY_ROLL      = 0x8A,
    TTY_ROUND     = 0x68,
    TTY_ROUND_MAX = 0x6B,
    TTY_RS        = 0x43,
    TTY_RTG       = 0x18,
    TTY_RTHG      = 0x19,
    TTY_RUTG      = 0x7C,
    TTY_SCANCTRL  = 0x85,
    TTY_SCANTYPE  = 0x8D,
    TTY_SCVTCI    = 0x1D,
    TTY_SDB       = 0x5E,
    TTY_SDS       = 0x5F,
    TTY_SHPIX     = 0x38,
    TTY_SLOOP     = 0x17,
    TTY_SRP0      = 0x10,
    TTY_SRP1      = 0x11,
    TTY_SRP2      = 0x12,
    TTY_SUB       = 0x61,
    TTY_SVTCA     = 0x00,
    TTY_SVTCA_MAX = 0x01,
    TTY_SWAP      = 0x23,
    TTY_SZPS      = 0x16,
    TTY_SZP0      = 0x13,
    TTY_SZP1      = 0x14,
    TTY_SZP2      = 0x15,
    TTY_WCVTF     = 0x70,
    TTY_WCVTP     = 0x44,
    TTY_WS        = 0x42,
};

enum {
    TTY_ROUND_TO_HALF_GRID  ,
    TTY_ROUND_TO_GRID       ,
    TTY_ROUND_TO_DOUBLE_GRID,
    TTY_ROUND_DOWN_TO_GRID  ,
    TTY_ROUND_UP_TO_GRID    ,
    TTY_ROUND_OFF           ,
};

static void tty__execute_font_program(TTY* font);

static void tty__execute_cv_program(TTY* font);

static TTY_bool tty__execute_glyph_program(TTY* font);

static void tty__execute_ins(TTY* font, TTY_Ins_Stream* stream, TTY_uint8 ins);

static void tty__ABS(TTY* font);

static void tty__ADD(TTY* font);

static void tty__ALIGNRP(TTY* font);

static void tty__AND(TTY* font);

static void tty__CALL(TTY* font);

static void tty__CINDEX(TTY* font);

static void tty__DELTAC1(TTY* font);

static void tty__DELTAC2(TTY* font);

static void tty__DELTAC3(TTY* font);

static void tty__DELTAC(TTY* font, TTY_uint8 range);

static void tty__DELTAP1(TTY* font);

static void tty__DELTAP2(TTY* font);

static void tty__DELTAP3(TTY* font);

static void tty__DELTAP(TTY* font, TTY_uint8 range);

static void tty__DEPTH(TTY* font);

static void tty__DIV(TTY* font);

static void tty__DUP(TTY* font);

static void tty__EQ(TTY* font);

static void tty__FDEF(TTY* font, TTY_Ins_Stream* stream);

static void tty__FLOOR(TTY* font);

static void tty__GC(TTY* font, TTY_uint8 ins);

static void tty__GETINFO(TTY* font);

static void tty__GPV(TTY* font);

static void tty__GT(TTY* font);

static void tty__GTEQ(TTY* font);

static void tty__IDEF(TTY* font, TTY_Ins_Stream* stream);

static void tty__IF(TTY* font, TTY_Ins_Stream* stream);

static void tty__IP(TTY* font);

static void tty__IUP(TTY* font, TTY_uint8 ins);

static void tty__JROT(TTY* font, TTY_Ins_Stream* stream);

static void tty__JMPR(TTY* font, TTY_Ins_Stream* stream);

static void tty__LOOPCALL(TTY* font);

static void tty__LT(TTY* font);

static void tty__LTEQ(TTY* font);

static void tty__MD(TTY* font, TTY_uint8 ins);

static void tty__MDAP(TTY* font, TTY_uint8 ins);

static void tty__MDRP(TTY* font, TTY_uint8 ins);

static void tty__MIAP(TTY* font, TTY_uint8 ins);

static void tty__MINDEX(TTY* font);

static void tty__MIRP(TTY* font, TTY_uint8 ins);

static void tty__MPPEM(TTY* font);

static void tty__MUL(TTY* font);

static void tty__NEG(TTY* font);

static void tty__NEQ(TTY* font);

static void tty__NPUSHB(TTY* font, TTY_Ins_Stream* stream);

static void tty__NPUSHW(TTY* font, TTY_Ins_Stream* stream);

static void tty__OR(TTY* font);

static void tty__POP(TTY* font);

static void tty__PUSHB(TTY* font, TTY_Ins_Stream* stream, TTY_uint8 ins);

static void tty__PUSHW(TTY* font, TTY_Ins_Stream* stream, TTY_uint8 ins);

static void tty__RCVT(TTY* font);

static void tty__RDTG(TTY* font);

static void tty__ROLL(TTY* font);

static void tty__ROUND(TTY* font, TTY_uint8 ins);

static void tty__RS(TTY* font);

static void tty__RTG(TTY* font);

static void tty__RTHG(TTY* font);

static void tty__RUTG(TTY* font);

static void tty__SCANCTRL(TTY* font);

static void tty__SCANTYPE(TTY* font);

static void tty__SCVTCI(TTY* font);

static void tty__SDB(TTY* font);

static void tty__SDS(TTY* font);

static void tty__SHPIX(TTY* font);

static void tty__SLOOP(TTY* font);

static void tty__SRP0(TTY* font);

static void tty__SRP1(TTY* font);

static void tty__SRP2(TTY* font);

static void tty__SUB(TTY* font);

static void tty__SVTCA(TTY* font, TTY_uint8 ins);

static void tty__SWAP(TTY* font);

static void tty__SZPS(TTY* font);

static void tty__SZP0(TTY* font);

static void tty__SZP1(TTY* font);

static void tty__SZP2(TTY* font);

static void tty__WCVTF(TTY* font);

static void tty__WCVTP(TTY* font);

static void tty__WS(TTY* font);

static void tty__reset_graphics_state(TTY* font);

static void tty__call_func(TTY* font, TTY_uint32 funcId, TTY_uint32 times);

static TTY_uint8 tty__jump_to_else_or_eif(TTY_Ins_Stream* stream);

static TTY_F26Dot6 tty__round(TTY* font, TTY_F26Dot6 val);

static void tty__move_point(TTY* font, TTY_Zone* zone, TTY_uint32 idx, TTY_F26Dot6 amount);

static TTY_F26Dot6 tty__apply_single_width_cut_in(TTY* font, TTY_F26Dot6 value);

static TTY_F26Dot6 tty__apply_min_dist(TTY* font, TTY_F26Dot6 value);

static TTY_bool tty__get_delta_value(TTY*         font, 
                                     TTY_uint32   exc, 
                                     TTY_uint8    range, 
                                     TTY_F26Dot6* deltaVal);

static void tty__IUP_interpolate_or_shift(TTY_Zone*      zone1, 
                                          TTY_Touch_Flag touchFlag, 
                                          TTY_uint16     startPointIdx, 
                                          TTY_uint16     endPointIdx, 
                                          TTY_uint16     touch0, 
                                          TTY_uint16     touch1);


/* ------------------ */
/* Utility Operations */
/* ------------------ */
#define tty__get_Offset16(data)       tty__get_uint16(data)
#define tty__get_Offset32(data)       tty__get_uint32(data)
#define tty__get_Version16Dot16(data) tty__get_uint32(data)

static TTY_uint16 tty__get_uint16(TTY_uint8* data);

static TTY_uint32 tty__get_uint32(TTY_uint8* data);

static TTY_int16 tty__get_int16(TTY_uint8* data);

static void tty__max_min(TTY_int32 a, TTY_int32 b, TTY_int32* max, TTY_int32* min);

static TTY_int32 tty__min(TTY_int32 a, TTY_int32 b);

static TTY_int32 tty__max(TTY_int32 a, TTY_int32 b);

static TTY_uint16 tty__get_upem(TTY* font);

static TTY_uint16 tty__get_glyph_x_advance(TTY* font, TTY_uint32 glyphIdx);

static TTY_int16 tty__get_glyph_left_side_bearing(TTY* font, TTY_uint32 glyphIdx);

static void tty__get_glyph_vmetrics(TTY*       font, 
                                    TTY_uint8* glyfBlock, 
                                    TTY_int32* ah, 
                                    TTY_int32* tsb);

static TTY_uint16 tty__get_cvt_cap(TTY* font);

static TTY_uint16 tty__get_storage_area_cap(TTY* font);

static TTY_Zone* tty__get_zone_pointer(TTY* font, TTY_uint32 zone);


/* ---------------- */
/* Fixed-point Math */
/* ---------------- */

/* 
 * https://en.wikipedia.org/wiki/Fixed-point_arithmetic
 *
 * If y = 2^n, then the following is true:
 *     round(x/y) = floor(x/y + 0.5) = floor((x + y/2)/y) = (x + 2^(n-1)) >> y 
 */
#define TTY_ROUNDED_DIV_POW2(a, shift) \
    (((a) + (1l << ((shift) - 1))) >> (shift))

/* The result has a scale factor of 1 << (shift(a) + shift(b) - shift) */
#define TTY_FIX_MUL(a, b, shift)\
    TTY_ROUNDED_DIV_POW2((TTY_uint64)(a) * (TTY_uint64)(b), shift)

static TTY_int64 tty__rounded_div(TTY_int64 a, TTY_int64 b);

/* The result has a scale factor of 1 << (shift(a) - shift(b) + shift) */
static TTY_int32 tty__fix_div(TTY_int32 a, TTY_int32 b, TTY_uint8 shift);

/* a and b must have the same scale factor */
static void tty__fix_v2_add(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* result);

/* a and b must have the same scale factor */
static void tty__fix_v2_sub(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* result);

static void tty__fix_v2_mul(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* result, TTY_uint8 shift);

static void tty__fix_v2_div(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* result, TTY_uint8 shift);

static TTY_int32 tty__fix_v2_dot(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_uint8 shift);

/* dot(a - b, c) */
static TTY_int32 tty__fix_v2_sub_dot(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* c, TTY_uint8 shift);

static void tty__fix_v2_scale(TTY_Fix_V2* v, TTY_int32 scale, TTY_uint8 shift);

static TTY_F26Dot6 tty__f26dot6_round(TTY_F26Dot6 val);

static TTY_F26Dot6 tty__f26dot6_ceil(TTY_F26Dot6 val);

static TTY_F26Dot6 tty__f26dot6_floor(TTY_F26Dot6 val);


// #define TTY_DEBUG

#define TTY_LOG_LEVEL_VERBOSE  0
#define TTY_LOG_LEVEL_MINIMAL  1
#define TTY_LOG_LEVEL_CRITICAL 2
#define TTY_LOG_LEVEL_NONE     3
#define TTY_LOG_LEVEL          TTY_LOG_LEVEL_NONE

#ifdef TTY_DEBUG
    static int count = 0;

    #define TTY_LOG_UNKNOWN_INS(ins)\
        if (TTY_LOG_LEVEL_CRITICAL >= TTY_LOG_LEVEL) printf("Unknown instruction: %#X\n", ins)

    #define TTY_LOG_PROGRAM(program)\
        if (TTY_LOG_LEVEL_MINIMAL >= TTY_LOG_LEVEL) printf("\n--- %s ---\n", program)

    #define TTY_LOG_INS(level)\
        if (level >= TTY_LOG_LEVEL) printf("%d) %s\n", count++, __func__ + 5)

    #define TTY_LOG_POINT(point, level)\
        if (level >= TTY_LOG_LEVEL) printf("\t(%d, %d)\n", (point).x, (point).y)

    #define TTY_LOG_VALUE(value, level)\
        if (level >= TTY_LOG_LEVEL) printf("\t%d\n", value)

    #define TTY_LOG_CUSTOM_F(level, format, ...)\
        if (level >= TTY_LOG_LEVEL) printf("\t"format"\n", __VA_ARGS__)

    #define TTY_FIX_TO_FLOAT(val, shift) tty__fix_to_float(val, shift)
    
    float tty__fix_to_float(TTY_int32 val, TTY_int32 shift) {
        float value = val >> shift;
        float power = 0.5f;
        TTY_int32 mask = 1 << (shift - 1);
        for (TTY_uint32 i = 0; i < shift; i++) {
            if (val & mask) {
                value += power;
            }
            mask >>= 1;
            power /= 2.0f;
        }
        return value;
    }
#else
    #define TTY_LOG_UNKNOWN_INS(ins)
    #define TTY_LOG_PROGRAM(program)
    #define TTY_LOG_INS(level)
    #define TTY_LOG_POINT(point, level)
    #define TTY_LOG_VALUE(value, level)
    #define TTY_LOG_CUSTOM_F(level, format, ...)
    #define TTY_FIX_TO_FLOAT(val, shift)
#endif


TTY_bool tty_init(TTY* font, const char* path) {
    memset(font, 0, sizeof(TTY));

    if (!tty__read_file_into_buffer(font, path)) {
        goto init_failure;
    }

    // sfntVersion is 0x00010000 for fonts that contain TrueType outlines
    if (tty__get_uint32(font->data) != 0x00010000) {
        goto init_failure;
    }

    if (!tty__extract_info_from_table_directory(font)) {
        goto init_failure;
    }

    if (!tty__extract_char_encoding(font)) {
        goto init_failure;
    }

    if (font->OS2.exists) {
        font->ascender  = tty__get_int16(font->data + font->OS2.off + 68);
        font->descender = tty__get_int16(font->data + font->OS2.off + 70);
    }
    else {
        // TODO: Get defaultAscender and defaultDescender from hhea
        assert(0);
    }

    if (font->hasHinting) {
        if (!tty__alloc_mem_for_ins_processing(font)) {
            goto init_failure;
        }

        tty__execute_font_program(font);
    }

    return TTY_TRUE;

init_failure:
    tty_free(font);
    return TTY_FALSE;
}

TTY_bool tty_instance_init(TTY* font, TTY_Instance* instance, TTY_uint32 ppem) {
    memset(instance, 0, sizeof(TTY_Instance));

    // Scale is 10.22 since upem already has a scale factor of 1
    instance->scale       = tty__rounded_div((TTY_int64)ppem << 22, tty__get_upem(font));
    instance->ppem        = ppem;
    instance->isRotated   = TTY_FALSE;
    instance->isStretched = TTY_FALSE;

    if (font->hasHinting) {
        {
            instance->zone0.cap = 
                TTY_NUM_PHANTOM_POINTS + tty__get_uint16(font->data + font->maxp.off + 16);

            size_t ptsSize   = instance->zone0.cap * sizeof(TTY_F26Dot6_V2);
            size_t touchSize = instance->zone0.cap * sizeof(TTY_Touch_Flag);
            size_t off       = 0;

            instance->zone0.mem = malloc(2 * ptsSize + touchSize);
            if (instance->zone0.mem == NULL) {
                return TTY_FALSE;
            }

            instance->zone0.cur        = (TTY_F26Dot6_V2*)(instance->zone0.mem);
            instance->zone0.orgScaled  = (TTY_F26Dot6_V2*)(instance->zone0.mem + (off += ptsSize));
            instance->zone0.touchFlags = (TTY_Touch_Flag*)(instance->zone0.mem + (off += ptsSize));
            instance->zone0.org        = NULL;
            instance->zone0.pointTypes = NULL;
        }

        {
            size_t cvtSize   = tty__get_cvt_cap(font)          * sizeof(TTY_F26Dot6);
            size_t storeSize = tty__get_storage_area_cap(font) * sizeof(TTY_int32);

            instance->mem = malloc(storeSize + cvtSize);
            if (instance->mem == NULL) {
                return TTY_FALSE;
            }

            instance->cvt         = (TTY_F26Dot6*)(instance->mem);
            instance->storageArea = (TTY_int32*)  (instance->mem + cvtSize);
        }
        
        {
            // Convert default CVT values, given in FUnits, to 26.6 fixed point
            // pixel units
            TTY_uint32 idx = 0;
            TTY_uint8* cvt = font->data + font->cvt.off;

            for (TTY_uint32 off = 0; off < font->cvt.size; off += 2) {
                TTY_int32 funits = tty__get_int16(cvt + off);
                instance->cvt[idx++] = TTY_FIX_MUL(funits << 6, instance->scale, 22);
            }
        }

        TTY_INSTANCE = instance;
        tty__execute_cv_program(font);
    }

    return TTY_TRUE;
}

TTY_bool tty_image_init(TTY_Image* image, TTY_uint8* pixels, TTY_uint32 w, TTY_uint32 h) {
    if (pixels == NULL) {
        pixels = calloc(w * h, 1);
        if (pixels == NULL) {
            return TTY_FALSE;
        }
    }

    image->pixels = pixels;
    image->w      = w;
    image->h      = h;
    return TTY_TRUE;
}

void tty_glyph_init(TTY* font, TTY_Glyph* glyph, TTY_uint32 glyphIdx) {
    glyph->idx         = glyphIdx;
    glyph->bitmapPos.x = 0;
    glyph->bitmapPos.y = 0;
    glyph->offset.x    = 0;
    glyph->offset.y    = 0;
    glyph->size.x      = 0;
    glyph->size.y      = 0;
    glyph->xAdvance    = tty__get_glyph_x_advance(font, glyphIdx);
}

void tty_free(TTY* font) {
    if (font) {
        free(font->data);
        free(font->insMem);
    }
}

void tty_free_instance(TTY* font, TTY_Instance* instance) {
    if (instance) {
        free(instance->zone0.mem);
        free(instance->mem);
    }
}

void tty_free_image(TTY_Image* image) {
    if (image) {
        free(image->pixels);
    }
}

TTY_uint32 tty_get_glyph_index(TTY* font, TTY_uint32 cp) {
    TTY_uint8* subtable = font->data + font->cmap.off + font->encoding.off;

    switch (tty__get_uint16(subtable)) {
        case 4:
            return tty__get_glyph_index_format_4(subtable, cp);
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

TTY_uint16 tty__get_num_glyphs(TTY* font) {
    return tty__get_uint16(font->data + font->maxp.off + 4);
}

TTY_int32 tty_get_ascender(TTY* font, TTY_Instance* instance) {
    return tty__f26dot6_ceil(TTY_FIX_MUL(font->ascender << 6, instance->scale, 22)) >> 6;
}

TTY_int32 tty_get_advance_width(TTY_Instance* instance, TTY_Glyph* glyph) {
    return tty__f26dot6_round(TTY_FIX_MUL(glyph->xAdvance << 6, instance->scale, 22)) >> 6;
}

TTY_bool tty_render_glyph(TTY* font, TTY_Image* image, TTY_uint32 cp) {
    // TODO
    assert(0);
    return TTY_FALSE;
}

TTY_bool tty_render_glyph_to_existing_image(TTY*          font, 
                                            TTY_Instance* instance, 
                                            TTY_Image*    image, 
                                            TTY_Glyph*    glyph, 
                                            TTY_uint32    x, 
                                            TTY_uint32    y) {
    if (!tty__get_glyf_data_block(font, &TTY_TEMP.glyfBlock, glyph->idx)) {
        // The glyph doesn't have any outlines (' ' for example)
        return TTY_TRUE;
    }

    TTY_INSTANCE         = instance;
    TTY_TEMP.glyph       = glyph;
    TTY_TEMP.numContours = tty__get_int16(TTY_TEMP.glyfBlock);
    TTY_TEMP.numPoints   = 1 + tty__get_uint16(TTY_TEMP.glyfBlock + 8 + 2 * TTY_TEMP.numContours);


    TTY_uint32 numEdges;
    TTY_Edge* edges = tty__get_glyph_edges(font, &numEdges);
    if (edges == NULL) {
        return TTY_FALSE;
    }

    // Sort edges from topmost to bottom most (smallest to largest y)
    qsort(edges, numEdges, sizeof(TTY_Edge), tty__compare_edges);


    TTY_Active_Edge_List activeEdgeList;
    if (!tty__active_edge_list_init(&activeEdgeList)) {
        free(edges);
        return TTY_FALSE;
    }
    
    
    // Use this array when calculating the alpha values of each row of pixels.
    // The image's pixels are not used directly because a loss of precision
    // would result. This is due to the fact that the image's pixels are one 
    // byte each and cannot store fractional values.
    TTY_F26Dot6* pixelRow = calloc(glyph->size.x, sizeof(TTY_F26Dot6));
    if (pixelRow == NULL) {
        tty__active_edge_list_free(&activeEdgeList);
        free(edges);
        return TTY_FALSE;
    }
    

    TTY_F26Dot6 yRel    = 0;
    TTY_F26Dot6 yAbs    = y << 6;
    TTY_F26Dot6 yEndAbs = tty__min(glyph->size.y + y, image->h - 1) << 6;
    TTY_uint32  edgeIdx = 0;
    
    while (yAbs <= yEndAbs) {
        {
            // If an edge is no longer active, remove it from the list, else
            // update its x-intersection with the current scanline

            TTY_Active_Edge* activeEdge     = activeEdgeList.headEdge;
            TTY_Active_Edge* prevActiveEdge = NULL;

            while (activeEdge != NULL) {
                TTY_Active_Edge* next = activeEdge->next;
                
                if (activeEdge->edge->yMax <= yRel) {
                    tty__remove_active_edge(&activeEdgeList, prevActiveEdge, activeEdge);
                }
                else {
                    activeEdge->xIntersection = 
                        tty__get_edge_scanline_x_intersection(activeEdge->edge, yRel);
                    
                    prevActiveEdge = activeEdge;
                }
                
                activeEdge = next;
            }
        }

        {
            // Make sure that the active edges are still sorted from smallest 
            // to largest x-intersection

            TTY_Active_Edge* activeEdge     = activeEdgeList.headEdge;
            TTY_Active_Edge* prevActiveEdge = NULL;
            
            while (activeEdge != NULL) {
                TTY_Active_Edge* current = activeEdge;

                while (TTY_TRUE) {
                    if (current->next != NULL) {
                        if (current->xIntersection > current->next->xIntersection) {
                            tty__swap_active_edge_with_next(
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
                TTY_F26Dot6 xIntersection = 
                    tty__get_edge_scanline_x_intersection(edges + edgeIdx, yRel);
                
                TTY_Active_Edge* activeEdge     = activeEdgeList.headEdge;
                TTY_Active_Edge* prevActiveEdge = NULL;
                
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
                
                TTY_Active_Edge* newActiveEdge = 
                    prevActiveEdge == NULL ?
                    tty__insert_active_edge_first(&activeEdgeList) :
                    tty__insert_active_edge_after(&activeEdgeList, prevActiveEdge);
                
                if (newActiveEdge == NULL) {
                    tty__active_edge_list_free(&activeEdgeList);
                    free(edges);
                    free(pixelRow);
                    return TTY_FALSE;
                }
                
                newActiveEdge->edge          = edges + edgeIdx;
                newActiveEdge->xIntersection = xIntersection;
            }
            
            edgeIdx++;
        }


        if (activeEdgeList.headEdge != NULL) {
            // Set the opacity of the pixels along the scanline

            TTY_Active_Edge* activeEdge    = activeEdgeList.headEdge;
            TTY_int32        windingNumber = 0;
            TTY_F26Dot6      weightedAlpha = TTY_FIX_MUL(0x3FC0, TTY_PIXELS_PER_SCANLINE, 6);
            
            TTY_F26Dot6 xRel = tty__f26dot6_ceil(activeEdge->xIntersection);
            if (xRel == 0) {
                xRel += 0x40;
            }

            TTY_F26Dot6 xIdx = (xRel >> 6) - 1;

            while (TTY_TRUE) {
                {
                    // Handle pixels that are only partially covered by a contour
                    // assert(xIdx < glyph->size.x);

                    TTY_F26Dot6 coverage =
                        windingNumber == 0 ?
                        xRel - activeEdge->xIntersection :
                        activeEdge->xIntersection - xRel + 0x40;

                partial_coverage:
                    pixelRow[xIdx] += TTY_FIX_MUL(weightedAlpha, coverage, 6);

                next_active_edge:
                    if (activeEdge->next == NULL) {
                        break;
                    }

                    TTY_Active_Edge* prev = activeEdge;

                    windingNumber += activeEdge->edge->dir;
                    activeEdge     = activeEdge->next;

                    if (activeEdge->xIntersection == prev->xIntersection) {
                        goto next_active_edge;
                    }

                    if (xRel == activeEdge->xIntersection) {
                        goto next_active_edge;
                    }

                    if (xRel > activeEdge->xIntersection) {
                        if (windingNumber == 0) {
                            coverage = xRel - activeEdge->xIntersection;
                            goto partial_coverage;
                        }
                        else {
                            goto next_active_edge;
                        }
                    }
                }

                xRel += 0x40;
                xIdx += 1;

                if (xRel < activeEdge->xIntersection) {
                    // Handle pixels that are either fully covered or fully 
                    // not covered by a contour

                    if (windingNumber == 0) {
                        xRel = tty__f26dot6_ceil(activeEdge->xIntersection);
                        xIdx = (xRel >> 6) - 1;
                    }
                    else {
                        do {
                            // assert(xIdx < glyph->size.x);

                            pixelRow[xIdx] += weightedAlpha;
                            xRel           += 0x40;
                            xIdx           += 1;
                        } while (xRel < activeEdge->xIntersection);
                    }
                }
            }
        }

        yAbs += TTY_PIXELS_PER_SCANLINE;
        yRel += TTY_PIXELS_PER_SCANLINE;
        
        if ((yRel & 0x3F) == 0) {
            // A new row of pixels has been reached
            TTY_uint32 startIdx = (((yAbs - 0x40) >> 6) * image->w) + x;
            
            for (TTY_uint32 i = 0; i < glyph->size.x; i++) {
                // TODO: Round instead of floor?
                // assert(pixelRow[i] >= 0);
                // assert(pixelRow[i] >> 6 <= 255);
                image->pixels[startIdx + i] = pixelRow[i] >> 6;
            }
            
            memset(pixelRow, 0, glyph->size.x * sizeof(TTY_F26Dot6));
        }
    }

    
    tty__active_edge_list_free(&activeEdgeList);
    free(edges);
    free(pixelRow);
    return TTY_TRUE;
}


/* ------------------------- */
/* Initialization Operations */
/* ------------------------- */
static TTY_bool tty__read_file_into_buffer(TTY* font, const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return TTY_FALSE;
    }
    
    fseek(f, 0, SEEK_END);
    font->size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    font->data = calloc(font->size, 1);
    if (font->data == NULL) {
        fclose(f);
        return TTY_FALSE;
    }

    fread(font->data, 1, font->size, f);
    fclose(f);

    return TTY_TRUE;
}

static TTY_bool tty__extract_info_from_table_directory(TTY* font) {
    TTY_uint16 numTables = tty__get_uint16(font->data + 4);

    for (TTY_uint16 i = 0; i < numTables; i++) {
        TTY_uint8* record = font->data + (12 + 16 * i);
        TTY_Table* table  = NULL;

        TTY_uint8 tag[4];
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
            table->exists = TTY_TRUE;
            table->off    = tty__get_Offset32(record + 8);
            table->size   = tty__get_uint32(record + 12);
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

static TTY_bool tty__extract_char_encoding(TTY* font) {
    TTY_uint16 numTables = tty__get_uint16(font->data + font->cmap.off + 2);
    
    for (TTY_uint16 i = 0; i < numTables; i++) {
        TTY_uint8* data = font->data + font->cmap.off + 4 + i * 8;

        TTY_uint16 platformID = tty__get_uint16(data);
        TTY_uint16 encodingID = tty__get_uint16(data + 2);
        TTY_bool   foundValid = TTY_FALSE;

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
            font->encoding.off        = tty__get_Offset32(data + 4);

            TTY_uint8* subtable = font->data + font->cmap.off + font->encoding.off;
            TTY_uint16 format   = tty__get_uint16(subtable);

            if (tty__format_is_supported(format)) {
                return TTY_TRUE;
            }
        }
    }

    return TTY_FALSE;
}

static TTY_bool tty__format_is_supported(TTY_uint16 format) {
    switch (format) {
        case 4:
        case 6:
        case 8:
        case 10:
        case 12:
        case 13:
        case 14:
            return TTY_TRUE;
    }
    return TTY_FALSE;
}

static TTY_bool tty__alloc_mem_for_ins_processing(TTY* font) {
    font->stack.cap     = tty__get_uint16(font->data + font->maxp.off + 24);
    font->funcArray.cap = tty__get_uint16(font->data + font->maxp.off + 20);

    size_t stackSize = sizeof(TTY_Stack_Frame) * font->stack.cap;
    size_t funcsSize = sizeof(TTY_Func)        * font->funcArray.cap;

    font->insMem = calloc(stackSize + funcsSize, 1);
    if (font->insMem == NULL) {
        return TTY_FALSE;
    }

    font->stack.frames    = (TTY_Stack_Frame*)(font->insMem);
    font->funcArray.funcs = (TTY_Func*)       (font->insMem + stackSize);
    return TTY_TRUE;
}


/* ------------------- */
/* Glyph Index Mapping */
/* ------------------- */
static TTY_uint16 tty__get_glyph_index_format_4(TTY_uint8* subtable, TTY_uint32 cp) {
    #define TTY_GET_END_CODE(index) tty__get_uint16(subtable + 14 + 2 * (index))
    
    TTY_uint16 segCount = tty__get_uint16(subtable + 6) >> 1;
    TTY_uint16 left     = 0;
    TTY_uint16 right    = segCount - 1;

    while (left <= right) {
        TTY_uint16 mid     = (left + right) / 2;
        TTY_uint16 endCode = TTY_GET_END_CODE(mid);

        if (endCode >= cp) {
            if (mid == 0 || TTY_GET_END_CODE(mid - 1) < cp) {
                TTY_uint32 off            = 16 + 2 * mid;
                TTY_uint8* idRangeOffsets = subtable + 6 * segCount + off;
                TTY_uint16 idRangeOffset  = tty__get_uint16(idRangeOffsets);
                TTY_uint16 startCode      = tty__get_uint16(subtable + 2 * segCount + off);

                if (startCode > cp) {
                    return 0;
                }
                
                if (idRangeOffset == 0) {
                    TTY_uint16 idDelta = tty__get_int16(subtable + 4 * segCount + off);
                    return cp + idDelta;
                }

                return tty__get_uint16(idRangeOffset + 2 * (cp - startCode) + idRangeOffsets);
            }
            right = mid - 1;
        }
        else {
            left = mid + 1;
        }
    }

    return 0;

    #undef TTY_GET_END_CODE
}


/* -------------------- */
/* Rendering Operations */
/* -------------------- */
static void tty__convert_points_to_bitmap_space(TTY* font, TTY_F26Dot6_V2* points) {
    // This function ensures that the x and y minimums are 0 and inverts the 
    // y-axis so that y values increase downwards.

    TTY_F26Dot6 xMax = points[0].x;
    TTY_F26Dot6 xMin = points[0].x;
    TTY_F26Dot6 yMin = points[0].y;
    TTY_F26Dot6 yMax = points[0].y;

    for (TTY_uint32 i = 1; i < TTY_TEMP.numPoints; i++) {
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

    TTY_TEMP.glyph->size.x = labs(xMax - xMin);
    TTY_TEMP.glyph->size.y = labs(yMax - yMin);

    for (TTY_uint32 i = 0; i < TTY_TEMP.numPoints; i++) {
        points[i].x -= xMin;
        points[i].y  = TTY_TEMP.glyph->size.y - (points[i].y - yMin);
    }

    TTY_TEMP.glyph->offset.x = tty__f26dot6_round(xMin) >> 6;
    TTY_TEMP.glyph->offset.y = tty__f26dot6_round(yMax) >> 6;

    TTY_TEMP.glyph->size.x = tty__f26dot6_ceil(TTY_TEMP.glyph->size.x) >> 6;
    TTY_TEMP.glyph->size.y = tty__f26dot6_ceil(TTY_TEMP.glyph->size.y) >> 6;
}

static TTY_Edge* tty__get_glyph_edges(TTY* font, TTY_uint32* numEdges) {
    // Get the points of the glyph
    TTY_F26Dot6_V2* points;
    TTY_Point_Type* pointTypes;

    if (font->hasHinting) {
        if (!tty__execute_glyph_program(font)) {
            return NULL;
        }
        points     = TTY_TEMP.zone1.cur;
        pointTypes = TTY_TEMP.zone1.pointTypes;
    }
    else {
        if (!tty__extract_glyph_points(font)) {
            return NULL;
        }
        points     = TTY_TEMP.unhinted.points;
        pointTypes = TTY_TEMP.unhinted.pointTypes;
    }

    tty__convert_points_to_bitmap_space(font, points);


    // Convert the glyph points into curves
    TTY_uint32 numCurves;
    TTY_Curve* curves = tty__convert_points_into_curves(font, points, pointTypes, &numCurves);

    if (font->hasHinting) {
        free(TTY_TEMP.zone1.mem);
    }
    else {
        free(TTY_TEMP.unhinted.mem);
    }

    if (curves == NULL) {
        return NULL;
    }


    // Approximate the glyph curves using edges
    //
    // This is done because the intersection of a scanline and an edge is 
    // simpler and cheaper to calculate than the intersection of a scanline and 
    // a curve.
    TTY_Edge* edges = tty__subdivide_curves_into_edges(font, curves, numCurves, numEdges);
    
    free(curves);

    if (edges == NULL) {
        return NULL;
    }

    return edges;
}

static TTY_Curve* tty__convert_points_into_curves(TTY*            font, 
                                                  TTY_F26Dot6_V2* points, 
                                                  TTY_Point_Type* pointTypes, 
                                                  TTY_uint32*     numCurves) {
    TTY_Curve* curves = malloc(TTY_TEMP.numPoints * sizeof(TTY_Curve));
    if (curves == NULL) {
        return NULL;
    }

    TTY_uint32 startPointIdx = 0;
    *numCurves = 0;

    for (TTY_uint32 i = 0; i < TTY_TEMP.numContours; i++) {
        TTY_uint16 endPointIdx   = tty__get_uint16(TTY_TEMP.glyfBlock + 10 + 2 * i);
        TTY_bool   addFinalCurve = TTY_TRUE;

        TTY_F26Dot6_V2* startPoint = points + startPointIdx;
        TTY_F26Dot6_V2* nextP0     = startPoint;
        
        for (TTY_uint32 j = startPointIdx + 1; j <= endPointIdx; j++) {
            TTY_Curve* curve = curves + *numCurves;
            curve->p0 = *nextP0;
            curve->p1 = points[j];

            if (pointTypes[j] == TTY_ON_CURVE_POINT) {
                curve->p2 = curve->p1;
            }
            else if (j == endPointIdx) {
                curve->p2     = *startPoint;
                addFinalCurve = TTY_FALSE;
            }
            else if (pointTypes[j + 1] == TTY_ON_CURVE_POINT) {
                curve->p2 = points[++j];
            }
            else { // Implied on-curve point
                TTY_F26Dot6_V2* nextPoint = points + j + 1;
                tty__fix_v2_sub(&curve->p1, nextPoint, &curve->p2);
                tty__fix_v2_scale(&curve->p2, 0x20, 6);
                tty__fix_v2_add(nextPoint, &curve->p2, &curve->p2);
            }

            nextP0 = &curve->p2;
            (*numCurves)++;
        }

        if (addFinalCurve) {
            TTY_Curve* finalCurve = curves + *numCurves;
            finalCurve->p0 = *nextP0;
            finalCurve->p1 = *startPoint;
            finalCurve->p2 = *startPoint;
            (*numCurves)++;
        }

        startPointIdx = endPointIdx + 1;
    }

    return curves;
}

static TTY_Edge* tty__subdivide_curves_into_edges(TTY*        font, 
                                                  TTY_Curve*  curves, 
                                                  TTY_uint32  numCurves, 
                                                  TTY_uint32* numEdges) {
    // Count the number of edges that are needed
    *numEdges = 0;
    for (TTY_uint32 i = 0; i < numCurves; i++) {
        if (curves[i].p1.x == curves[i].p2.x && curves[i].p1.y == curves[i].p2.y) {
            // The curve is a straight line, no need to flatten it
            (*numEdges)++;
        }
        else {
            tty__subdivide_curve_into_edges(
                &curves[i].p0, &curves[i].p1, &curves[i].p2, 0, NULL, numEdges);
        }
    }

    TTY_Edge* edges = malloc(sizeof(TTY_Edge) * *numEdges);
    if (edges == NULL) {
        return NULL;
    }

    *numEdges = 0;
    for (TTY_uint32 i = 0; i < numCurves; i++) {
        TTY_int8 dir = curves[i].p2.y < curves[i].p0.y ? 1 : -1;

        if (curves[i].p1.x == curves[i].p2.x && curves[i].p1.y == curves[i].p2.y) {
            // The curve is a straight line, no need to flatten it
            tty__edge_init(edges + *numEdges, &curves[i].p0, &curves[i].p2, dir);
            (*numEdges)++;
        }
        else {
            tty__subdivide_curve_into_edges(
                &curves[i].p0, &curves[i].p1, &curves[i].p2, dir, edges, numEdges);
        }
    }

    return edges;
}

static void tty__subdivide_curve_into_edges(TTY_F26Dot6_V2* p0, 
                                            TTY_F26Dot6_V2* p1, 
                                            TTY_F26Dot6_V2* p2, 
                                            TTY_int8        dir, 
                                            TTY_Edge*       edges, 
                                            TTY_uint32*     numEdges) {
    #define TTY_DIVIDE(a, b)                       \
        { TTY_FIX_MUL(((a)->x + (b)->x), 0x20, 6), \
          TTY_FIX_MUL(((a)->y + (b)->y), 0x20, 6) }

    TTY_F26Dot6_V2 mid0 = TTY_DIVIDE(p0, p1);
    TTY_F26Dot6_V2 mid1 = TTY_DIVIDE(p1, p2);
    TTY_F26Dot6_V2 mid2 = TTY_DIVIDE(&mid0, &mid1);

    {
        TTY_F26Dot6_V2 d = TTY_DIVIDE(p0, p2);
        d.x -= mid2.x;
        d.y -= mid2.y;

        TTY_F26Dot6 sqrdError = TTY_FIX_MUL(d.x, d.x, 6) + TTY_FIX_MUL(d.y, d.y, 6);
        if (sqrdError <= TTY_SUBDIVIDE_SQRD_ERROR) {
            if (edges != NULL) {
                tty__edge_init(edges + *numEdges, p0, p2, dir);
            }
            (*numEdges)++;
            return;
        }
    }

    tty__subdivide_curve_into_edges(p0, &mid0, &mid2, dir, edges, numEdges);
    tty__subdivide_curve_into_edges(&mid2, &mid1, p2, dir, edges, numEdges);

    #undef TTY_DIVIDE
}

static void tty__edge_init(TTY_Edge* edge, TTY_F26Dot6_V2* p0, TTY_F26Dot6_V2* p1, TTY_int8 dir) {
    edge->p0       = *p0;
    edge->p1       = *p1;
    edge->invSlope = tty__get_inv_slope(p0, p1);
    edge->dir      = dir;
    edge->xMin     = tty__min(p0->x, p1->x);
    tty__max_min(p0->y, p1->y, &edge->yMax, &edge->yMin);
}

static TTY_F16Dot16 tty__get_inv_slope(TTY_F26Dot6_V2* p0, TTY_F26Dot6_V2* p1) {
    if (p0->x == p1->x) {
        return 0;
    }
    if (p0->y == p1->y) {
        return 0;
    }
    
    TTY_F16Dot16 slope = tty__fix_div(p1->y - p0->y, p1->x - p0->x, 16);
    return tty__fix_div(1l << 16, slope, 16);
}

static int tty__compare_edges(const void* edge0, const void* edge1) {
    return ((TTY_Edge*)edge0)->yMin - ((TTY_Edge*)edge1)->yMin;
}

static TTY_F26Dot6 tty__get_edge_scanline_x_intersection(TTY_Edge* edge, TTY_F26Dot6 scanline) {
    return TTY_FIX_MUL(scanline - edge->p0.y, edge->invSlope, 16) + edge->p0.x;
}

static TTY_bool tty__active_edge_list_init(TTY_Active_Edge_List* list) {
    list->headChunk = calloc(1, sizeof(TTY_Active_Chunk));
    if (list->headChunk != NULL) {
        list->headEdge      = NULL;
        list->reusableEdges = NULL;
        return TTY_TRUE;
    }
    return TTY_FALSE;
}

static void tty__active_edge_list_free(TTY_Active_Edge_List* list) {
    TTY_Active_Chunk* chunk = list->headChunk;
    while (chunk != NULL) {
        TTY_Active_Chunk* next = chunk->next;
        free(chunk);
        chunk = next;
    }
}

static TTY_Active_Edge* tty__get_available_active_edge(TTY_Active_Edge_List* list) {
    if (list->reusableEdges != NULL) {
        // Reuse the memory from a previously removed edge
        TTY_Active_Edge* edge = list->reusableEdges;
        list->reusableEdges = edge->next;
        edge->next          = NULL;
        return edge;
    }
    
    if (list->headChunk->numEdges == TTY_EDGES_PER_CHUNK) {
        // The current chunk is full, so allocate a new one
        TTY_Active_Chunk* chunk = calloc(1, sizeof(TTY_Active_Chunk));
        if (chunk == NULL) {
            return NULL;
        }
        chunk->next     = list->headChunk;
        list->headChunk = chunk;
    }

    TTY_Active_Edge* edge = list->headChunk->edges + list->headChunk->numEdges;
    list->headChunk->numEdges++;
    return edge;
}

static TTY_Active_Edge* tty__insert_active_edge_first(TTY_Active_Edge_List* list) {
    TTY_Active_Edge* edge = tty__get_available_active_edge(list);
    edge->next     = list->headEdge;
    list->headEdge = edge;
    return edge;
}

static TTY_Active_Edge* tty__insert_active_edge_after(TTY_Active_Edge_List* list, 
                                                      TTY_Active_Edge*      after) {
    TTY_Active_Edge* edge = tty__get_available_active_edge(list);
    edge->next  = after->next;
    after->next = edge;
    return edge;
}

static void tty__remove_active_edge(TTY_Active_Edge_List* list, 
                                    TTY_Active_Edge*      prev, 
                                    TTY_Active_Edge*      remove) {
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

static void tty__swap_active_edge_with_next(TTY_Active_Edge_List* list, 
                                            TTY_Active_Edge*      prev, 
                                            TTY_Active_Edge*      edge) {
    assert(edge->next != NULL);
    
    if (prev != NULL) {
        prev->next = edge->next;
    }

    TTY_Active_Edge* temp = edge->next->next;
    edge->next->next = edge;
    edge->next       = temp;
}


/* --------------------- */
/* glyf Table Operations */
/* --------------------- */
static TTY_bool tty__get_glyf_data_block(TTY* font, TTY_uint8** block, TTY_uint32 glyphIdx) {
    #define TTY_GET_OFF_16(idx)\
        (tty__get_Offset16(font->data + font->loca.off + (2 * (idx))) * 2)

    #define TTY_GET_OFF_32(idx)\
        tty__get_Offset32(font->data + font->loca.off + (4 * (idx)))

    TTY_int16  version   = tty__get_int16(font->data + font->head.off + 50);
    TTY_uint16 numGlyphs = tty__get_num_glyphs(font);

    TTY_Offset32 blockOff, nextBlockOff;

    if (glyphIdx == numGlyphs - 1) {
        blockOff = version == 0 ? TTY_GET_OFF_16(glyphIdx) : TTY_GET_OFF_32(glyphIdx);
        *block   = font->data + font->glyf.off + blockOff;
        return TTY_TRUE;
    }

    if (version == 0) {
        blockOff     = TTY_GET_OFF_16(glyphIdx);
        nextBlockOff = TTY_GET_OFF_16(glyphIdx + 1);
    }
    else {
        blockOff     = TTY_GET_OFF_32(glyphIdx);
        nextBlockOff = TTY_GET_OFF_32(glyphIdx + 1);
    }
    
    if (blockOff == nextBlockOff) {
        // "If a glyph has no outline, then loca[n] = loca [n+1]"
        return TTY_FALSE;
    }

    *block = font->data + font->glyf.off + blockOff;
    return TTY_TRUE;
    
    #undef TTY_GET_OFF_16
    #undef TTY_GET_OFF_32
}

static TTY_bool tty__extract_glyph_points(TTY* font) {
    TTY_uint32      pointIdx = 0;
    TTY_V2*         points;
    TTY_Point_Type* pointTypes;

    if (font->hasHinting) {
        TTY_TEMP.zone1.cap = TTY_NUM_PHANTOM_POINTS + TTY_TEMP.numPoints;

        size_t ptsSize   = TTY_TEMP.zone1.cap * sizeof(TTY_F26Dot6_V2);
        size_t touchSize = TTY_TEMP.zone1.cap * sizeof(TTY_Touch_Flag);
        size_t typesSize = TTY_TEMP.zone1.cap * sizeof(TTY_Point_Type);
        size_t off       = 0;
        
        TTY_TEMP.zone1.mem = malloc(3 * ptsSize + touchSize + typesSize);
        if (TTY_TEMP.zone1.mem == NULL) {
            return TTY_FALSE;
        }

        TTY_TEMP.zone1.org        = (TTY_V2*)        (TTY_TEMP.zone1.mem);
        TTY_TEMP.zone1.orgScaled  = (TTY_F26Dot6_V2*)(TTY_TEMP.zone1.mem + (off += ptsSize));
        TTY_TEMP.zone1.cur        = (TTY_F26Dot6_V2*)(TTY_TEMP.zone1.mem + (off += ptsSize));
        TTY_TEMP.zone1.touchFlags = (TTY_Touch_Flag*)(TTY_TEMP.zone1.mem + (off += ptsSize));
        TTY_TEMP.zone1.pointTypes = (TTY_Point_Type*)(TTY_TEMP.zone1.mem + (off += touchSize));

        points     = TTY_TEMP.zone1.org;
        pointTypes = TTY_TEMP.zone1.pointTypes;
    }
    else {
        size_t pointsSize     = sizeof(TTY_F26Dot6_V2) * TTY_TEMP.numPoints;
        size_t pointTypesSize = sizeof(TTY_Point_Type) * TTY_TEMP.numPoints;

        TTY_TEMP.unhinted.mem = malloc(pointsSize + pointTypesSize);
        if (TTY_TEMP.unhinted.mem == NULL) {
            return TTY_FALSE;
        }

        TTY_TEMP.unhinted.points     = (TTY_F26Dot6_V2*)(TTY_TEMP.unhinted.mem);
        TTY_TEMP.unhinted.pointTypes = (TTY_Point_Type*)(TTY_TEMP.unhinted.mem + pointsSize);

        points     = TTY_TEMP.unhinted.points;
        pointTypes = TTY_TEMP.unhinted.pointTypes;
    }


    // Get the contour points from the glyf table data
    {
        TTY_uint32 flagsSize = 0;
        TTY_uint32 xDataSize = 0;
        TTY_V2     absPos    = { 0 };

        TTY_uint8* flagData, *xData, *yData;

        flagData  = TTY_TEMP.glyfBlock + (10 + 2 * TTY_TEMP.numContours);
        flagData += 2 + tty__get_uint16(flagData);
        
        for (TTY_uint32 i = 0; i < TTY_TEMP.numPoints;) {
            TTY_uint8 flags = flagData[flagsSize];
            
            TTY_uint8 xSize = 
                flags & TTY_GLYF_X_SHORT_VECTOR ? 1 : flags & TTY_GLYF_X_DUAL ? 0 : 2;
            
            TTY_uint8 flagsReps;

            if (flags & TTY_GLYF_REPEAT_FLAG) {
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

        while (pointIdx < TTY_TEMP.numPoints) {
            TTY_uint8 flags = *flagData;

            TTY_uint8 flagsReps;
            if (flags & TTY_GLYF_REPEAT_FLAG) {
                flagsReps = 1 + flagData[1];
                flagData += 2;
            }
            else {
                flagsReps = 1;
                flagData++;
            }

            while (flagsReps > 0) {
                TTY_int32 xOff = tty__get_next_coord_off(
                    &xData, TTY_GLYF_X_DUAL, TTY_GLYF_X_SHORT_VECTOR, flags);

                TTY_int32 yOff = tty__get_next_coord_off(
                    &yData, TTY_GLYF_Y_DUAL, TTY_GLYF_Y_SHORT_VECTOR, flags);

                if (flags & TTY_GLYF_ON_CURVE_POINT) {
                    pointTypes[pointIdx] = TTY_ON_CURVE_POINT;
                }
                else {
                    pointTypes[pointIdx] = TTY_OFF_CURVE_POINT;
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
        for (TTY_uint32 i = 0; i < TTY_TEMP.numPoints; i++) {
            points[i].x = TTY_FIX_MUL(points[i].x << 6, TTY_INSTANCE->scale, 22);
            points[i].y = TTY_FIX_MUL(points[i].y << 6, TTY_INSTANCE->scale, 22);
        }
        return TTY_TRUE;
    }


    // Get the phantom points
    {
        TTY_int16 xMin            = tty__get_int16(TTY_TEMP.glyfBlock + 2);
        TTY_int16 yMax            = tty__get_int16(TTY_TEMP.glyfBlock + 8);
        TTY_int16 leftSideBearing = tty__get_glyph_left_side_bearing(font, TTY_TEMP.glyph->idx);

        TTY_int32 yAdvance, topSideBearing;
        tty__get_glyph_vmetrics(font, TTY_TEMP.glyfBlock, &yAdvance, &topSideBearing);

        points[pointIdx].x = xMin - leftSideBearing;
        points[pointIdx].y = 0;

        pointIdx++;
        points[pointIdx].x = points[pointIdx - 1].x + TTY_TEMP.glyph->xAdvance;
        points[pointIdx].y = 0;

        pointIdx++;
        points[pointIdx].y = yMax + topSideBearing;
        points[pointIdx].x = 0;

        pointIdx++;
        points[pointIdx].y = points[pointIdx - 1].y - yAdvance;
        points[pointIdx].x = 0;

        pointIdx++;
        assert(pointIdx == TTY_TEMP.zone1.cap);
    }


    // Set the scaled points
    for (TTY_uint32 i = 0; i < TTY_TEMP.zone1.cap; i++) {
        TTY_TEMP.zone1.orgScaled[i].x = TTY_FIX_MUL(points[i].x << 6, TTY_INSTANCE->scale, 22);
        TTY_TEMP.zone1.orgScaled[i].y = TTY_FIX_MUL(points[i].y << 6, TTY_INSTANCE->scale, 22);
    }


    // Set the current points, phantom points are rounded
    for (TTY_uint32 i = 0; i < TTY_TEMP.zone1.cap; i++) {
        TTY_TEMP.zone1.cur[i]        = TTY_TEMP.zone1.orgScaled[i];
        TTY_TEMP.zone1.touchFlags[i] = TTY_UNTOUCHED;
    }

    TTY_F26Dot6_V2* point = TTY_TEMP.zone1.cur + TTY_TEMP.numPoints;

    point->x = tty__round(font, point->x);
    point++;

    point->x = tty__round(font, point->x);
    point++;

    point->y = tty__round(font, point->y);
    point++;

    point->y = tty__round(font, point->y); 

    return TTY_TRUE;
}

static TTY_int32 tty__get_next_coord_off(TTY_uint8** data, 
                                         TTY_uint8   dualFlag, 
                                         TTY_uint8   shortFlag, 
                                         TTY_uint8   flags) {
    TTY_int32 coord;

    if (flags & shortFlag) {
        coord = !(flags & dualFlag) ? -(**data) : **data;
        (*data)++;
    }
    else if (flags & dualFlag) {
        coord = 0;
    }
    else {
        coord = tty__get_int16(*data);
        (*data) += 2;
    }

    return coord;
}


/* ---------------------------- */
/* Interpreter Stack Operations */
/* ---------------------------- */
static void tty__stack_push_uint32(TTY* font, TTY_uint32 val) {
    assert(font->stack.count < font->stack.cap);
    font->stack.frames[font->stack.count++].uValue = val;
}

static void tty__stack_push_int32(TTY* font, TTY_int32 val) {
    assert(font->stack.count < font->stack.cap);
    font->stack.frames[font->stack.count++].sValue = val;
}

static TTY_uint32 tty__stack_pop_uint32(TTY* font) {
    assert(font->stack.count > 0);
    return font->stack.frames[--font->stack.count].uValue;
}

static TTY_int32 tty__stack_pop_int32(TTY* font) {
    assert(font->stack.count > 0);
    return font->stack.frames[--font->stack.count].sValue;
}

static void tty__stack_clear(TTY* font) {
    font->stack.count = 0;
}

static TTY_uint8 tty__get_num_vals_to_push(TTY_uint8 ins) {
    return 1 + (ins & 0x7);
}


/* ----------------------------- */
/* Instruction Stream Operations */
/* ----------------------------- */
static void tty__ins_stream_init(TTY_Ins_Stream* stream, TTY_uint8* bytes) {
    stream->bytes = bytes;
    stream->off   = 0;
}

static TTY_uint8 tty__ins_stream_next(TTY_Ins_Stream* stream) {
    return stream->bytes[stream->off++];
}

static void tty__ins_stream_jump(TTY_Ins_Stream* stream, TTY_int32 count) {
    stream->off += count;
}


/* --------------------- */
/* Instruction Execution */
/* --------------------- */
static void tty__execute_font_program(TTY* font) {
    TTY_LOG_PROGRAM("Font Program");

    TTY_Ins_Stream stream;
    tty__ins_stream_init(&stream, font->data + font->fpgm.off);
    
    while (stream.off < font->fpgm.size) {
        TTY_uint8 ins = tty__ins_stream_next(&stream);
        assert(ins == TTY_PUSHB || ins == TTY_FDEF || ins == TTY_IDEF);
        tty__execute_ins(font, &stream, ins);
    }
}

static void tty__execute_cv_program(TTY* font) {
    TTY_LOG_PROGRAM("CV Program");

    {
        // "Every time the control value program is run, the zone 0 contour 
        // data is initialized to 0s."
        size_t ptsSize   = TTY_INSTANCE->zone0.cap * sizeof(TTY_F26Dot6_V2);
        size_t touchSize = TTY_INSTANCE->zone0.cap * sizeof(TTY_Touch_Flag);
        memset(TTY_INSTANCE->zone0.mem, 0, 2 * ptsSize + touchSize);
    }

    TTY_Ins_Stream stream;
    tty__ins_stream_init(&stream, font->data + font->prep.off);

    tty__stack_clear(font);
    tty__reset_graphics_state(font);
    
    while (stream.off < font->prep.size) {
        TTY_uint8 ins = tty__ins_stream_next(&stream);
        tty__execute_ins(font, &stream, ins);
    }
}

static TTY_bool tty__execute_glyph_program(TTY* font) {
    TTY_LOG_PROGRAM("Glyph Program");

    tty__stack_clear(font);
    tty__reset_graphics_state(font);

    TTY_uint32 insOff = 10 + TTY_TEMP.numContours * 2;
    TTY_uint16 numIns = tty__get_int16(TTY_TEMP.glyfBlock + insOff);

    TTY_Ins_Stream stream;
    tty__ins_stream_init(&stream, TTY_TEMP.glyfBlock + insOff + 2);
    
    if (!tty__extract_glyph_points(font)) {
        return TTY_FALSE;
    }

    while (stream.off < numIns) {
        TTY_uint8 ins = tty__ins_stream_next(&stream);
        tty__execute_ins(font, &stream, ins);
    }

    return TTY_TRUE;
}

static void tty__execute_ins(TTY* font, TTY_Ins_Stream* stream, TTY_uint8 ins) {
    switch (ins) {
        case TTY_ABS:
            tty__ABS(font);
            return;
        case TTY_ADD:
            tty__ADD(font);
            return;
        case TTY_ALIGNRP:
            tty__ALIGNRP(font);
            return;
        case TTY_AND:
            tty__AND(font);
            return;
        case TTY_CALL:
            tty__CALL(font);
            return;
        case TTY_CINDEX:
            tty__CINDEX(font);
            return;
        case TTY_DELTAC1:
            tty__DELTAC1(font);
            return;
        case TTY_DELTAC2:
            tty__DELTAC2(font);
            return;
        case TTY_DELTAC3:
            tty__DELTAC3(font);
            return;
        case TTY_DELTAP1:
            tty__DELTAP1(font);
            return;
        case TTY_DELTAP2:
            tty__DELTAP2(font);
            return;
        case TTY_DELTAP3:
            tty__DELTAP3(font);
            return;
        case TTY_DEPTH:
            tty__DEPTH(font);
            return;
        case TTY_DIV:
            tty__DIV(font);
            return;
        case TTY_DUP:
            tty__DUP(font);
            return;
        case TTY_EQ:
            tty__EQ(font);
            return;
        case TTY_FDEF:
            tty__FDEF(font, stream);
            return;
        case TTY_FLOOR:
            tty__FLOOR(font);
            return;
        case TTY_GETINFO:
            tty__GETINFO(font);
            return;
        case TTY_GPV:
            tty__GPV(font);
            return;
        case TTY_GT:
            tty__GT(font);
            return;
        case TTY_GTEQ:
            tty__GTEQ(font);
            return;
        case TTY_IDEF:
            tty__IDEF(font, stream);
            return;
        case TTY_IF:
            tty__IF(font, stream);
            return;
        case TTY_IP:
            tty__IP(font);
            return;
        case TTY_JROT:
            tty__JROT(font, stream);
            return;
        case TTY_JMPR:
            tty__JMPR(font, stream);
            return;
        case TTY_LOOPCALL:
            tty__LOOPCALL(font);
            return;
        case TTY_LT:
            tty__LT(font);
            return;
        case TTY_LTEQ:
            tty__LTEQ(font);
            return;
        case TTY_MINDEX:
            tty__MINDEX(font);
            return;
        case TTY_MPPEM:
            tty__MPPEM(font);
            return;
        case TTY_MUL:
            tty__MUL(font);
            return;
        case TTY_NEG:
            tty__NEG(font);
            return;
        case TTY_NEQ:
            tty__NEQ(font);
            return;
        case TTY_NPUSHB:
            tty__NPUSHB(font, stream);
            return;
        case TTY_NPUSHW:
            tty__NPUSHW(font, stream);
            return;
        case TTY_OR:
            tty__OR(font);
            return;
        case TTY_POP:
            tty__POP(font);
            return;
        case TTY_RCVT:
            tty__RCVT(font);
            return;
        case TTY_RDTG:
            tty__RDTG(font);
            return;
        case TTY_ROLL:
            tty__ROLL(font);
            return;
        case TTY_RS:
            tty__RS(font);
            return;
        case TTY_RTG:
            tty__RTG(font);
            return;
        case TTY_RTHG:
            tty__RTHG(font);
            return;
        case TTY_RUTG:
            tty__RUTG(font);
            return;
        case TTY_SCANCTRL:
            tty__SCANCTRL(font);
            return;
        case TTY_SCANTYPE:
            tty__SCANTYPE(font);
            return;
        case TTY_SCVTCI:
            tty__SCVTCI(font);
            return;
        case TTY_SDB:
            tty__SDB(font);
            return;
        case TTY_SDS:
            tty__SDS(font);
            return;
        case TTY_SHPIX:
            tty__SHPIX(font);
            return;
        case TTY_SLOOP:
            tty__SLOOP(font);
            return;
        case TTY_SRP0:
            tty__SRP0(font);
            return;
        case TTY_SRP1:
            tty__SRP1(font);
            return;
        case TTY_SRP2:
            tty__SRP2(font);
            return;
        case TTY_SUB:
            tty__SUB(font);
            return;
        case TTY_SWAP:
            tty__SWAP(font);
            return;
        case TTY_SZPS:
            tty__SZPS(font);
            return;
        case TTY_SZP0:
            tty__SZP0(font);
            return;
        case TTY_SZP1:
            tty__SZP1(font);
            return;
        case TTY_SZP2:
            tty__SZP2(font);
            return;
        case TTY_WCVTF:
            tty__WCVTF(font);
            return;
        case TTY_WCVTP:
            tty__WCVTP(font);
            return;
        case TTY_WS:
            tty__WS(font);
            return;
    }

    if (ins >= TTY_GC && ins <= TTY_GC_MAX) {
        tty__GC(font, ins);
        return;
    }
    else if (ins >= TTY_IUP && ins <= TTY_IUP_MAX) {
        tty__IUP(font, ins);
        return;
    }
    else if (ins >= TTY_MD && ins <= TTY_MD_MAX) {
        tty__MD(font, ins);
        return;
    }
    else if (ins >= TTY_MDAP && ins <= TTY_MDAP_MAX) {
        tty__MDAP(font, ins);
        return;
    }
    else if (ins >= TTY_MDRP && ins <= TTY_MDRP_MAX) {
        tty__MDRP(font, ins);
        return;
    }
    else if (ins >= TTY_MIAP && ins <= TTY_MIAP_MAX) {
        tty__MIAP(font, ins);
        return;
    }
    else if (ins >= TTY_MIRP && ins <= TTY_MIRP_MAX) {
        tty__MIRP(font, ins);
        return;
    }
    else if (ins >= TTY_PUSHB && ins <= TTY_PUSHB_MAX) {
        tty__PUSHB(font, stream, ins);
        return;
    }
    else if (ins >= TTY_PUSHW && ins <= TTY_PUSHW_MAX) {
        tty__PUSHW(font, stream, ins);
        return;
    }
    else if (ins >= TTY_ROUND && ins <= TTY_ROUND_MAX) {
        tty__ROUND(font, ins);
        return;
    }
    else if (ins >= TTY_SVTCA && ins <= TTY_SVTCA_MAX) {
        tty__SVTCA(font, ins);
        return;
    }

    TTY_LOG_UNKNOWN_INS(ins);
    assert(0);
}

static void tty__ABS(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_F26Dot6 val = tty__stack_pop_F26Dot6(font);
    tty__stack_push_F26Dot6(font, labs(val));
    TTY_LOG_VALUE((int)labs(val), TTY_LOG_LEVEL_VERBOSE);
}

static void tty__ADD(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_F26Dot6 n1 = tty__stack_pop_F26Dot6(font);
    TTY_F26Dot6 n2 = tty__stack_pop_F26Dot6(font);
    tty__stack_push_F26Dot6(font, n1 + n2);
    TTY_LOG_VALUE(n1 + n2, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__ALIGNRP(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    TTY_F26Dot6_V2* rp0Cur = font->gState.zp0->cur + font->gState.rp0;

    for (TTY_uint32 i = 0; i < font->gState.loop; i++) {
        TTY_uint32 pointIdx = tty__stack_pop_uint32(font);
        assert(pointIdx < font->gState.zp1->cap);

        TTY_F26Dot6 dist = tty__fix_v2_sub_dot(
            rp0Cur, font->gState.zp1->cur + pointIdx, &font->gState.projVec, 14);

        tty__move_point(font, font->gState.zp1, pointIdx, dist);

        TTY_LOG_POINT(font->gState.zp1->cur[pointIdx], TTY_LOG_LEVEL_MINIMAL);
    }

    font->gState.loop = 1;
}

static void tty__AND(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 e2 = tty__stack_pop_uint32(font);
    TTY_uint32 e1 = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, e1 != 0 && e2 != 0 ? 1 : 0);
    TTY_LOG_VALUE(e1 != 0 && e2 != 0, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__CALL(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    tty__call_func(font, tty__stack_pop_uint32(font), 1);
}

static void tty__CINDEX(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 pos = tty__stack_pop_uint32(font);
    TTY_uint32 val = font->stack.frames[font->stack.count - pos].uValue;
    tty__stack_push_uint32(font, val);
    TTY_LOG_VALUE(val, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__DELTAC1(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    tty__DELTAC(font, 0);
}

static void tty__DELTAC2(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    tty__DELTAC(font, 16);
}

static void tty__DELTAC3(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    tty__DELTAC(font, 32);
}

static void tty__DELTAC(TTY* font, TTY_uint8 range) {
    TTY_uint32 count = tty__stack_pop_uint32(font);

    while (count > 0) {
        TTY_uint32 cvtIdx = tty__stack_pop_uint32(font);
        TTY_uint32 exc    = tty__stack_pop_uint32(font);

        TTY_F26Dot6 deltaVal;
        if (tty__get_delta_value(font, exc, range, &deltaVal)) {
            TTY_INSTANCE->cvt[cvtIdx] += deltaVal;
            TTY_LOG_VALUE(deltaVal, TTY_LOG_LEVEL_MINIMAL);
        }

        count--;
    }
}

static void tty__DELTAP1(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    tty__DELTAP(font, 0);
}

static void tty__DELTAP2(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    tty__DELTAP(font, 16);
}

static void tty__DELTAP3(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    tty__DELTAP(font, 32);
}

static void tty__DELTAP(TTY* font, TTY_uint8 range) {
    TTY_uint32 count = tty__stack_pop_uint32(font);

    while (count > 0) {
        TTY_uint32 pointIdx = tty__stack_pop_uint32(font);
        TTY_uint32 exc      = tty__stack_pop_uint32(font);

        TTY_F26Dot6 deltaVal;
        if (tty__get_delta_value(font, exc, range, &deltaVal)) {
            tty__move_point(font, font->gState.zp0, pointIdx, deltaVal);
            TTY_LOG_VALUE(deltaVal, TTY_LOG_LEVEL_MINIMAL);
        }

        count--;
    }
}

static void tty__DEPTH(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_LOG_VALUE(font->stack.count, TTY_LOG_LEVEL_VERBOSE);
    tty__stack_push_uint32(font, font->stack.count);
}

static void tty__DIV(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    TTY_F26Dot6 n1 = tty__stack_pop_F26Dot6(font);
    TTY_F26Dot6 n2 = tty__stack_pop_F26Dot6(font);
    assert(n1 != 0);

    TTY_bool isNeg = TTY_FALSE;
    
    if (n2 < 0) {
        n2    = -n2;
        isNeg = TTY_TRUE;
    }
    if (n1 < 0) {
        n1    = -n1;
        isNeg = !isNeg;
    }

    TTY_F26Dot6 result = ((TTY_int64)n2 << 6) / n1;
    if (isNeg) {
        result = -result;
    }

    tty__stack_push_F26Dot6(font, result);
    TTY_LOG_VALUE(result, TTY_LOG_LEVEL_MINIMAL);
}

static void tty__DUP(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 e = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, e);
    tty__stack_push_uint32(font, e);
    TTY_LOG_VALUE(e, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__EQ(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_int32 e2 = tty__stack_pop_uint32(font);
    TTY_int32 e1 = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, e1 == e2 ? 1 : 0);
    TTY_LOG_VALUE(e1 == e2, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__FDEF(TTY* font, TTY_Ins_Stream* stream) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    assert(font->funcArray.count < font->funcArray.cap);

    TTY_uint32 funcId = tty__stack_pop_uint32(font);
    assert(funcId < font->funcArray.cap);

    font->funcArray.funcs[funcId].firstIns = stream->bytes + stream->off;
    font->funcArray.count++;

    while (tty__ins_stream_next(stream) != TTY_ENDF);
}

static void tty__FLOOR(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    TTY_F26Dot6 val = tty__stack_pop_F26Dot6(font);
    tty__stack_push_F26Dot6(font, tty__f26dot6_floor(val));
    TTY_LOG_VALUE(tty__f26dot6_floor(val), TTY_LOG_LEVEL_MINIMAL);
}

static void tty__GC(TTY* font, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    TTY_uint32  pointIdx = tty__stack_pop_uint32(font);
    TTY_F26Dot6 value;

    assert(font->gState.zp2 != NULL);
    assert(pointIdx < font->gState.zp2->cap);

    if (ins & 0x1) {
        value = tty__fix_v2_dot(
            font->gState.zp2->orgScaled + pointIdx, &font->gState.dualProjVec, 14);
    }
    else {
        value = tty__fix_v2_dot(font->gState.zp2->cur + pointIdx, &font->gState.projVec, 14);
    }

    tty__stack_push_F26Dot6(font, value);

    TTY_LOG_VALUE(value, TTY_LOG_LEVEL_MINIMAL);
}

static void tty__GETINFO(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    // These are the only supported selector bits for scalar version 35
    enum {
        TTY_VERSION                  = 0x01,
        TTY_GLYPH_ROTATED            = 0x02,
        TTY_GLYPH_STRETCHED          = 0x04,
        TTY_FONT_SMOOTHING_GRAYSCALE = 0x20,
    };

    TTY_uint32 result   = 0;
    TTY_uint32 selector = tty__stack_pop_uint32(font);

    if (selector & TTY_VERSION) {
        result = TTY_SCALAR_VERSION;
    }
    if (selector & TTY_GLYPH_ROTATED) {
        if (TTY_INSTANCE->isRotated) {
            result |= 0x100;
        }
    }
    if (selector & TTY_GLYPH_STRETCHED) {
        if (TTY_INSTANCE->isStretched) {
            result |= 0x200;
        }
    }
    if (selector & TTY_FONT_SMOOTHING_GRAYSCALE) {
        // result |= 0x1000;
    }

    TTY_LOG_VALUE(result, TTY_LOG_LEVEL_MINIMAL);
    tty__stack_push_uint32(font, result);
}

static void tty__GPV(TTY* font) {
    // TODO
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    assert(0);
}

static void tty__GT(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_int32 e2 = tty__stack_pop_uint32(font);
    TTY_int32 e1 = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, e1 > e2 ? 1 : 0);
    TTY_LOG_VALUE(e1 > e2, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__GTEQ(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_int32 e2 = tty__stack_pop_uint32(font);
    TTY_int32 e1 = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, e1 >= e2 ? 1 : 0);
    TTY_LOG_VALUE(e1 >= e2, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__IDEF(TTY* font, TTY_Ins_Stream* stream) {
    // TODO
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    assert(0);
}

static void tty__IF(TTY* font, TTY_Ins_Stream* stream) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    if (tty__stack_pop_uint32(font) == 0) {
        TTY_LOG_VALUE(0, TTY_LOG_LEVEL_VERBOSE);
        if (tty__jump_to_else_or_eif(stream) == TTY_EIF) {
            // Condition is false and there is no else instruction
            return;
        }
    }
    else {
        TTY_LOG_VALUE(1, TTY_LOG_LEVEL_VERBOSE);
    }

    while (TTY_TRUE) {
        TTY_uint8 ins = tty__ins_stream_next(stream);

        if (ins == TTY_ELSE) {
            tty__jump_to_else_or_eif(stream);
            return;
        }

        if (ins == TTY_EIF) {
            return;
        }

        tty__execute_ins(font, stream, ins);
    }
}

static void tty__IP(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    assert(font->gState.rp1 < font->gState.zp0->cap);
    assert(font->gState.rp2 < font->gState.zp1->cap);

    TTY_F26Dot6_V2* rp1Cur = font->gState.zp0->cur + font->gState.rp1;
    TTY_F26Dot6_V2* rp2Cur = font->gState.zp1->cur + font->gState.rp2;

    TTY_bool isTwilightZone = 
        (font->gState.zp0 == &TTY_INSTANCE->zone0) ||
        (font->gState.zp1 == &TTY_INSTANCE->zone0) ||
        (font->gState.zp2 == &TTY_INSTANCE->zone0);

    TTY_F26Dot6_V2* rp1Org, *rp2Org;

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

    TTY_F26Dot6 totalDistCur = tty__fix_v2_sub_dot(rp2Cur, rp1Cur, &font->gState.projVec, 14);
    TTY_F26Dot6 totalDistOrg = tty__fix_v2_sub_dot(rp2Org, rp1Org, &font->gState.dualProjVec, 14);

    for (TTY_uint32 i = 0; i < font->gState.loop; i++) {
        TTY_uint32 pointIdx = tty__stack_pop_uint32(font);
        assert(pointIdx < font->gState.zp2->cap);

        TTY_F26Dot6_V2* pointCur = font->gState.zp2->cur + pointIdx;
        TTY_F26Dot6_V2* pointOrg = 
            (isTwilightZone ? font->gState.zp2->orgScaled : font->gState.zp2->org) + pointIdx;

        TTY_F26Dot6 distCur = tty__fix_v2_sub_dot(pointCur, rp1Cur, &font->gState.projVec, 14);
        TTY_F26Dot6 distOrg = tty__fix_v2_sub_dot(pointOrg, rp1Org, &font->gState.dualProjVec, 14);
        
        // Scale distOrg by however many times bigger totalDistCur is than
        // totalDistOrg. 
        //
        // This ensures D(p,rp1)/D(p',rp1') = D(p,rp2)/D(p',rp2') holds true.
        TTY_F26Dot6 distNew = 
            tty__fix_div(TTY_FIX_MUL(distOrg, totalDistCur, 6), totalDistOrg, 6);

        tty__move_point(font, font->gState.zp2, pointIdx, distNew - distCur);

        TTY_LOG_POINT(*pointCur, TTY_LOG_LEVEL_MINIMAL);
    }

    font->gState.loop = 1;
}

static void tty__IUP(TTY* font, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    // Applying IUP to zone0 is an error
    // TODO: How are composite glyphs handled?
    assert(font->gState.zp2 == &TTY_TEMP.zone1);
    assert(TTY_TEMP.numContours >= 0);

    if (TTY_TEMP.numContours == 0) {
        return;
    }

    TTY_Touch_Flag  touchFlag  = ins & 0x1 ? TTY_TOUCH_X : TTY_TOUCH_Y;
    TTY_Touch_Flag* touchFlags = TTY_TEMP.zone1.touchFlags;
    TTY_uint32      pointIdx   = 0;

    for (TTY_uint16 i = 0; i < TTY_TEMP.numContours; i++) {
        TTY_uint16 startPointIdx = pointIdx;
        TTY_uint16 endPointIdx   = tty__get_uint16(TTY_TEMP.glyfBlock + 10 + 2 * i);
        TTY_uint16 touch0        = 0;
        TTY_bool   findingTouch1 = TTY_FALSE;

        while (pointIdx <= endPointIdx) {
            if (touchFlags[pointIdx] & touchFlag) {
                if (findingTouch1) {
                    tty__IUP_interpolate_or_shift(
                        &TTY_TEMP.zone1, touchFlag, startPointIdx, endPointIdx, touch0, 
                        pointIdx);

                    findingTouch1 = 
                        pointIdx != endPointIdx || (touchFlags[startPointIdx] & touchFlag) == 0;

                    if (findingTouch1) {
                        touch0 = pointIdx;
                    }
                }
                else {
                    touch0        = pointIdx;
                    findingTouch1 = TTY_TRUE;
                }
            }

            pointIdx++;
        }

        if (findingTouch1) {
            // The index of the second touched point wraps back to the 
            // beginning.
            for (TTY_uint32 i = startPointIdx; i <= touch0; i++) {
                if (touchFlags[i] & touchFlag) {
                    tty__IUP_interpolate_or_shift(
                        &TTY_TEMP.zone1, touchFlag, startPointIdx, endPointIdx, touch0, i);
                    break;
                }
            }
        }
    }
}

static void tty__JROT(TTY* font, TTY_Ins_Stream* stream) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    
    TTY_uint32 val = tty__stack_pop_uint32(font);
    TTY_int32  off = tty__stack_pop_int32(font); 

    if (val != 0) {
        tty__ins_stream_jump(stream, off - 1);
        TTY_LOG_VALUE(off - 1, TTY_LOG_LEVEL_VERBOSE);
    }
    else {
        TTY_LOG_VALUE(0, TTY_LOG_LEVEL_VERBOSE);
    }
}

static void tty__JMPR(TTY* font, TTY_Ins_Stream* stream) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_int32 off = tty__stack_pop_int32(font);
    tty__ins_stream_jump(stream, off - 1);
    TTY_LOG_VALUE(off - 1, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__LOOPCALL(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 funcId = tty__stack_pop_uint32(font);
    TTY_uint32 times  = tty__stack_pop_uint32(font);
    tty__call_func(font, funcId, times);
}

static void tty__LT(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_int32 e2 = tty__stack_pop_uint32(font);
    TTY_int32 e1 = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, e1 < e2 ? 1 : 0);
    TTY_LOG_VALUE(e1 < e2, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__LTEQ(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_int32 e2 = tty__stack_pop_uint32(font);
    TTY_int32 e1 = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, e1 <= e2 ? 1 : 0);
    TTY_LOG_VALUE(e1 <= e2, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__MD(TTY* font, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    
    TTY_uint32  pointIdx0 = tty__stack_pop_uint32(font);
    TTY_uint32  pointIdx1 = tty__stack_pop_uint32(font);
    TTY_F26Dot6 dist;

    // TODO: Spec says if ins & 0x1 = 1 then use original outline, but FreeType
    //       uses current outline.

    if (ins & 0x1) {
        dist = tty__fix_v2_sub_dot(
            font->gState.zp0->cur + pointIdx1, font->gState.zp1->cur + pointIdx0, 
            &font->gState.projVec, 14);
    }
    else {
        TTY_bool isTwilightZone =
            font->gState.zp0 == &TTY_INSTANCE->zone0 ||
            font->gState.zp1 == &TTY_INSTANCE->zone0;

        if (isTwilightZone) {
            dist = tty__fix_v2_sub_dot(
                font->gState.zp0->orgScaled + pointIdx1, font->gState.zp1->orgScaled + pointIdx0,
                &font->gState.dualProjVec, 14);
        }
        else {
            dist = tty__fix_v2_sub_dot(
                font->gState.zp0->org + pointIdx1, font->gState.zp1->org + pointIdx0,
                &font->gState.dualProjVec, 14);

            dist = TTY_FIX_MUL(dist << 6, TTY_INSTANCE->scale, 22);
        }
    }

    tty__stack_push_F26Dot6(font, dist);

    TTY_LOG_VALUE(dist, TTY_LOG_LEVEL_MINIMAL);
}

static void tty__MDAP(TTY* font, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    TTY_uint32      pointIdx = tty__stack_pop_uint32(font);
    TTY_F26Dot6_V2* point    = font->gState.zp0->cur + pointIdx;

    if (ins & 0x1) {
        // Move the point to its rounded position
        TTY_F26Dot6 curDist     = tty__fix_v2_dot(point, &font->gState.projVec, 14);
        TTY_F26Dot6 roundedDist = tty__round(font, curDist);
        tty__move_point(font, font->gState.zp0, pointIdx, roundedDist - curDist);
    }
    else {
        // Don't move the point, just mark it as touched
        font->gState.zp0->touchFlags[pointIdx] |= font->gState.touchFlags;
    }

    font->gState.rp0 = pointIdx;
    font->gState.rp1 = pointIdx;

    TTY_LOG_POINT(*point, TTY_LOG_LEVEL_MINIMAL);
}

static void tty__MDRP(TTY* font, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    assert(font->gState.rp0 < font->gState.zp0->cap);

    TTY_uint32 pointIdx = tty__stack_pop_uint32(font);
    assert(pointIdx < font->gState.zp1->cap);

    TTY_F26Dot6_V2* rp0Cur   = font->gState.zp0->cur + font->gState.rp0;
    TTY_F26Dot6_V2* pointCur = font->gState.zp1->cur + pointIdx;

    TTY_bool isTwilightZone = 
        (font->gState.zp0 == &TTY_INSTANCE->zone0) || 
        (font->gState.zp1 == &TTY_INSTANCE->zone0);

    TTY_F26Dot6_V2* rp0Org, *pointOrg;

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

    TTY_F26Dot6 distCur = tty__fix_v2_sub_dot(pointCur, rp0Cur, &font->gState.projVec, 14);
    TTY_F26Dot6 distOrg = tty__fix_v2_sub_dot(pointOrg, rp0Org, &font->gState.dualProjVec, 14);

    if (!isTwilightZone) {
        // Make distOrg a 26.6 pixel value
        distOrg = TTY_FIX_MUL(distOrg << 6, TTY_INSTANCE->scale, 22);
    }

    distOrg = tty__apply_single_width_cut_in(font, distOrg);

    if (ins & 0x04) {
        distOrg = tty__round(font, distOrg);
    }

    if (ins & 0x08) {
        distOrg = tty__apply_min_dist(font, distOrg);
    }

    if (ins & 0x10) {
        font->gState.rp0 = pointIdx;
    }

    tty__move_point(font, font->gState.zp1, pointIdx, distOrg - distCur);

    TTY_LOG_POINT(*pointCur, TTY_LOG_LEVEL_MINIMAL);
}

static void tty__MIAP(TTY* font, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    TTY_uint32 cvtIdx   = tty__stack_pop_uint32(font);
    TTY_uint32 pointIdx = tty__stack_pop_uint32(font);

    assert(cvtIdx < font->cvt.size / sizeof(TTY_FWORD));
    assert(pointIdx < font->gState.zp0->cap);

    TTY_F26Dot6 newDist = TTY_INSTANCE->cvt[cvtIdx];

    if (font->gState.zp0 == &TTY_INSTANCE->zone0) {
        font->gState.zp0->orgScaled[pointIdx].x = 
            TTY_FIX_MUL(newDist, font->gState.freedomVec.x, 14);

        font->gState.zp0->orgScaled[pointIdx].y = 
            TTY_FIX_MUL(newDist, font->gState.freedomVec.y, 14);

        font->gState.zp0->cur[pointIdx] = font->gState.zp0->orgScaled[pointIdx];
    }

    TTY_F26Dot6 curDist = 
        tty__fix_v2_dot(font->gState.zp0->cur + pointIdx, &font->gState.projVec, 14);
    
    if (ins & 0x1) {
        if (labs(newDist - curDist) > font->gState.controlValueCutIn) {
            newDist = curDist;
        }
        newDist = tty__round(font, newDist);
    }

    tty__move_point(font, font->gState.zp0, pointIdx, newDist - curDist);
    
    font->gState.rp0 = pointIdx;
    font->gState.rp1 = pointIdx;

    TTY_LOG_POINT(font->gState.zp0->cur[pointIdx], TTY_LOG_LEVEL_MINIMAL);
}

static void tty__MINDEX(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    TTY_uint32 idx  = font->stack.count - font->stack.frames[font->stack.count - 1].uValue - 1;
    size_t     size = sizeof(TTY_Stack_Frame) * (font->stack.count - idx - 1);

    font->stack.count--;
    font->stack.frames[font->stack.count] = font->stack.frames[idx];
    memmove(font->stack.frames + idx, font->stack.frames + idx + 1, size);
}

static void tty__MIRP(TTY* font, TTY_uint8 ins) {
    // Note: There is a lot of undocumented stuff involving this instruction

    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    TTY_uint32 cvtIdx   = tty__stack_pop_uint32(font);
    TTY_uint32 pointIdx = tty__stack_pop_uint32(font);

    assert(cvtIdx   < font->cvt.size / sizeof(TTY_FWORD));
    assert(pointIdx < font->gState.zp1->cap);

    TTY_F26Dot6 cvtVal = TTY_INSTANCE->cvt[cvtIdx];
    cvtVal = tty__apply_single_width_cut_in(font, cvtVal);

    TTY_F26Dot6_V2* rp0Org = font->gState.zp0->orgScaled + font->gState.rp0;
    TTY_F26Dot6_V2* rp0Cur = font->gState.zp0->cur       + font->gState.rp0;

    TTY_F26Dot6_V2* pointOrg = font->gState.zp1->orgScaled + pointIdx;
    TTY_F26Dot6_V2* pointCur = font->gState.zp1->cur       + pointIdx;

    if (font->gState.zp1 == &TTY_INSTANCE->zone0) {
        pointOrg->x = rp0Org->x + TTY_FIX_MUL(cvtVal, font->gState.freedomVec.x, 14);
        pointOrg->y = rp0Org->y + TTY_FIX_MUL(cvtVal, font->gState.freedomVec.y, 14);
        *pointCur   = *pointOrg;
    }

    TTY_int32 distOrg = tty__fix_v2_sub_dot(pointOrg, rp0Org, &font->gState.dualProjVec, 14);
    TTY_int32 distCur = tty__fix_v2_sub_dot(pointCur, rp0Cur, &font->gState.projVec, 14);

    if (font->gState.autoFlip) {
        if ((distOrg ^ cvtVal) < 0) {
            // Match the sign of distOrg
            cvtVal = -cvtVal;
        }
    }

    TTY_int32 distNew;

    if (ins & 0x04) {
        if (font->gState.zp0 == font->gState.zp1) {
            if (labs(cvtVal - distOrg) > font->gState.controlValueCutIn) {
                cvtVal = distOrg;
            }
        }
        distNew = tty__round(font, cvtVal);
    }
    else {
        distNew = cvtVal;
    }

    if (ins & 0x08) {
        distNew = tty__apply_min_dist(font, distNew);
    }

    tty__move_point(font, font->gState.zp1, pointIdx, distNew - distCur);

    font->gState.rp1 = font->gState.rp0;
    font->gState.rp2 = pointIdx;

    if (ins & 0x10) {
        font->gState.rp0 = pointIdx;
    }

    TTY_LOG_POINT(*pointCur, TTY_LOG_LEVEL_MINIMAL);
}

static void tty__MPPEM(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    tty__stack_push_uint32(font, TTY_INSTANCE->ppem);
    TTY_LOG_VALUE(TTY_INSTANCE->ppem, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__MUL(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_F26Dot6 n1     = tty__stack_pop_F26Dot6(font);
    TTY_F26Dot6 n2     = tty__stack_pop_F26Dot6(font);
    TTY_F26Dot6 result = TTY_FIX_MUL(n1, n2, 6);
    tty__stack_push_F26Dot6(font, result);
    TTY_LOG_VALUE(result, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__NEG(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_F26Dot6 val = tty__stack_pop_F26Dot6(font);
    tty__stack_push_F26Dot6(font, -val);
    TTY_LOG_VALUE(-val, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__NEQ(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_int32 e2 = tty__stack_pop_uint32(font);
    TTY_int32 e1 = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, e1 != e2 ? 1 : 0);
    TTY_LOG_VALUE(e1 != e2, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__NPUSHB(TTY* font, TTY_Ins_Stream* stream) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    TTY_uint8 count = tty__ins_stream_next(stream);

    do {
        TTY_uint8 byte = tty__ins_stream_next(stream);
        tty__stack_push_uint32(font, byte);
    } while (--count);
}

static void tty__NPUSHW(TTY* font, TTY_Ins_Stream* stream) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    TTY_uint8 count = tty__ins_stream_next(stream);

    do {
        TTY_uint8 ms  = tty__ins_stream_next(stream);
        TTY_uint8 ls  = tty__ins_stream_next(stream);
        TTY_int32 val = (ms << 8) | ls;
        tty__stack_push_int32(font, val);
    } while (--count);
}

static void tty__OR(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_int32 e1 = tty__stack_pop_int32(font);
    TTY_int32 e2 = tty__stack_pop_int32(font);
    tty__stack_push_uint32(font, (e1 != 0 || e2 != 0) ? 1 : 0);
    TTY_LOG_VALUE((e1 != 0 || e2 != 0), TTY_LOG_LEVEL_VERBOSE);
}

static void tty__POP(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    tty__stack_pop_uint32(font);
}

static void tty__PUSHB(TTY* font, TTY_Ins_Stream* stream, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    TTY_uint8 count = tty__get_num_vals_to_push(ins);

    do {
        TTY_uint8 byte = tty__ins_stream_next(stream);
        tty__stack_push_uint32(font, byte);
    } while (--count);
}

static void tty__PUSHW(TTY* font, TTY_Ins_Stream* stream, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    TTY_uint8 count = tty__get_num_vals_to_push(ins);

    do {
        TTY_uint8 ms  = tty__ins_stream_next(stream);
        TTY_uint8 ls  = tty__ins_stream_next(stream);
        TTY_int32 val = (ms << 8) | ls;
        tty__stack_push_int32(font, val);
    } while (--count);
}

static void tty__RCVT(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    
    TTY_uint32 cvtIdx = tty__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTY_FWORD));

    tty__stack_push_F26Dot6(font, TTY_INSTANCE->cvt[cvtIdx]);
    TTY_LOG_VALUE(TTY_INSTANCE->cvt[cvtIdx], TTY_LOG_LEVEL_VERBOSE);
}

static void tty__RDTG(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.roundState = TTY_ROUND_DOWN_TO_GRID;
}

static void tty__ROLL(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 a = tty__stack_pop_uint32(font);
    TTY_uint32 b = tty__stack_pop_uint32(font);
    TTY_uint32 c = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, b);
    tty__stack_push_uint32(font, a);
    tty__stack_push_uint32(font, c);
}

static void tty__ROUND(TTY* font, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    TTY_F26Dot6 dist = tty__stack_pop_F26Dot6(font);
    dist = tty__round(font, dist);
    tty__stack_push_F26Dot6(font, dist);
    TTY_LOG_VALUE(dist, TTY_LOG_LEVEL_MINIMAL);
}

static void tty__RS(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 idx = tty__stack_pop_uint32(font);
    tty__stack_push_int32(font, TTY_INSTANCE->storageArea[idx]);
    TTY_LOG_VALUE(TTY_INSTANCE->storageArea[idx], TTY_LOG_LEVEL_VERBOSE);
}

static void tty__RTG(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.roundState = TTY_ROUND_TO_GRID;
}

static void tty__RTHG(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.roundState = TTY_ROUND_TO_HALF_GRID;
}

static void tty__RUTG(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.roundState = TTY_ROUND_UP_TO_GRID;
}

static void tty__SCANCTRL(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);

    TTY_uint16 flags  = tty__stack_pop_uint32(font);
    TTY_uint8  thresh = flags & 0xFF;
    
    if (thresh == 0xFF) {
        font->gState.scanControl = TTY_TRUE;
    }
    else if (thresh == 0x0) {
        font->gState.scanControl = TTY_FALSE;
    }
    else {
        if (flags & 0x100) {
            if (TTY_INSTANCE->ppem <= thresh) {
                font->gState.scanControl = TTY_TRUE;
            }
        }

        if (flags & 0x200) {
            if (TTY_INSTANCE->isRotated) {
                font->gState.scanControl = TTY_TRUE;
            }
        }

        if (flags & 0x400) {
            if (TTY_INSTANCE->isStretched) {
                font->gState.scanControl = TTY_TRUE;
            }
        }

        if (flags & 0x800) {
            if (thresh > TTY_INSTANCE->ppem) {
                font->gState.scanControl = TTY_FALSE;
            }
        }

        if (flags & 0x1000) {
            if (!TTY_INSTANCE->isRotated) {
                font->gState.scanControl = TTY_FALSE;
            }
        }

        if (flags & 0x2000) {
            if (!TTY_INSTANCE->isStretched) {
                font->gState.scanControl = TTY_FALSE;
            }
        }
    }

    TTY_LOG_VALUE(font->gState.scanControl, TTY_LOG_LEVEL_MINIMAL);
}

static void tty__SCANTYPE(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.scanType = tty__stack_pop_uint32(font);
    TTY_LOG_VALUE(font->gState.scanType, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SCVTCI(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.controlValueCutIn = tty__stack_pop_F26Dot6(font);
    TTY_LOG_VALUE(font->gState.controlValueCutIn, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SDB(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.deltaBase = tty__stack_pop_uint32(font);
    TTY_LOG_VALUE(font->gState.deltaBase, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SDS(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.deltaShift = tty__stack_pop_uint32(font);
    TTY_LOG_VALUE(font->gState.deltaShift, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SHPIX(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_MINIMAL);
    
    TTY_F26Dot6_V2 dist;
    {
        TTY_F26Dot6 amt = tty__stack_pop_F26Dot6(font);
        dist.x = TTY_FIX_MUL(amt, font->gState.freedomVec.x, 14);
        dist.y = TTY_FIX_MUL(amt, font->gState.freedomVec.y, 14);
    }

    for (TTY_uint32 i = 0; i < font->gState.loop; i++) {
        TTY_uint32 pointIdx = tty__stack_pop_uint32(font);
        assert(pointIdx <= font->gState.zp2->cap);

        font->gState.zp2->cur[pointIdx].x      += dist.x;
        font->gState.zp2->cur[pointIdx].y      += dist.y;
        font->gState.zp2->touchFlags[pointIdx] |= font->gState.touchFlags;

        TTY_LOG_POINT(font->gState.zp2->cur[pointIdx], TTY_LOG_LEVEL_MINIMAL);
    }

    font->gState.loop = 1;
}

static void tty__SLOOP(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.loop = tty__stack_pop_uint32(font);
    TTY_LOG_VALUE(font->gState.loop, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SRP0(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.rp0 = tty__stack_pop_uint32(font);
    TTY_LOG_VALUE(font->gState.rp0, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SRP1(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.rp1 = tty__stack_pop_uint32(font);
    TTY_LOG_VALUE(font->gState.rp1, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SRP2(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    font->gState.rp2 = tty__stack_pop_uint32(font);
    TTY_LOG_VALUE(font->gState.rp2, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SUB(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_F26Dot6 n1 = tty__stack_pop_F26Dot6(font);
    TTY_F26Dot6 n2 = tty__stack_pop_F26Dot6(font);
    tty__stack_push_F26Dot6(font, n2 - n1);
    TTY_LOG_VALUE(n2 - n1, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SVTCA(TTY* font, TTY_uint8 ins) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    if (ins & 0x1) {
        font->gState.freedomVec.x = 1l << 14;
        font->gState.freedomVec.y = 0;
        font->gState.touchFlags   = TTY_TOUCH_X;
    }
    else {
        font->gState.freedomVec.x = 0;
        font->gState.freedomVec.y = 1l << 14;
        font->gState.touchFlags   = TTY_TOUCH_Y;
    }

    font->gState.projVec     = font->gState.freedomVec;
    font->gState.dualProjVec = font->gState.freedomVec;
}

static void tty__SWAP(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 e2 = tty__stack_pop_uint32(font);
    TTY_uint32 e1 = tty__stack_pop_uint32(font);
    tty__stack_push_uint32(font, e2);
    tty__stack_push_uint32(font, e1);
}

static void tty__SZPS(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 zone = tty__stack_pop_uint32(font);
    font->gState.zp0 = tty__get_zone_pointer(font, zone);
    font->gState.zp1 = font->gState.zp0;
    font->gState.zp2 = font->gState.zp0;
    TTY_LOG_VALUE(zone, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SZP0(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 zone = tty__stack_pop_uint32(font);
    font->gState.zp0 = tty__get_zone_pointer(font, zone);
    TTY_LOG_VALUE(zone, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SZP1(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 zone = tty__stack_pop_uint32(font);
    font->gState.zp1 = tty__get_zone_pointer(font, zone);
    TTY_LOG_VALUE(zone, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__SZP2(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_uint32 zone = tty__stack_pop_uint32(font);
    font->gState.zp2 = tty__get_zone_pointer(font, zone);
    TTY_LOG_VALUE(zone, TTY_LOG_LEVEL_VERBOSE);
}

static void tty__WCVTF(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    TTY_uint32 funits = tty__stack_pop_uint32(font);
    TTY_uint32 cvtIdx = tty__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTY_FWORD));

    TTY_INSTANCE->cvt[cvtIdx] = TTY_FIX_MUL(funits << 6, TTY_INSTANCE->scale, 22);

    TTY_LOG_VALUE(TTY_INSTANCE->cvt[cvtIdx], TTY_LOG_LEVEL_VERBOSE);
}

static void tty__WCVTP(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);

    TTY_uint32 pixels = tty__stack_pop_uint32(font);
    
    TTY_uint32 cvtIdx = tty__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTY_FWORD));

    TTY_INSTANCE->cvt[cvtIdx] = pixels;
    TTY_LOG_VALUE(TTY_INSTANCE->cvt[cvtIdx], TTY_LOG_LEVEL_VERBOSE);
}

static void tty__WS(TTY* font) {
    TTY_LOG_INS(TTY_LOG_LEVEL_VERBOSE);
    TTY_int32  value = tty__stack_pop_int32(font);
    TTY_uint32 idx   = tty__stack_pop_uint32(font);
    TTY_INSTANCE->storageArea[idx] = value;
    TTY_LOG_VALUE(TTY_INSTANCE->storageArea[idx], TTY_LOG_LEVEL_VERBOSE);
}

static void tty__reset_graphics_state(TTY* font) {
    font->gState.autoFlip          = TTY_TRUE;
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
    font->gState.roundState        = TTY_ROUND_TO_GRID;
    font->gState.scanControl       = TTY_FALSE;
    font->gState.singleWidthCutIn  = 0;
    font->gState.singleWidthValue  = 0;
    font->gState.touchFlags        = TTY_TOUCH_X;
    font->gState.zp0               = &TTY_TEMP.zone1;
    font->gState.zp1               = &TTY_TEMP.zone1;
    font->gState.zp2               = &TTY_TEMP.zone1;
}

static void tty__call_func(TTY* font, TTY_uint32 funcId, TTY_uint32 times) {
    assert(funcId < font->funcArray.count);

    while (times > 0) {
        TTY_Ins_Stream stream;
        tty__ins_stream_init(&stream, font->funcArray.funcs[funcId].firstIns);

        while (TTY_TRUE) {
            TTY_uint8 ins = tty__ins_stream_next(&stream);
            
            if (ins == TTY_ENDF) {
                break;
            }

            tty__execute_ins(font, &stream, ins);
        };

        times--;
    }
}

static TTY_uint8 tty__jump_to_else_or_eif(TTY_Ins_Stream* stream) {
    TTY_uint32 numNested = 0;

    while (TTY_TRUE) {
        TTY_uint8 ins = tty__ins_stream_next(stream);

        if (ins >= TTY_PUSHB && ins <= TTY_PUSHB_MAX) {
            tty__ins_stream_jump(stream, tty__get_num_vals_to_push(ins));
        }
        else if (ins >= TTY_PUSHW && ins <= TTY_PUSHW_MAX) {
            tty__ins_stream_jump(stream, 2 * tty__get_num_vals_to_push(ins));
        }
        else if (ins == TTY_NPUSHB) {
            tty__ins_stream_jump(stream, tty__ins_stream_next(stream));
        }
        else if (ins == TTY_NPUSHW) {
            tty__ins_stream_jump(stream, 2 * tty__ins_stream_next(stream));
        }
        else if (ins == TTY_IF) {
            numNested++;
        }
        else if (numNested == 0) {
            if (ins == TTY_EIF || ins == TTY_ELSE) {
                return ins;
            }
        }
        else if (ins == TTY_EIF) {
            numNested--;
        }
    }

    assert(0);
    return 0;
}

static TTY_F26Dot6 tty__round(TTY* font, TTY_F26Dot6 val) {
    // TODO: No idea how to apply "engine compensation" described in the spec

    switch (font->gState.roundState) {
        case TTY_ROUND_TO_HALF_GRID:
            return (val & 0x3F) | 0x20;
        case TTY_ROUND_TO_GRID:
            return tty__f26dot6_round(val);
        case TTY_ROUND_TO_DOUBLE_GRID:
            // TODO
            assert(0);
            break;
        case TTY_ROUND_DOWN_TO_GRID:
            return tty__f26dot6_floor(val);
        case TTY_ROUND_UP_TO_GRID:
            return tty__f26dot6_ceil(val);
        case TTY_ROUND_OFF:
            // TODO
            assert(0);
            break;
    }
    assert(0);
    return 0;
}

static void tty__move_point(TTY* font, TTY_Zone* zone, TTY_uint32 idx, TTY_F26Dot6 amount) {
    zone->cur[idx].x         += TTY_FIX_MUL(amount, font->gState.freedomVec.x, 14);
    zone->cur[idx].y         += TTY_FIX_MUL(amount, font->gState.freedomVec.y, 14);
    zone->touchFlags[idx] |= font->gState.touchFlags;
}

static TTY_F26Dot6 tty__apply_single_width_cut_in(TTY* font, TTY_F26Dot6 value) {
    TTY_F26Dot6 absDiff = labs(value - font->gState.singleWidthValue);
    if (absDiff < font->gState.singleWidthCutIn) {
        if (value < 0) {
            return -font->gState.singleWidthValue;
        }
        return font->gState.singleWidthValue;
    }
    return value;
}

static TTY_F26Dot6 tty__apply_min_dist(TTY* font, TTY_F26Dot6 value) {
    if (labs(value) < font->gState.minDist) {
        if (value < 0) {
            return -font->gState.minDist;
        }
        return font->gState.minDist;
    }
    return value;
}

static TTY_bool tty__get_delta_value(TTY*         font, 
                                     TTY_uint32   exc, 
                                     TTY_uint8    range, 
                                     TTY_F26Dot6* deltaVal) {
    TTY_uint32 ppem = ((exc & 0xF0) >> 4) + font->gState.deltaBase + range;

    if (TTY_INSTANCE->ppem != ppem) {
        return TTY_FALSE;
    }

    TTY_int8 numSteps = (exc & 0xF) - 8;
    if (numSteps > 0) {
        numSteps++;
    }

    // The result is 26.6 since numSteps already has a scale factor of 1
    *deltaVal = numSteps * (1l << (6 - font->gState.deltaShift));
    return TTY_TRUE;
}

static void tty__IUP_interpolate_or_shift(TTY_Zone*      zone1, 
                                          TTY_Touch_Flag touchFlag, 
                                          TTY_uint16     startPointIdx, 
                                          TTY_uint16     endPointIdx, 
                                          TTY_uint16     touch0, 
                                          TTY_uint16     touch1) {
    #define TTY_IUP_INTERPOLATE(coord)                                                       \
        TTY_F26Dot6 totalDistCur = zone1->cur[touch1].coord - zone1->cur[touch0].coord;      \
        TTY_int32   totalDistOrg = zone1->org[touch1].coord - zone1->org[touch0].coord;      \
        TTY_int32   orgDist      = zone1->org[i].coord      - zone1->org[touch0].coord;      \
                                                                                             \
        TTY_F10Dot22 scale   = tty__rounded_div((TTY_int64)totalDistCur << 16, totalDistOrg);\
        TTY_F26Dot6  newDist = TTY_FIX_MUL(orgDist << 6, scale, 22);                         \
        zone1->cur[i].coord  = zone1->cur[touch0].coord + newDist;                           \
                                                                                             \
        TTY_LOG_CUSTOM_F(TTY_LOG_LEVEL_MINIMAL, "Interp %3d: %5d", i, zone1->cur[i].coord);

    #define TTY_IUP_SHIFT(coord)\
        TTY_int32 diff0 = labs(zone1->org[touch0].coord - zone1->org[i].coord);           \
        TTY_int32 diff1 = labs(zone1->org[touch1].coord - zone1->org[i].coord);           \
                                                                                          \
        if (diff0 < diff1) {                                                              \
            TTY_int32 diff = zone1->cur[touch0].coord - zone1->orgScaled[touch0].coord;   \
            zone1->cur[i].coord += diff;                                                  \
        }                                                                                 \
        else {                                                                            \
            TTY_int32 diff = zone1->cur[touch1].coord - zone1->orgScaled[touch1].coord;   \
            zone1->cur[i].coord += diff;                                                  \
        }                                                                                 \
        TTY_LOG_CUSTOM_F(TTY_LOG_LEVEL_MINIMAL, "Shift %3d: %5d", i, zone1->cur[i].coord);

    #define TTY_IUP_INTERPOLATE_OR_SHIFT                                 \
        if (touchFlag == TTY_TOUCH_X) {                                  \
            if (coord0 <= zone1->org[i].x && zone1->org[i].x <= coord1) {\
                TTY_IUP_INTERPOLATE(x);                                  \
            }                                                            \
            else {                                                       \
                TTY_IUP_SHIFT(x)                                         \
            }                                                            \
        }                                                                \
        else {                                                           \
            if (coord0 <= zone1->org[i].y && zone1->org[i].y <= coord1) {\
                TTY_IUP_INTERPOLATE(y);                                  \
            }                                                            \
            else {                                                       \
                TTY_IUP_SHIFT(y)                                         \
            }                                                            \
        }

    TTY_int32 coord0, coord1;

    if (touchFlag == TTY_TOUCH_X) {
        tty__max_min(zone1->org[touch0].x, zone1->org[touch1].x, &coord1, &coord0);
    }
    else {
        tty__max_min(zone1->org[touch0].y, zone1->org[touch1].y, &coord1, &coord0);
    }

    if (touch0 >= touch1) {
        for (TTY_uint32 i = touch0 + 1; i <= endPointIdx; i++) {
            TTY_IUP_INTERPOLATE_OR_SHIFT
        }

        for (TTY_uint32 i = startPointIdx; i < touch1; i++) {
            TTY_IUP_INTERPOLATE_OR_SHIFT
        } 
    }
    else {
        for (TTY_uint32 i = touch0 + 1; i < touch1; i++) {
            TTY_IUP_INTERPOLATE_OR_SHIFT
        }
    }

    #undef TTY_IUP_SHIFT
    #undef TTY_IUP_INTERPOLATE_OR_SHIFT
}


/* ------------------ */
/* Utility Operations */
/* ------------------ */
static TTY_uint16 tty__get_uint16(TTY_uint8* data) {
    return data[0] << 8 | data[1];
}

static TTY_uint32 tty__get_uint32(TTY_uint8* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static TTY_int16 tty__get_int16(TTY_uint8* data) {
    return data[0] << 8 | data[1];
}

static void tty__max_min(TTY_int32 a, TTY_int32 b, TTY_int32* max, TTY_int32* min) {
    if (a > b) {
        *max = a;
        *min = b;
    }
    else {
        *max = b;
        *min = a;
    }
}

static TTY_int32 tty__min(TTY_int32 a, TTY_int32 b) {
    return a < b ? a : b;
}

static TTY_int32 tty__max(TTY_int32 a, TTY_int32 b) {
    return a > b ? a : b;
}

static TTY_uint16 tty__get_upem(TTY* font) {
    return tty__get_uint16(font->data + font->head.off + 18);
}

static TTY_uint16 tty__get_glyph_x_advance(TTY* font, TTY_uint32 glyphIdx) {
    TTY_uint8* hmtxData    = font->data + font->hmtx.off;
    TTY_uint16 numHMetrics = tty__get_uint16(font->data + font->hhea.off + 48);
    if (glyphIdx < numHMetrics) {
        return tty__get_uint16(hmtxData + 4 * glyphIdx);
    }
    return 0;
}

static TTY_int16 tty__get_glyph_left_side_bearing(TTY* font, TTY_uint32 glyphIdx) {
    TTY_uint8* hmtxData    = font->data + font->hmtx.off;
    TTY_uint16 numHMetrics = tty__get_uint16(font->data + font->hhea.off + 48);
    if (glyphIdx < numHMetrics) {
        return tty__get_int16(hmtxData + 4 * glyphIdx + 2);
    }
    return tty__get_int16(hmtxData + 4 * numHMetrics + 2 * (numHMetrics - glyphIdx));
}

static void tty__get_glyph_vmetrics(TTY*       font, 
                                    TTY_uint8* glyfBlock, 
                                    TTY_int32* ah, 
                                    TTY_int32* tsb) {
    if (font->vmtx.exists) {
        // TODO: Get vertical phantom points from vmtx
        assert(0);
    }
    else  {
        TTY_int16 yMax = tty__get_int16(glyfBlock + 8);
        *ah  = font->ascender - font->descender;
        *tsb = font->ascender - yMax;
    }
}

static TTY_uint16 tty__get_cvt_cap(TTY* font) {
    return font->cvt.size / sizeof(TTY_FWORD);
}

static TTY_uint16 tty__get_storage_area_cap(TTY* font) {
    // TODO: make sure maxp has version 1.0
    return tty__get_uint16(font->data + font->maxp.off + 18);   
}

static TTY_Zone* tty__get_zone_pointer(TTY* font, TTY_uint32 zone) {
    switch (zone) {
        case 0:
            return &TTY_INSTANCE->zone0;
        case 1:
            return &TTY_TEMP.zone1;
    }
    assert(0);
    return NULL;
}


/* ---------------- */
/* Fixed-point Math */
/* ---------------- */
static TTY_int64 tty__rounded_div(TTY_int64 a, TTY_int64 b) {
    // https://stackoverflow.com/a/18067292
    if (b == 0) {
        return 0;
    }
    return (a < 0) ^ (b < 0) ? (a - b / 2) / b : (a + b / 2) / b;
}

static TTY_int32 tty__fix_div(TTY_int32 a, TTY_int32 b, TTY_uint8 shift) {
    TTY_int64 q = tty__rounded_div((TTY_int64)a << 31, b);
    shift = 31 - shift;
    return TTY_ROUNDED_DIV_POW2(q, shift);
}

static void tty__fix_v2_add(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* result) {
    result->x = a->x + b->x;
    result->y = a->y + b->y;
}

static void tty__fix_v2_sub(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* result) {
    result->x = a->x - b->x;
    result->y = a->y - b->y;
}

static void tty__fix_v2_mul(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* result, TTY_uint8 shift) {
    result->x = TTY_FIX_MUL(a->x, b->x, shift);
    result->y = TTY_FIX_MUL(a->y, b->y, shift);
}

static void tty__fix_v2_div(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* result, TTY_uint8 shift) {
    result->x = tty__fix_div(a->x, b->x, shift);
    result->y = tty__fix_div(a->y, b->y, shift);
}

static TTY_int32 tty__fix_v2_dot(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_uint8 shift) {
    return TTY_FIX_MUL(a->x, b->x, shift) + TTY_FIX_MUL(a->y, b->y, shift);
}

static TTY_int32 tty__fix_v2_sub_dot(TTY_Fix_V2* a, TTY_Fix_V2* b, TTY_Fix_V2* c, TTY_uint8 shift) {
    TTY_Fix_V2 diff;
    tty__fix_v2_sub(a, b, &diff);
    return tty__fix_v2_dot(&diff, c, shift);
}

static void tty__fix_v2_scale(TTY_Fix_V2* v, TTY_int32 scale, TTY_uint8 shift) {
    v->x = TTY_FIX_MUL(v->x, scale, shift);
    v->y = TTY_FIX_MUL(v->y, scale, shift);
}

static TTY_F26Dot6 tty__f26dot6_round(TTY_F26Dot6 val) {
    return ((val & 0x20) << 1) + (val & 0xFFFFFFC0);
}

static TTY_F26Dot6 tty__f26dot6_ceil(TTY_F26Dot6 val) {
    return (val & 0x3F) ? (val & 0xFFFFFFC0) + 0x40 : val;
}

static TTY_F26Dot6 tty__f26dot6_floor(TTY_F26Dot6 val) {
    return val & 0xFFFFFFC0;
}

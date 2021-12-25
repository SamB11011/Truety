#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "truety.h"

/* -------------------------- */
/* Initialization and Cleanup */
/* -------------------------- */
static TTY_bool tty_read_file_into_buffer(TTY* font, const char* path);

static TTY_bool tty_extract_info_from_table_directory(TTY* font);

static TTY_bool tty_extract_char_encoding(TTY* font);

static TTY_bool tty_format_is_supported(TTY_uint16 format);

static void tty_extract_vmetrics(TTY* font);

static TTY_bool tty_interpreter_init(TTY* font);

static TTY_bool tty_unhinted_init(TTY* font, TTY_Unhinted* unhinted, TTY_uint32 numOutlinePoints);

static TTY_bool tty_zone0_init(TTY* font, TTY_Zone* zone);

static TTY_bool tty_zone1_init(TTY* font, TTY_Zone* zone, TTY_uint32 numOutlinePoints);

static void tty_interpreter_free(TTY_Interp* interp);

static void tty_unhinted_free(TTY_Unhinted* unhinted);

static void tty_zone_free(TTY_Zone* zone);


/* ------------------- */
/* Glyph Index Mapping */
/* ------------------- */
static TTY_uint16 tty_get_glyph_index_format_4(TTY_uint8* subtable, TTY_uint32 cp);


/* ---------------- */
/* glyf Table Stuff */
/* ---------------- */
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

static TTY_bool tty_get_glyf_data_block(TTY* font, TTY_uint8** block, TTY_uint32 glyphIdx);

static TTY_bool tty_extract_glyph_points(TTY*            font, 
                                         TTY_Instance*   instance, 
                                         TTY_Glyph_Data* glyphData);

static TTY_int32 tty_get_next_coord_off(TTY_uint8** data, 
                                        TTY_uint8   dualFlag, 
                                        TTY_uint8   shortFlag, 
                                        TTY_uint8   flags);

static void tty_scale_glyph_points(TTY_F26Dot6_V2* scaledPoints, 
                                   TTY_V2*         points, 
                                   TTY_uint32      numPoints, 
                                   TTY_F10Dot22    scale);

static void tty_round_phantom_points(TTY_F26Dot6_V2* phantomPoints);


/* --------- */
/* Rendering */
/* --------- */
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

static TTY_bool tty_render_glyph_internal(TTY*          font,
                                          TTY_Instance* instance,
                                          TTY_Glyph*    glyph,
                                          TTY_Image*    image,
                                          TTY_uint32    x,
                                          TTY_uint32    y);

static TTY_bool tty_render_simple_glyph(TTY*            font, 
                                        TTY_Instance*   instance, 
                                        TTY_Glyph_Data* glyphData,
                                        TTY_Image*      image,
                                        TTY_uint32      x,
                                        TTY_uint32      y);

static void tty_get_max_and_min_points(TTY_F26Dot6_V2* points, 
                                       TTY_uint32      numPoints, 
                                       TTY_F26Dot6_V2* max, 
                                       TTY_F26Dot6_V2* min);

static void tty_set_hinted_glyph_metrics(TTY_Glyph*      glyph,
                                         TTY_F26Dot6_V2* phantomPoints,
                                         TTY_F26Dot6_V2* max, 
                                         TTY_F26Dot6_V2* min);

static void tty_set_unhinted_glyph_metrics(TTY_Glyph*      glyph,
                                           TTY_F26Dot6_V2* phantomPoints,
                                           TTY_F26Dot6_V2* max, 
                                           TTY_F26Dot6_V2* min);

static TTY_Curve* tty_convert_points_into_curves(TTY_Glyph_Data* glyphData, 
                                                 TTY_F26Dot6_V2* points, 
                                                 TTY_Point_Type* pointTypes,
                                                 TTY_uint32      numPoints,
                                                 TTY_uint32*     numCurves);

static TTY_Edge* tty_subdivide_curves_into_edges(TTY_Curve*  curves, 
                                                 TTY_uint32  numCurves, 
                                                 TTY_uint32* numEdges);

static void tty_subdivide_curve_into_edges(TTY_F26Dot6_V2* p0, 
                                           TTY_F26Dot6_V2* p1, 
                                           TTY_F26Dot6_V2* p2, 
                                           TTY_int8        dir, 
                                           TTY_Edge*       edges, 
                                           TTY_uint32*     numEdges);

static void tty_edge_init(TTY_Edge* edge, TTY_F26Dot6_V2* p0, TTY_F26Dot6_V2* p1, TTY_int8 dir);

static TTY_F16Dot16 tty_get_inv_slope(TTY_F26Dot6_V2* p0, TTY_F26Dot6_V2* p1);

static int tty_compare_edges(const void* edge0, const void* edge1);

static TTY_F26Dot6 tty_get_edge_scanline_x_intersection(TTY_Edge* edge, TTY_F26Dot6 scanline);

static TTY_bool tty_active_edge_list_init(TTY_Active_Edge_List* list);

static void tty_active_edge_list_free(TTY_Active_Edge_List* list);

static TTY_Active_Edge* tty_get_available_active_edge(TTY_Active_Edge_List* list);

static TTY_Active_Edge* tty_insert_active_edge_first(TTY_Active_Edge_List* list);

static TTY_Active_Edge* tty_insert_active_edge_after(TTY_Active_Edge_List* list, 
                                                     TTY_Active_Edge*      after);

static void tty_remove_active_edge(TTY_Active_Edge_List* list, 
                                   TTY_Active_Edge*     prev, 
                                   TTY_Active_Edge*     remove);

static void tty_swap_active_edge_with_next(TTY_Active_Edge_List* list, 
                                           TTY_Active_Edge*     prev, 
                                           TTY_Active_Edge*     edge);


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
    TTY_SHP       = 0x32,
    TTY_SHP_MAX   = 0x33,
    TTY_SHPIX     = 0x38,
    TTY_SLOOP     = 0x17,
    TTY_SMD       = 0x1A,
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

static void tty_execute_font_program(TTY* font);

static void tty_execute_cv_program(TTY* font, TTY_Instance* instance);

static TTY_bool tty_execute_glyph_program(TTY*            font, 
                                          TTY_Instance*   instance, 
                                          TTY_Glyph_Data* glyphData);

static void tty_execute_next_font_program_ins(TTY_Interp* interp);

static void tty_execute_next_cv_program_ins(TTY_Interp* interp);

static void tty_execute_next_glyph_program_ins(TTY_Interp* interp);

static TTY_bool tty_try_execute_shared_ins(TTY_Interp* interp, TTY_uint8 ins);

static void tty_ins_stream_init(TTY_Ins_Stream* stream, TTY_uint8* buffer);

static TTY_uint8 tty_ins_stream_next(TTY_Ins_Stream* stream);

static TTY_uint8 tty_ins_stream_peek(TTY_Ins_Stream* stream);

static void tty_ins_stream_consume(TTY_Ins_Stream* stream);

static void tty_ins_stream_jump(TTY_Ins_Stream* stream, TTY_int32 count);

static void tty_stack_clear(TTY_Stack* stack);

static void tty_stack_push(TTY_Stack* stack, TTY_int32 val);

static TTY_int32 tty_stack_pop(TTY_Stack* stack);

static void tty_reset_graphics_state(TTY_Interp* interp);

static void tty_ABS(TTY_Interp* interp);

static void tty_ADD(TTY_Interp* interp);

static void tty_ALIGNRP(TTY_Interp* interp);

static void tty_AND(TTY_Interp* interp);

static void tty_CALL(TTY_Interp* interp);

static void tty_CINDEX(TTY_Interp* interp);

static void tty_DELTAC1(TTY_Interp* interp);

static void tty_DELTAC2(TTY_Interp* interp);

static void tty_DELTAC3(TTY_Interp* interp);

static void tty_DELTAP1(TTY_Interp* interp);

static void tty_DELTAP2(TTY_Interp* interp);

static void tty_DELTAP3(TTY_Interp* interp);

static void tty_DEPTH(TTY_Interp* interp);

static void tty_DIV(TTY_Interp* interp);

static void tty_DUP(TTY_Interp* interp);

static void tty_EQ(TTY_Interp* interp);

static void tty_FDEF(TTY_Interp* interp);

static void tty_FLOOR(TTY_Interp* interp);

static void tty_GC(TTY_Interp* interp, TTY_uint8 ins);

static void tty_GETINFO(TTY_Interp* interp);

static void tty_GPV(TTY_Interp* interp);

static void tty_GT(TTY_Interp* interp);

static void tty_GTEQ(TTY_Interp* interp);

static void tty_IDEF(TTY_Interp* interp);

static void tty_IF(TTY_Interp* interp);

static void tty_IP(TTY_Interp* interp);

static void tty_IUP(TTY_Interp* interp, TTY_uint8 ins);

static void tty_JROT(TTY_Interp* interp);

static void tty_JMPR(TTY_Interp* interp);

static void tty_LOOPCALL(TTY_Interp* interp);

static void tty_LT(TTY_Interp* interp);

static void tty_LTEQ(TTY_Interp* interp);

static void tty_MD(TTY_Interp* interp, TTY_uint8 ins);

static void tty_MDAP(TTY_Interp* interp, TTY_uint8 ins);

static void tty_MDRP(TTY_Interp* interp, TTY_uint8 ins);

static void tty_MIAP(TTY_Interp* interp, TTY_uint8 ins);

static void tty_MINDEX(TTY_Interp* interp);

static void tty_MIRP(TTY_Interp* interp, TTY_uint8 ins);

static void tty_MPPEM(TTY_Interp* interp);

static void tty_MUL(TTY_Interp* interp);

static void tty_NEG(TTY_Interp* interp);

static void tty_NEQ(TTY_Interp* interp);

static void tty_NPUSHB(TTY_Interp* interp);

static void tty_NPUSHW(TTY_Interp* interp);

static void tty_OR(TTY_Interp* interp);

static void tty_POP(TTY_Interp* interp);

static void tty_PUSHB(TTY_Interp* interp, TTY_uint8 ins);

static void tty_PUSHW(TTY_Interp* interp, TTY_uint8 ins);

static void tty_RCVT(TTY_Interp* interp);

static void tty_RDTG(TTY_Interp* interp);

static void tty_ROLL(TTY_Interp* interp);

static void tty_ROUND(TTY_Interp* interp, TTY_uint8 ins);

static void tty_RS(TTY_Interp* interp);

static void tty_RTG(TTY_Interp* interp);

static void tty_RTHG(TTY_Interp* interp);

static void tty_RUTG(TTY_Interp* interp);

static void tty_SCANCTRL(TTY_Interp* interp);

static void tty_SCANTYPE(TTY_Interp* interp);

static void tty_SCVTCI(TTY_Interp* interp);

static void tty_SDB(TTY_Interp* interp);

static void tty_SDS(TTY_Interp* interp);

static void tty_SHP(TTY_Interp* interp, TTY_uint8 ins);

static void tty_SHPIX(TTY_Interp* interp);

static void tty_SLOOP(TTY_Interp* interp);

static void tty_SMD(TTY_Interp* interp);

static void tty_SRP0(TTY_Interp* interp);

static void tty_SRP1(TTY_Interp* interp);

static void tty_SRP2(TTY_Interp* interp);

static void tty_SUB(TTY_Interp* interp);

static void tty_SVTCA(TTY_Interp* interp, TTY_uint8 ins);

static void tty_SWAP(TTY_Interp* interp);

static void tty_SZPS(TTY_Interp* interp);

static void tty_SZP0(TTY_Interp* interp);

static void tty_SZP1(TTY_Interp* interp);

static void tty_SZP2(TTY_Interp* interp);

static void tty_WCVTF(TTY_Interp* interp);

static void tty_WCVTP(TTY_Interp* interp);

static void tty_WS(TTY_Interp* interp);

static void tty_call_func(TTY_Interp* interp, TTY_uint32 funcId, TTY_uint32 times);

static void tty_push_bytes(TTY_Interp* interp, TTY_uint32 count);

static void tty_push_words(TTY_Interp* interp, TTY_uint32 count);

static TTY_uint8 tty_jump_to_else_or_eif(TTY_Ins_Stream* stream);

static TTY_int32 tty_proj(TTY_Interp* interp, TTY_Fix_V2* v);

static TTY_int32 tty_dual_proj(TTY_Interp* interp, TTY_Fix_V2* v);

static TTY_int32 tty_sub_proj(TTY_Interp* interp, TTY_Fix_V2* a, TTY_Fix_V2* b);

static TTY_int32 tty_sub_dual_proj(TTY_Interp* interp, TTY_Fix_V2* a, TTY_Fix_V2* b);

static void tty_move_point(TTY_Interp* interp, TTY_Zone* zone, TTY_uint32 idx, TTY_F26Dot6 dist);

static TTY_F26Dot6 tty_round(TTY_Interp* interp, TTY_F26Dot6 val);

static TTY_F26Dot6 tty_apply_single_width_cut_in(TTY_Interp* interp, TTY_F26Dot6 value);

static TTY_F26Dot6 tty_apply_min_dist(TTY_Interp* interp, TTY_F26Dot6 value);

static void tty_deltac(TTY_Interp* interp, TTY_uint8 range);

static void tty_deltap(TTY_Interp* interp, TTY_uint8 range);

static TTY_bool tty_get_delta_value(TTY_Interp*   interp,
                                    TTY_uint32    exc, 
                                    TTY_uint8     range, 
                                    TTY_F26Dot6*  deltaVal);

static void tty_iup_interpolate_or_shift(TTY_Zone*      zone1, 
                                         TTY_Touch_Flag touchFlag, 
                                         TTY_uint16     startPointIdx, 
                                         TTY_uint16     endPointIdx, 
                                         TTY_uint16     touch0, 
                                         TTY_uint16     touch1);

static TTY_Zone* tty_get_zone_pointer(TTY_Interp* interp, TTY_uint32 zone);


/* ---------------- */
/* Fixed-point Math */
/* ---------------- */
#define TTY_ROUNDED_DIV_POW2(a, addend, shift)\
    (((a) + (addend)) >> (shift))

#define TTY_FIX_DIV(a, b, numerShift, addend, shift)\
    TTY_ROUNDED_DIV_POW2(tty_rounded_div((TTY_int64)(a) << (numerShift), b), addend, shift)

#define TTY_F26DOT6_DIV(a, b)\
    TTY_FIX_DIV(a, b, 31, 0x1000000, 25)

#define TTY_F2DOT14_DIV(a, b)\
    TTY_FIX_DIV(a, b, 31, 0x10000, 17)

#define TTY_F16DOT16_DIV(a, b)\
    TTY_FIX_DIV(a, b, 31, 0x4000, 15)

#define TTY_F10DOT22_DIV(a, b)\
    TTY_FIX_DIV(a, b, 31, 0x100, 9)


#define TTY_FIX_MUL(a, b, addend, shift)\
    TTY_ROUNDED_DIV_POW2((TTY_uint64)(a) * (TTY_uint64)(b), addend, shift)

#define TTY_F26DOT6_MUL(a, b)\
    TTY_FIX_MUL(a, b, 0x20, 6)

#define TTY_F2DOT14_MUL(a, b)\
    TTY_FIX_MUL(a, b, 0x2000, 14)

#define TTY_F16DOT16_MUL(a, b)\
    TTY_FIX_MUL(a, b, 0x8000, 16)

#define TTY_F10DOT22_MUL(a, b)\
    TTY_FIX_MUL(a, b, 0x200000, 22)


#define TTY_FIX_V2_ADD(a, b, result)  \
    {                                 \
        (result)->x = (a)->x + (b)->x;\
        (result)->y = (a)->y + (b)->y;\
    }

#define TTY_FIX_V2_SUB(a, b, result)  \
    {                                 \
        (result)->x = (a)->x - (b)->x;\
        (result)->y = (a)->y - (b)->y;\
    }

#define TTY_F26DOT6_V2_SCALE(v, scale, result)       \
    {                                                \
        (result)->x = TTY_F26DOT6_MUL((v)->x, scale);\
        (result)->y = TTY_F26DOT6_MUL((v)->y, scale);\
    }


static TTY_int64 tty_rounded_div(TTY_int64 a, TTY_int64 b);

static TTY_F26Dot6 tty_f26dot6_round(TTY_F26Dot6 val);

static TTY_F26Dot6 tty_f26dot6_ceil(TTY_F26Dot6 val);

static TTY_F26Dot6 tty_f26dot6_floor(TTY_F26Dot6 val);


/* ---- */
/* Util */
/* ---- */
#define TTY_FREE_AND_NULLIFY(data)\
    {                             \
        free(data);               \
        data = NULL;              \
    }

#define tty_get_offset16(data)       tty_get_uint16(data)
#define tty_get_offset32(data)       tty_get_uint32(data)
#define tty_get_version16dot16(data) tty_get_uint32(data)

static TTY_uint16 tty_get_uint16(TTY_uint8* data);

static TTY_uint32 tty_get_uint32(TTY_uint8* data);

static TTY_int16 tty_get_int16(TTY_uint8* data);

static TTY_int32 tty_min(TTY_int32 a, TTY_int32 b);

static void tty_max_min(TTY_int32 a, TTY_int32 b, TTY_int32* max, TTY_int32* min);

static TTY_uint16 tty_get_glyph_advance_width(TTY* font, TTY_uint32 glyphIdx);

static TTY_int16 tty_get_glyph_left_side_bearing(TTY* font, TTY_uint32 glyphIdx);

static TTY_int32 tty_get_glyph_advance_height(TTY* font);

static TTY_int32 tty_get_glyph_top_side_bearing(TTY* font, TTY_int16 yMax);


/* --------------------- */
/* Debugging and Logging */
/* --------------------- */
#define TTY_DEBUG
// #define TTY_LOGGING

#ifdef TTY_DEBUG
    #define TTY_ASSERT(cond) assert(cond)
    
    #define TTY_FIX_TO_FLOAT(val, shift) tty_fix_to_float(val, shift)
    
    float tty_fix_to_float(TTY_int32 val, TTY_int32 shift) {
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
    #define TTY_ASSERT(cond)
    #define TTY_FIX_TO_FLOAT(val, shift)
#endif

#ifdef TTY_LOGGING
    static int tty_insCount = 0;

    #define TTY_LOG_PROGRAM(program)      \
        printf("\n--- %s ---\n", program);\
        tty_insCount = 0

    #define TTY_LOG_UNKNOWN_INS(ins)\
        printf("Unknown instruction: %#X\n", ins)

    #define TTY_LOG_INS()\
        printf("%d) %s\n", tty_insCount++, __func__ + 4)

    #define TTY_LOG_POINT(point)\
        printf("\t(%d, %d)\n", (point).x, (point).y)

    #define TTY_LOG_VALUE(val)\
        printf("\t%d\n", val)

    #define TTY_LOG_PUSHED_VALUE(stack)\
        TTY_LOG_VALUE(stack.buffer[stack.count - 1])

    #define TTY_LOG_CUSTOM_F(format, ...)\
        printf("\t"format"\n", __VA_ARGS__)
#else
    #define TTY_LOG_UNKNOWN_INS(ins)
    #define TTY_LOG_PROGRAM(program)
    #define TTY_LOG_INS()
    #define TTY_LOG_POINT(point)
    #define TTY_LOG_VALUE(val)
    #define TTY_LOG_PUSHED_VALUE(stack)
    #define TTY_LOG_CUSTOM_F(format, ...)
#endif


/* ---------------- */
/* Public Functions */
/* ---------------- */
TTY_bool tty_init(TTY* font, const char* path) {
    memset(font, 0, sizeof(TTY));

    if (!tty_read_file_into_buffer(font, path)) {
        goto init_failure;
    }
    
    if (tty_get_uint32(font->data) != 0x00010000) {
        // The font doesn't contain TrueType outlines
        goto init_failure;
    }

    if (!tty_extract_info_from_table_directory(font)) {
        goto init_failure;
    }

    if (!tty_extract_char_encoding(font)) {
        goto init_failure;
    }

    font->upem = tty_get_uint16(font->data + font->head.off + 18);

    tty_extract_vmetrics(font);

    if (font->hasHinting) {
        if (!tty_interpreter_init(font)) {
            goto init_failure;
        }
        tty_execute_font_program(font);
    }

    return TTY_TRUE;

init_failure:
    tty_free(font);
    return TTY_FALSE;
}

TTY_bool tty_instance_init(TTY* font, TTY_Instance* instance, TTY_uint32 ppem, TTY_bool useHinting) {
    memset(instance, 0, sizeof(TTY_Instance));
    
    instance->scale       = tty_rounded_div((TTY_int64)ppem << 22, font->upem);
    instance->ppem        = ppem;
    instance->useHinting  = font->hasHinting && useHinting;
    instance->isRotated   = TTY_FALSE;
    instance->isStretched = TTY_FALSE;

    if (!instance->useHinting) {
        return TTY_TRUE;
    }
    
    if (!tty_zone0_init(font, &instance->zone0)) {
        return TTY_FALSE;
    }
    
    {
        instance->cvt.cap     = font->cvt.size / sizeof(TTY_FWORD);
        instance->storage.cap = tty_get_uint16(font->data + font->maxp.off + 18);
        
        size_t cvtSize   = instance->cvt.cap     * sizeof(TTY_F26Dot6);
        size_t storeSize = instance->storage.cap * sizeof(TTY_int32);
        
        instance->mem = malloc(cvtSize + storeSize);
        if (instance->mem == NULL) {
            tty_zone_free(&instance->zone0);
            return TTY_FALSE;
        }
        
        instance->cvt.buffer     = (TTY_F26Dot6*)(instance->mem);
        instance->storage.buffer = (TTY_int32*)  (instance->mem + cvtSize);
    }

    {
        // Convert default CVT values from FUnits to pixels
        TTY_uint32 idx = 0;
        TTY_uint8* cvt = font->data + font->cvt.off;

        for (TTY_uint32 off = 0; off < font->cvt.size; off += 2) {
            TTY_int32 funits = tty_get_int16(cvt + off);
            instance->cvt.buffer[idx++] = TTY_F10DOT22_MUL(funits << 6, instance->scale);
        }
    }

    tty_execute_cv_program(font, instance);
    return TTY_TRUE;
}

void tty_glyph_init(TTY* font, TTY_Glyph* glyph, TTY_uint32 glyphIdx) {
    memset(glyph, 0, sizeof(TTY_Glyph));
    glyph->idx = glyphIdx;
}

TTY_bool tty_image_init(TTY_Image* image, TTY_uint8* pixels, TTY_uint32 w, TTY_uint32 h) {
    if (pixels == NULL) {
        image->pixels = calloc(w * h, 1);
    }
    image->w = w;
    image->h = h;
    return image->pixels != NULL;
}

void tty_free(TTY* font) {
    if (font) {
        TTY_FREE_AND_NULLIFY(font->data);
        
        if (font->hasHinting) {
            tty_interpreter_free(&font->interp);
        }
    }
}

void tty_instance_free(TTY_Instance* instance) {
    if (instance) {
        if (instance->useHinting) {
            tty_zone_free(&instance->zone0);
        }
        TTY_FREE_AND_NULLIFY(instance->mem);
    }
}

void tty_image_free(TTY_Image* image) {
    if (image) {
        TTY_FREE_AND_NULLIFY(image->pixels);
    }
}

TTY_uint32 tty_get_glyph_index(TTY* font, TTY_uint32 cp) {
    TTY_uint8* subtable = font->data + font->cmap.off + font->encoding.off;

    switch (tty_get_uint16(subtable)) {
        case 4:
            return tty_get_glyph_index_format_4(subtable, cp);
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

    TTY_ASSERT(0);
    return 0;
}

TTY_uint16 tty_get_num_glyphs(TTY* font) {
    return tty_get_uint16(font->data + font->maxp.off + 4);
}

TTY_int32 tty_get_ascender(TTY* font, TTY_Instance* instance) {
    return tty_f26dot6_ceil(TTY_F10DOT22_MUL(font->ascender << 6, instance->scale)) >> 6;
}

TTY_int32 tty_get_descender(TTY* font, TTY_Instance* instance) {
    return tty_f26dot6_ceil(TTY_F10DOT22_MUL(font->descender << 6, instance->scale)) >> 6;
}

TTY_int32 tty_get_line_gap(TTY* font, TTY_Instance* instance) {
    return tty_f26dot6_ceil(TTY_F10DOT22_MUL(font->lineGap << 6, instance->scale)) >> 6;
}

TTY_int32 tty_get_new_line_offset(TTY* font, TTY_Instance* instance) {
    TTY_int32 offset = font->lineGap + font->ascender - font->descender;
    return tty_f26dot6_ceil(TTY_F10DOT22_MUL(offset << 6, instance->scale)) >> 6;
}

TTY_int32 tty_get_max_horizontal_extent(TTY* font, TTY_Instance* instance) {
    TTY_int32 extent = tty_get_int16(font->data + font->hhea.off + 16);
    return tty_f26dot6_ceil(TTY_F10DOT22_MUL(extent << 6, instance->scale)) >> 6;
}

TTY_bool tty_render_glyph(TTY*          font,
                          TTY_Instance* instance,
                          TTY_Glyph*    glyph,
                          TTY_Image*    image) {
    memset(image, 0, sizeof(TTY_Image));
    return tty_render_glyph_internal(font, instance, glyph, image, 0, 0);
}

TTY_bool tty_render_glyph_to_existing_image(TTY*          font, 
                                            TTY_Instance* instance,
                                            TTY_Glyph*    glyph,
                                            TTY_Image*    image,  
                                            TTY_uint32    x, 
                                            TTY_uint32    y) {
    return tty_render_glyph_internal(font, instance, glyph, image, x, y);
}


/* -------------------------- */
/* Initialization and Cleanup */
/* -------------------------- */
static TTY_bool tty_read_file_into_buffer(TTY* font, const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return TTY_FALSE;
    }
    
    if (fseek(f, 0, SEEK_END)) {
        goto read_failure;
    }
    
    if ((font->size = ftell(f)) == -1L) {
        goto read_failure;
    }
    
    if (fseek(f, 0, SEEK_SET)) {
        goto read_failure;
    }
    
    font->data = malloc(font->size);
    if (font->data == NULL) {
        goto read_failure;
    }

    if (fread(font->data, 1, font->size, f) != font->size) {
        goto read_failure;
    }
    
    return fclose(f) == 0 ? TTY_TRUE : TTY_FALSE;
    
read_failure:
    fclose(f);
    return TTY_FALSE;
}

static TTY_bool tty_extract_info_from_table_directory(TTY* font) {
    #define TTY_TAG_EQUALS(val) !memcmp(tag, val, 4)
    
    TTY_uint16 numTables = tty_get_uint16(font->data + 4);

    for (TTY_uint16 i = 0; i < numTables; i++) {
        TTY_uint8* record = font->data + (12 + 16 * i);
        TTY_Table* table  = NULL;

        TTY_uint8 tag[4];
        memcpy(tag, record, 4);

        if (!font->cmap.exists && TTY_TAG_EQUALS("cmap")) {
            table = &font->cmap;
        }
        else if (!font->cvt.exists && TTY_TAG_EQUALS("cvt ")) {
            table = &font->cvt;
        }
        else if (!font->fpgm.exists && TTY_TAG_EQUALS("fpgm")) {
            table = &font->fpgm;
        }
        else if (!font->glyf.exists && TTY_TAG_EQUALS("glyf")) {
            table = &font->glyf;
        }
        else if (!font->head.exists && TTY_TAG_EQUALS("head")) {
            table = &font->head;
        }
        else if (!font->hhea.exists && TTY_TAG_EQUALS("hhea")) {
            table = &font->hhea;
        }
        else if (!font->hmtx.exists && TTY_TAG_EQUALS("hmtx")) {
            table = &font->hmtx;
        }
        else if (!font->loca.exists && TTY_TAG_EQUALS("loca")) {
            table = &font->loca;
        }
        else if (!font->maxp.exists && TTY_TAG_EQUALS("maxp")) {
            table = &font->maxp;
        }
        else if (!font->OS2.exists && TTY_TAG_EQUALS("OS/2")) {
            table = &font->OS2;
        }
        else if (!font->prep.exists && TTY_TAG_EQUALS("prep")) {
            table = &font->prep;
        }
        else if (!font->vmtx.exists && TTY_TAG_EQUALS("vmtx")) {
            table = &font->vmtx;
        }

        if (table) {
            table->exists = TTY_TRUE;
            table->off    = tty_get_offset32(record + 8);
            table->size   = tty_get_uint32(record + 12);
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
    
    #undef TTY_TAG_EQUALS
}

static TTY_bool tty_extract_char_encoding(TTY* font) {
    TTY_uint16 numTables = tty_get_uint16(font->data + font->cmap.off + 2);
    
    for (TTY_uint16 i = 0; i < numTables; i++) {
        TTY_uint8* data = font->data + font->cmap.off + 4 + i * 8;

        TTY_uint16 platformID = tty_get_uint16(data);
        TTY_uint16 encodingID = tty_get_uint16(data + 2);
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
            font->encoding.off        = tty_get_offset32(data + 4);

            TTY_uint8* subtable = font->data + font->cmap.off + font->encoding.off;
            TTY_uint16 format   = tty_get_uint16(subtable);

            if (tty_format_is_supported(format)) {
                return TTY_TRUE;
            }
        }
    }

    return TTY_FALSE;
}

static TTY_bool tty_format_is_supported(TTY_uint16 format) {
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

static void tty_extract_vmetrics(TTY* font) {
    TTY_uint8* hhea = font->data + font->hhea.off;
    font->ascender  = tty_get_int16(hhea + 4);
    font->descender = tty_get_int16(hhea + 6);
    font->lineGap   = tty_get_int16(hhea + 8);
}

static TTY_bool tty_interpreter_init(TTY* font) {
    font->interp.stack.cap = tty_get_uint16(font->data + font->maxp.off + 24);
    font->interp.funcs.cap = tty_get_uint16(font->data + font->maxp.off + 20);
    
    size_t stackSize = sizeof(TTY_int32) * font->interp.stack.cap;
    size_t funcsSize = sizeof(TTY_Func)  * font->interp.funcs.cap;

    font->interp.mem = calloc(stackSize + funcsSize, 1);
    if (font->interp.mem == NULL) {
        return TTY_FALSE;
    }

    font->interp.stack.buffer = (TTY_int32*)(font->interp.mem);
    font->interp.funcs.buffer = (TTY_Func*) (font->interp.mem + stackSize);
    return TTY_TRUE;
}

static TTY_bool tty_unhinted_init(TTY* font, TTY_Unhinted* unhinted, TTY_uint32 numOutlinePoints) {
    unhinted->numOutlinePoints = numOutlinePoints;
    unhinted->numPoints        = numOutlinePoints + TTY_NUM_PHANTOM_POINTS;

    size_t pointsSize = unhinted->numPoints * sizeof(TTY_V2);
    size_t typesSize  = unhinted->numPoints * sizeof(TTY_Point_Type);

    unhinted->mem = calloc(pointsSize + typesSize, 1);
    if (unhinted->mem == NULL) {
        return TTY_FALSE;
    }

    unhinted->points     = (TTY_V2*)        (unhinted->mem);
    unhinted->pointTypes = (TTY_Point_Type*)(unhinted->mem + pointsSize);

    return TTY_TRUE;
}

static TTY_bool tty_zone0_init(TTY* font, TTY_Zone* zone) {
    zone->numOutlinePoints = tty_get_uint16(font->data + font->maxp.off + 16);
    zone->numPoints        = zone->numOutlinePoints + TTY_NUM_PHANTOM_POINTS;
    
    size_t pointsSize = zone->numPoints * sizeof(TTY_V2);
    size_t touchSize  = zone->numPoints * sizeof(TTY_Touch_Flag);
    size_t off        = 0;

    zone->memSize = 2 * pointsSize + touchSize;
    
    zone->mem = calloc(zone->memSize, 1);
    if (zone->mem == NULL) {
        return TTY_FALSE;
    }
    
    zone->org        = NULL;
    zone->orgScaled  = (TTY_V2*)        (zone->mem + (off));
    zone->cur        = (TTY_V2*)        (zone->mem + (off += pointsSize));
    zone->touchFlags = (TTY_Touch_Flag*)(zone->mem + (off += pointsSize));
    zone->pointTypes = NULL;
    
    return TTY_TRUE;
}

static TTY_bool tty_zone1_init(TTY* font, TTY_Zone* zone, TTY_uint32 numOutlinePoints) {
    zone->numOutlinePoints = numOutlinePoints;
    zone->numPoints        = zone->numOutlinePoints + TTY_NUM_PHANTOM_POINTS;
    
    size_t pointsSize = zone->numPoints * sizeof(TTY_V2);
    size_t touchSize  = zone->numPoints * sizeof(TTY_Touch_Flag);
    size_t typesSize  = zone->numPoints * sizeof(TTY_Point_Type);
    size_t off        = 0;

    zone->memSize = 3 * pointsSize + touchSize + typesSize;
    
    zone->mem = calloc(zone->memSize, 1);
    if (zone->mem == NULL) {
        return TTY_FALSE;
    }
    
    zone->org        = (TTY_V2*)        (zone->mem);
    zone->orgScaled  = (TTY_V2*)        (zone->mem + (off += pointsSize));
    zone->cur        = (TTY_V2*)        (zone->mem + (off += pointsSize));
    zone->touchFlags = (TTY_Touch_Flag*)(zone->mem + (off += pointsSize));
    zone->pointTypes = (TTY_Point_Type*)(zone->mem + (off += touchSize));
    
    return TTY_TRUE;
}

static void tty_interpreter_free(TTY_Interp* interp) {
    TTY_FREE_AND_NULLIFY(interp->mem);
}

static void tty_unhinted_free(TTY_Unhinted* unhinted) {
    TTY_FREE_AND_NULLIFY(unhinted->mem);
}

static void tty_zone_free(TTY_Zone* zone) {
    TTY_FREE_AND_NULLIFY(zone->mem);
}


/* ------------------- */
/* Glyph Index Mapping */
/* ------------------- */
static TTY_uint16 tty_get_glyph_index_format_4(TTY_uint8* subtable, TTY_uint32 cp) {
    #define TTY_GET_END_CODE(index) tty_get_uint16(subtable + 14 + 2 * (index))
    
    TTY_uint16 segCount = tty_get_uint16(subtable + 6) >> 1;
    TTY_uint16 left     = 0;
    TTY_uint16 right    = segCount - 1;

    while (left <= right) {
        TTY_uint16 mid     = (left + right) / 2;
        TTY_uint16 endCode = TTY_GET_END_CODE(mid);

        if (endCode >= cp) {
            if (mid == 0 || TTY_GET_END_CODE(mid - 1) < cp) {
                TTY_uint32 off            = 16 + 2 * mid;
                TTY_uint8* idRangeOffsets = subtable + 6 * segCount + off;
                TTY_uint16 idRangeOffset  = tty_get_uint16(idRangeOffsets);
                TTY_uint16 startCode      = tty_get_uint16(subtable + 2 * segCount + off);

                if (startCode > cp) {
                    return 0;
                }
                
                if (idRangeOffset == 0) {
                    TTY_uint16 idDelta = tty_get_int16(subtable + 4 * segCount + off);
                    return cp + idDelta;
                }

                return tty_get_uint16(idRangeOffset + 2 * (cp - startCode) + idRangeOffsets);
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


/* ---------------- */
/* glyf Table Stuff */
/* ---------------- */
static TTY_bool tty_get_glyf_data_block(TTY* font, TTY_uint8** block, TTY_uint32 glyphIdx) {
    #define TTY_GET_OFF_16(idx)\
        (tty_get_offset16(font->data + font->loca.off + (2 * (idx))) * 2)

    #define TTY_GET_OFF_32(idx)\
        tty_get_offset32(font->data + font->loca.off + (4 * (idx)))

    TTY_int16  version   = tty_get_int16(font->data + font->head.off + 50);
    TTY_uint16 numGlyphs = tty_get_num_glyphs(font);

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

static TTY_bool tty_extract_glyph_points(TTY*            font, 
                                         TTY_Instance*   instance, 
                                         TTY_Glyph_Data* data) {
    TTY_uint32 pointIdx         = 0;
    TTY_uint32 numOutlinePoints = 1 + tty_get_uint16(data->glyfBlock + 8 + 2 * data->numContours);

    TTY_V2*         points;
    TTY_Point_Type* pointTypes;


    // Allocate memory for the glyph points

    if (instance->useHinting) {
        if (!tty_zone1_init(font, &data->zone1, numOutlinePoints)) {
            return TTY_FALSE;
        }
        points     = data->zone1.org;
        pointTypes = data->zone1.pointTypes;
    }
    else {
        if (!tty_unhinted_init(font, &data->unhinted, numOutlinePoints)) {
            return TTY_FALSE;
        }
        points     = data->unhinted.points;
        pointTypes = data->unhinted.pointTypes;
    }


    {
        // Calculate pointers to the glyph's coordinate and flag data

        TTY_uint8* flagData, *xData, *yData;

        TTY_uint32 flagsSize = 0;
        TTY_uint32 xDataSize = 0;
        
        flagData  = data->glyfBlock + (10 + 2 * data->numContours);
        flagData += 2 + tty_get_uint16(flagData);
        
        for (TTY_uint32 i = 0; i < numOutlinePoints;) {
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


        // Extract the glyph's points

        TTY_V2 absPos = { 0 };

        while (pointIdx < numOutlinePoints) {
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
                TTY_int32 xOff = tty_get_next_coord_off(
                    &xData, TTY_GLYF_X_DUAL, TTY_GLYF_X_SHORT_VECTOR, flags);

                TTY_int32 yOff = tty_get_next_coord_off(
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

    {
        // Calculate the glyph's phantom points

        TTY_int16  xMin = tty_get_int16(data->glyfBlock + 2);
        TTY_int16  yMax = tty_get_int16(data->glyfBlock + 8);
        TTY_uint16 xAdv = tty_get_glyph_advance_width(font, data->glyph->idx);
        TTY_int16  lsb  = tty_get_glyph_left_side_bearing(font, data->glyph->idx);
        TTY_int32  yAdv = tty_get_glyph_advance_height(font);
        TTY_int32  tsb  = tty_get_glyph_top_side_bearing(font, yMax);

        points[pointIdx].x = xMin - lsb;
        points[pointIdx].y = 0;

        pointIdx++;
        points[pointIdx].x = points[pointIdx - 1].x + xAdv;
        points[pointIdx].y = 0;

        pointIdx++;
        points[pointIdx].y = yMax + tsb;
        points[pointIdx].x = 0;

        pointIdx++;
        points[pointIdx].y = points[pointIdx - 1].y - yAdv;
        points[pointIdx].x = 0;

        pointIdx++;
        TTY_ASSERT(pointIdx == numOutlinePoints + TTY_NUM_PHANTOM_POINTS);
    }


    if (!instance->useHinting) {
        tty_scale_glyph_points(points, points, data->unhinted.numPoints, instance->scale);
        tty_round_phantom_points(points + numOutlinePoints);
        return TTY_TRUE;
    }


    tty_scale_glyph_points(data->zone1.orgScaled, points, data->zone1.numPoints, instance->scale);
    memcpy(data->zone1.cur, data->zone1.orgScaled, data->zone1.numPoints * sizeof(TTY_F26Dot6_V2));
    tty_round_phantom_points(data->zone1.cur + numOutlinePoints);

    return TTY_TRUE;
}

static TTY_int32 tty_get_next_coord_off(TTY_uint8** data, 
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
        coord = tty_get_int16(*data);
        (*data) += 2;
    }

    return coord;
}

static void tty_scale_glyph_points(TTY_F26Dot6_V2* scaledPoints, 
                                   TTY_V2*         points, 
                                   TTY_uint32      numPoints, 
                                   TTY_F10Dot22    scale) {
    for (TTY_uint32 i = 0; i < numPoints; i++) {
        scaledPoints[i].x = TTY_F10DOT22_MUL(points[i].x << 6, scale);
        scaledPoints[i].y = TTY_F10DOT22_MUL(points[i].y << 6, scale);
    }
}

static void tty_round_phantom_points(TTY_F26Dot6_V2* phantomPoints) {
    phantomPoints[0].x = tty_f26dot6_round(phantomPoints[0].x);
    phantomPoints[1].x = tty_f26dot6_round(phantomPoints[1].x);
    phantomPoints[2].y = tty_f26dot6_round(phantomPoints[2].y);
    phantomPoints[3].y = tty_f26dot6_round(phantomPoints[3].y);
}


/* --------- */
/* Rendering */
/* --------- */
static TTY_bool tty_render_glyph_internal(TTY*          font, 
                                          TTY_Instance* instance, 
                                          TTY_Glyph*    glyph,
                                          TTY_Image*    image, 
                                          TTY_uint32    x, 
                                          TTY_uint32    y) {
    TTY_Glyph_Data glyphData;
    glyphData.glyph = glyph;
    
    if (!tty_get_glyf_data_block(font, &glyphData.glyfBlock, glyph->idx)) {
        // The glyph is an emtpy glyph
        // TODO: x-advance off by 1?
        // TODO: y-advance?
        glyph->advance.x = tty_get_glyph_advance_width(font, glyph->idx);
        glyph->advance.x = tty_rounded_div((TTY_int64)glyph->advance.x * instance->scale, 64);
        glyph->advance.x >>= 16;
        return TTY_TRUE;
    }

    glyphData.numContours = tty_get_int16(glyphData.glyfBlock);
    if (glyphData.numContours < 0) {
        // TODO: handle composite glyphs
        TTY_ASSERT(0);
    }

    return tty_render_simple_glyph(font, instance, &glyphData, image, x, y);
}

static TTY_F26Dot6 tty_linear_map(TTY_F26Dot6 x, TTY_F26Dot6 x0, TTY_F26Dot6 y0, TTY_F26Dot6 x1, TTY_F26Dot6 y1) {
    TTY_F16Dot16 m = TTY_F16DOT16_DIV(y1 - y0, x1 - x0);
    TTY_F26Dot6  b = y0 - TTY_F16DOT16_MUL(m, x0);
    return TTY_F16DOT16_MUL(m, x) + b;
}

static TTY_bool tty_render_simple_glyph(TTY*            font, 
                                        TTY_Instance*   instance, 
                                        TTY_Glyph_Data* glyphData,
                                        TTY_Image*      image,
                                        TTY_uint32      x,
                                        TTY_uint32      y) {
    TTY_F26Dot6_V2 max, min;

    TTY_Edge*  edges;
    TTY_uint32 numEdges;

    {
        TTY_Curve* curves;
        TTY_uint32 numCurves;

        if (instance->useHinting) {
            if (!tty_execute_glyph_program(font, instance, glyphData)) {
                return TTY_FALSE;
            }
            
            tty_get_max_and_min_points(
                glyphData->zone1.cur, glyphData->zone1.numOutlinePoints, &max, &min);

            tty_set_hinted_glyph_metrics(
                glyphData->glyph, glyphData->zone1.cur + glyphData->zone1.numOutlinePoints, &max,
                &min);

            curves = tty_convert_points_into_curves(
                glyphData, glyphData->zone1.cur, glyphData->zone1.pointTypes, 
                glyphData->zone1.numOutlinePoints, &numCurves);

            tty_zone_free(&glyphData->zone1);
        }
        else {
            if (!tty_extract_glyph_points(font, instance, glyphData)) {
                return TTY_FALSE;
            }

            tty_get_max_and_min_points(
                glyphData->unhinted.points, glyphData->unhinted.numOutlinePoints, &max, &min);

            tty_set_unhinted_glyph_metrics(
                glyphData->glyph, 
                glyphData->unhinted.points + glyphData->unhinted.numOutlinePoints, &max, &min);

            curves = tty_convert_points_into_curves(
                glyphData, glyphData->unhinted.points, glyphData->unhinted.pointTypes,
                glyphData->unhinted.numOutlinePoints, &numCurves);

            tty_unhinted_free(&glyphData->unhinted);
        }

        if (curves == NULL) {
            return TTY_FALSE;
        }


        edges = tty_subdivide_curves_into_edges(curves, numCurves, &numEdges);
        free(curves);
        if (edges == NULL) {
            return TTY_FALSE;
        }

        qsort(edges, numEdges, sizeof(TTY_Edge), tty_compare_edges);
    }


    if (image->pixels == NULL) {
        // The image's pixels have not been allocated so create an image that
        // is a tight bounding box of the glyph
        if (!tty_image_init(image, NULL, glyphData->glyph->size.x, glyphData->glyph->size.y)) {
            free(edges);
            return TTY_FALSE;
        }
    }


    // Maintain a list of active edges. An active edge is an edge that is 
    // intersected by the current scanline
    TTY_Active_Edge_List activeEdgeList;
    if (!tty_active_edge_list_init(&activeEdgeList)) {
        free(edges);
        return TTY_FALSE;
    }


    // Use a separate array when calculating the alpha values for each row of 
    // pixels. The image's pixels are not used directly because a loss of 
    // precision would result. This is due to the fact that the image's pixels 
    // are one byte each and cannot store fractional values.
    TTY_F26Dot6* pixelRow = calloc(glyphData->glyph->size.x, sizeof(TTY_F26Dot6));
    if (pixelRow == NULL) {
        tty_active_edge_list_free(&activeEdgeList);
        free(edges);
        return TTY_FALSE;
    }


    TTY_uint32 pixelsPerRow =
        x + glyphData->glyph->size.x > image->w ?
        glyphData->glyph->size.x - image->w + x :
        glyphData->glyph->size.x;

    TTY_F26Dot6 yRel     = tty_f26dot6_ceil(max.y);
    TTY_F26Dot6 yAbs     = yRel  - (y << 6);
    TTY_F26Dot6 yEndAbs  = min.y - (y << 6);
    TTY_int32   yMaxCeil = tty_f26dot6_ceil(max.y) >> 6;
    TTY_uint32  edgeIdx  = 0;

    if (y + glyphData->glyph->size.y > image->h) {
        // TODO: test
        yEndAbs += (glyphData->glyph->size.y - image->h + y) << 6;
    }

    while (yAbs >= yEndAbs) {
        {
            // If an edge is no longer active, remove it from the list, else
            // update its x-intersection with the current scanline

            TTY_Active_Edge* activeEdge     = activeEdgeList.headEdge;
            TTY_Active_Edge* prevActiveEdge = NULL;

            while (activeEdge != NULL) {
                TTY_Active_Edge* next = activeEdge->next;
                
                if (activeEdge->edge->yMin >= yRel) { // TODO: Use > instead?
                    tty_remove_active_edge(&activeEdgeList, prevActiveEdge, activeEdge);
                }
                else {
                    activeEdge->xIntersection = 
                        tty_get_edge_scanline_x_intersection(activeEdge->edge, yRel);
                    
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
                        // TODO: If x-intersections are equal, sort by smallest x-min
                        if (current->xIntersection > current->next->xIntersection) {
                            tty_swap_active_edge_with_next(
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
            if (edges[edgeIdx].yMax < yRel) {
                break;
            }
            
            if (edges[edgeIdx].yMin < yRel) {
                TTY_F26Dot6 xIntersection = 
                    tty_get_edge_scanline_x_intersection(edges + edgeIdx, yRel);
                
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
                    tty_insert_active_edge_first(&activeEdgeList) :
                    tty_insert_active_edge_after(&activeEdgeList, prevActiveEdge);
                
                if (newActiveEdge == NULL) {
                    tty_active_edge_list_free(&activeEdgeList);
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
            TTY_Active_Edge* activeEdge    = activeEdgeList.headEdge;
            TTY_int32        windingNumber = 0;
            TTY_F26Dot6      weightedAlpha = TTY_F26DOT6_MUL(0x3FC0, TTY_PIXELS_PER_SCANLINE);
            
            TTY_F26Dot6 xRel = 
                activeEdge->xIntersection == 0 ?
                0x40 :
                tty_f26dot6_ceil(activeEdge->xIntersection);

            #define COMPUTE() tty_f26dot6_floor(tty_linear_map(xRel, min.x, 0, tty_f26dot6_ceil(max.x), (glyphData->glyph->size.x - 1) << 6) + 1)>> 6

            TTY_uint32 xIdx = COMPUTE();

            while (TTY_TRUE) {
                // Set the opacity of the pixels along the scanline
                // printf("xRel = %d, xIdx = %d\n", xRel >> 6, xIdx);

                {
                    // Handle pixels that are only partially covered by the 
                    // glyph outline
                    TTY_ASSERT(xIdx < glyphData->glyph->size.x);
                    TTY_ASSERT(xRel <= tty_f26dot6_ceil(max.x));

                    TTY_F26Dot6 coverage =
                        windingNumber == 0 ?
                        xRel - activeEdge->xIntersection :
                        activeEdge->xIntersection - xRel + 0x40;

                partial_coverage:
                    pixelRow[xIdx] += TTY_F26DOT6_MUL(weightedAlpha, coverage);

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
                xIdx = COMPUTE();

                if (xRel < activeEdge->xIntersection) {
                    // Handle pixels that are either fully covered or fully 
                    // not covered by the glyph outline

                    if (windingNumber == 0) {
                        xRel = tty_f26dot6_ceil(activeEdge->xIntersection);
                        xIdx = COMPUTE();
                    }
                    else {
                        do {
                            // printf("xRel = %d, xIdx = %d\n", xRel >> 6, xIdx);
                            TTY_ASSERT(xIdx < glyphData->glyph->size.x);
                            TTY_ASSERT(xRel <= tty_f26dot6_ceil(max.x));
                            pixelRow[xIdx] += weightedAlpha;
                            xRel           += 0x40;
                            xIdx            = COMPUTE();
                        } while (xRel < activeEdge->xIntersection);
                    }
                }
            }
        }

        yAbs -= TTY_PIXELS_PER_SCANLINE;
        yRel -= TTY_PIXELS_PER_SCANLINE;

        if ((yRel & 0x3F) == 0) {
            // A new row of pixels has been reached, transfer the accumulated
            // alpha values of the previous row to the image

            TTY_uint32 startIdx = (yMaxCeil - (yAbs >> 6) - 1) * image->w + x;
            TTY_ASSERT(startIdx < image->w * image->h);
            
            for (TTY_uint32 i = 0; i < pixelsPerRow; i++) {
                TTY_ASSERT(pixelRow[i] >= 0);
                // TTY_ASSERT(pixelRow[i] >> 6 <= 255);
                image->pixels[startIdx + i] = pixelRow[i] >> 6;
            }
            
            memset(pixelRow, 0, glyphData->glyph->size.x * sizeof(TTY_F26Dot6));
        }
    }

    
    tty_active_edge_list_free(&activeEdgeList);
    free(edges);
    free(pixelRow);
    return TTY_TRUE;
}

static void tty_get_max_and_min_points(TTY_F26Dot6_V2* points, 
                                       TTY_uint32      numPoints, 
                                       TTY_F26Dot6_V2* max, 
                                       TTY_F26Dot6_V2* min) {
    *max = *points;
    *min = *points;

    for (TTY_uint32 i = 1; i < numPoints; i++) {
        if (points[i].x < min->x) {
            min->x = points[i].x;
        }
        else if (points[i].x > max->x) {
            max->x = points[i].x;
        }

        if (points[i].y < min->y) {
            min->y = points[i].y;
        }
        else if (points[i].y > max->y) {
            max->y = points[i].y;
        }
    }
}

static void tty_set_hinted_glyph_metrics(TTY_Glyph*      glyph,
                                         TTY_F26Dot6_V2* phantomPoints,
                                         TTY_F26Dot6_V2* max, 
                                         TTY_F26Dot6_V2* min) {
    glyph->size.x = labs(max->x - min->x);
    glyph->size.y = labs(max->y - min->y);

    {
        TTY_F26Dot6 xHoriBearing = tty_f26dot6_floor(min->x);
        TTY_F26Dot6 right        = tty_f26dot6_ceil(min->x + glyph->size.x);

        glyph->size.x    = (right - xHoriBearing) >> 6;
        glyph->advance.x = tty_f26dot6_round(phantomPoints[1].x - phantomPoints[0].x) >> 6;
        
        if (min->x > 0) {
            glyph->offset.x = min->x >> 6;
        }
        else if (min->x < 0) {
            glyph->offset.x = -tty_f26dot6_ceil(labs(min->x)) >> 6; // TODO: just floor?
        }
    }

    {
        TTY_F26Dot6 yHoriBearing = tty_f26dot6_ceil(max->y);
        TTY_F26Dot6 bottom       = tty_f26dot6_floor(max->y - glyph->size.y);
        glyph->size.y   = (yHoriBearing - bottom) >> 6;
        glyph->offset.y = tty_f26dot6_round(max->y) >> 6;
    }
}

static void tty_set_unhinted_glyph_metrics(TTY_Glyph*      glyph,
                                           TTY_F26Dot6_V2* phantomPoints,
                                           TTY_F26Dot6_V2* max, 
                                           TTY_F26Dot6_V2* min) {
    // TODO
    // assert(0);
    tty_set_hinted_glyph_metrics(glyph, phantomPoints, max, min);
}

static TTY_Curve* tty_convert_points_into_curves(TTY_Glyph_Data* glyphData, 
                                                 TTY_F26Dot6_V2* points, 
                                                 TTY_Point_Type* pointTypes,
                                                 TTY_uint32      numPoints,
                                                 TTY_uint32*     numCurves) {
    TTY_Curve* curves = malloc(numPoints * sizeof(TTY_Curve));
    if (curves == NULL) {
        return NULL;
    }

    TTY_uint32 startPointIdx = 0;
    *numCurves = 0;

    for (TTY_uint32 i = 0; i < glyphData->numContours; i++) {
        TTY_uint16 endPointIdx   = tty_get_uint16(glyphData->glyfBlock + 10 + 2 * i);
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
                TTY_FIX_V2_SUB(&curve->p1, nextPoint, &curve->p2);
                curve->p2.x = TTY_F26DOT6_MUL(curve->p2.x, 0x20);
                curve->p2.y = TTY_F26DOT6_MUL(curve->p2.y, 0x20);
                TTY_FIX_V2_ADD(nextPoint, &curve->p2, &curve->p2);
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

static TTY_Edge* tty_subdivide_curves_into_edges(TTY_Curve*  curves, 
                                                 TTY_uint32  numCurves, 
                                                 TTY_uint32* numEdges) {
    // Count the number of edges that are needed

    *numEdges = 0;
    for (TTY_uint32 i = 0; i < numCurves; i++) {
        if (curves[i].p1.x == curves[i].p2.x && curves[i].p1.y == curves[i].p2.y) {
            // The curve is a straight line, no need to flatten it 

            if (curves[i].p0.y != curves[i].p2.y) {
                (*numEdges)++;
                continue;
            }
        }
        else {
            tty_subdivide_curve_into_edges(
                &curves[i].p0, &curves[i].p1, &curves[i].p2, 0, NULL, numEdges);
        }
    }

    TTY_Edge* edges = malloc(sizeof(TTY_Edge) * *numEdges);
    if (edges == NULL) {
        return NULL;
    }


    // Add the edges to the array

    *numEdges = 0;

    for (TTY_uint32 i = 0; i < numCurves; i++) {
        TTY_int8 dir = curves[i].p2.y > curves[i].p0.y ? 1 : -1;

        if (curves[i].p1.x == curves[i].p2.x && curves[i].p1.y == curves[i].p2.y) {
            // The curve is a straight line, no need to flatten it

            if (curves[i].p0.y != curves[i].p2.y) {
                tty_edge_init(edges + *numEdges, &curves[i].p0, &curves[i].p2, dir);
                (*numEdges)++;
            }
        }
        else {
            tty_subdivide_curve_into_edges(
                &curves[i].p0, &curves[i].p1, &curves[i].p2, dir, edges, numEdges);
        }
    }

    return edges;
}

static void tty_subdivide_curve_into_edges(TTY_F26Dot6_V2* p0, 
                                           TTY_F26Dot6_V2* p1, 
                                           TTY_F26Dot6_V2* p2, 
                                           TTY_int8        dir, 
                                           TTY_Edge*       edges, 
                                           TTY_uint32*     numEdges) {
    #define TTY_SUBDIVIDE(a, b)                     \
        { TTY_F26DOT6_MUL(((a)->x + (b)->x), 0x20), \
          TTY_F26DOT6_MUL(((a)->y + (b)->y), 0x20) }

    TTY_F26Dot6_V2 mid0 = TTY_SUBDIVIDE(p0, p1);
    TTY_F26Dot6_V2 mid1 = TTY_SUBDIVIDE(p1, p2);
    TTY_F26Dot6_V2 mid2 = TTY_SUBDIVIDE(&mid0, &mid1);

    {
        TTY_F26Dot6_V2 d = TTY_SUBDIVIDE(p0, p2);
        d.x -= mid2.x;
        d.y -= mid2.y;

        TTY_F26Dot6 sqrdError = TTY_F26DOT6_MUL(d.x, d.x) + TTY_F26DOT6_MUL(d.y, d.y);
        if (sqrdError <= TTY_SUBDIVIDE_SQRD_ERROR) {
            if (edges != NULL) {
                tty_edge_init(edges + *numEdges, p0, p2, dir);
            }
            (*numEdges)++;
            return;
        }
    }

    tty_subdivide_curve_into_edges(p0, &mid0, &mid2, dir, edges, numEdges);
    tty_subdivide_curve_into_edges(&mid2, &mid1, p2, dir, edges, numEdges);

    #undef TTY_SUBDIVIDE
}

static void tty_edge_init(TTY_Edge* edge, TTY_F26Dot6_V2* p0, TTY_F26Dot6_V2* p1, TTY_int8 dir) {
    edge->p0       = *p0;
    edge->p1       = *p1;
    edge->invSlope = tty_get_inv_slope(p0, p1);
    edge->dir      = dir;
    edge->xMin     = tty_min(p0->x, p1->x);
    tty_max_min(p0->y, p1->y, &edge->yMax, &edge->yMin);
}

static TTY_F16Dot16 tty_get_inv_slope(TTY_F26Dot6_V2* p0, TTY_F26Dot6_V2* p1) {
    if (p0->x == p1->x) {
        return 0;
    }
    
    TTY_F16Dot16 slope = TTY_F16DOT16_DIV(p1->y - p0->y, p1->x - p0->x);
    return TTY_F16DOT16_DIV(0x10000, slope);
}

static int tty_compare_edges(const void* edge0, const void* edge1) {
    if (((TTY_Edge*)edge0)->yMax > ((TTY_Edge*)edge1)->yMax) {
        return -1;
    }
    return 1;
}

static TTY_F26Dot6 tty_get_edge_scanline_x_intersection(TTY_Edge* edge, TTY_F26Dot6 scanline) {
    return TTY_F16DOT16_MUL(scanline - edge->p0.y, edge->invSlope) + edge->p0.x;
}

static TTY_bool tty_active_edge_list_init(TTY_Active_Edge_List* list) {
    list->headChunk = calloc(1, sizeof(TTY_Active_Chunk));
    if (list->headChunk != NULL) {
        list->headEdge      = NULL;
        list->reusableEdges = NULL;
        return TTY_TRUE;
    }
    return TTY_FALSE;
}

static void tty_active_edge_list_free(TTY_Active_Edge_List* list) {
    TTY_Active_Chunk* chunk = list->headChunk;
    while (chunk != NULL) {
        TTY_Active_Chunk* next = chunk->next;
        free(chunk);
        chunk = next;
    }
}

static TTY_Active_Edge* tty_get_available_active_edge(TTY_Active_Edge_List* list) {
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

static TTY_Active_Edge* tty_insert_active_edge_first(TTY_Active_Edge_List* list) {
    TTY_Active_Edge* edge = tty_get_available_active_edge(list);
    edge->next     = list->headEdge;
    list->headEdge = edge;
    return edge;
}

static TTY_Active_Edge* tty_insert_active_edge_after(TTY_Active_Edge_List* list, 
                                                     TTY_Active_Edge*      after) {
    TTY_Active_Edge* edge = tty_get_available_active_edge(list);
    edge->next  = after->next;
    after->next = edge;
    return edge;
}

static void tty_remove_active_edge(TTY_Active_Edge_List* list, 
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

static void tty_swap_active_edge_with_next(TTY_Active_Edge_List* list, 
                                           TTY_Active_Edge*      prev, 
                                           TTY_Active_Edge*      edge) {
    TTY_ASSERT(edge->next != NULL);
    
    if (prev != NULL) {
        prev->next = edge->next;
    }

    TTY_Active_Edge* temp = edge->next->next;
    edge->next->next = edge;
    edge->next       = temp;
}


/* --------------------- */
/* Instruction Execution */
/* --------------------- */
#define TTY_SCALAR_VERSION     35
#define TTY_NUM_PHANTOM_POINTS  4

static void tty_execute_font_program(TTY* font) {
    TTY_LOG_PROGRAM("Font Program");
    
    TTY_Temp_Interp_Data temp;
    temp.instance         = NULL;
    temp.glyphData        = NULL;
    temp.execute_next_ins = NULL;
    tty_ins_stream_init(&temp.stream, font->data + font->fpgm.off);

    font->interp.temp = &temp;

    tty_stack_clear(&font->interp.stack);

    while (temp.stream.off < font->fpgm.size) {
        tty_execute_next_font_program_ins(&font->interp);
    }
}

static void tty_execute_cv_program(TTY* font, TTY_Instance* instance) {
    TTY_LOG_PROGRAM("CV Program");

    // "Every time the control value program is run, the zone 0 contour data is
    //  initialized to 0s."
    memset(instance->zone0.mem, 0, instance->zone0.memSize);

    TTY_Temp_Interp_Data temp;
    temp.instance         = instance;
    temp.glyphData        = NULL;
    temp.execute_next_ins = tty_execute_next_cv_program_ins;
    tty_ins_stream_init(&temp.stream, font->data + font->prep.off);

    font->interp.temp = &temp;

    tty_stack_clear(&font->interp.stack);
    tty_reset_graphics_state(&font->interp);

    while (temp.stream.off < font->prep.size) {
        tty_execute_next_cv_program_ins(&font->interp);
    }
}

static TTY_bool tty_execute_glyph_program(TTY*            font, 
                                          TTY_Instance*   instance, 
                                          TTY_Glyph_Data* glyphData) {
    TTY_LOG_PROGRAM("Glyph Program");

    if (!tty_extract_glyph_points(font, instance, glyphData)) {
        return TTY_FALSE;
    }

    TTY_uint32 insOff = 10 + glyphData->numContours * 2;
    TTY_uint16 numIns = tty_get_int16(glyphData->glyfBlock + insOff);

    TTY_Temp_Interp_Data temp;
    temp.instance         = instance;
    temp.glyphData        = glyphData;
    temp.execute_next_ins = tty_execute_next_glyph_program_ins;
    tty_ins_stream_init(&temp.stream, glyphData->glyfBlock + insOff + 2);

    font->interp.temp = &temp;

    tty_stack_clear(&font->interp.stack);
    tty_reset_graphics_state(&font->interp);

    while (temp.stream.off < numIns) {
        tty_execute_next_glyph_program_ins(&font->interp);
    }

    #ifdef TTY_LOGGING
        for (TTY_uint32 i = 0; i < glyphData->zone1.numOutlinePoints; i++) {
            printf("%d) (%d, %d)\n", i, glyphData->zone1.cur[i].x, glyphData->zone1.cur[i].y);
        }
    #endif

    return TTY_TRUE;
}

static void tty_execute_next_font_program_ins(TTY_Interp* interp) {
    TTY_uint8 ins = tty_ins_stream_next(&interp->temp->stream);

    switch (ins) {
        case TTY_FDEF:
            tty_FDEF(interp);
            return;
        case TTY_IDEF:
            tty_IDEF(interp);
            return;
        case TTY_NPUSHB:
            tty_NPUSHB(interp);
            return;
        case TTY_NPUSHW:
            tty_NPUSHW(interp);
            return;
    }

    if (ins >= TTY_PUSHB && ins <= TTY_PUSHB_MAX) {
        tty_PUSHB(interp, ins);
        return;
    }
    else if (ins >= TTY_PUSHW && ins <= TTY_PUSHW_MAX) {
        tty_PUSHW(interp, ins);
        return;
    }

    TTY_LOG_UNKNOWN_INS(ins);
    TTY_ASSERT(0);
}

static void tty_execute_next_cv_program_ins(TTY_Interp* interp) {
    TTY_uint8 ins = tty_ins_stream_next(&interp->temp->stream);

    if (tty_try_execute_shared_ins(interp, ins)) {
        return;
    }

    switch (ins) {
        case TTY_FDEF:
            tty_FDEF(interp);
            return;
        case TTY_IDEF:
            tty_IDEF(interp);
            return;
    }

    TTY_LOG_UNKNOWN_INS(ins);
    TTY_ASSERT(0);
}

static void tty_execute_next_glyph_program_ins(TTY_Interp* interp) {
    TTY_uint8 ins = tty_ins_stream_next(&interp->temp->stream);

    if (tty_try_execute_shared_ins(interp, ins)) {
        return;
    }

    switch (ins) {
        case TTY_ALIGNRP:
            tty_ALIGNRP(interp);
            return;
        case TTY_DELTAP1:
            tty_DELTAP1(interp);
            return;
        case TTY_DELTAP2:
            tty_DELTAP2(interp);
            return;
        case TTY_DELTAP3:
            tty_DELTAP3(interp);
            return;
        case TTY_IP:
            tty_IP(interp);
            return;
        case TTY_SHPIX:
            tty_SHPIX(interp);
            return;
        case TTY_SMD:
            tty_SMD(interp);
            return;
        case TTY_SRP0:
            tty_SRP0(interp);
            return;
        case TTY_SRP1:
            tty_SRP1(interp);
            return;
        case TTY_SRP2:
            tty_SRP2(interp);
            return;
        case TTY_SZPS:
            tty_SZPS(interp);
            return;
        case TTY_SZP0:
            tty_SZP0(interp);
            return;
        case TTY_SZP1:
            tty_SZP1(interp);
            return;
        case TTY_SZP2:
            tty_SZP2(interp);
            return;
    }

    if (ins >= TTY_GC && ins <= TTY_GC_MAX) {
        tty_GC(interp, ins);
    }
    else if (ins >= TTY_IUP && ins <= TTY_IUP_MAX) {
        tty_IUP(interp, ins);
    }
    else if (ins >= TTY_MD && ins <= TTY_MD_MAX) {
        tty_MD(interp, ins);
    }
    else if (ins >= TTY_MDAP && ins <= TTY_MDAP_MAX) {
        tty_MDAP(interp, ins);
    }
    else if (ins >= TTY_MDRP && ins <= TTY_MDRP_MAX) {
        tty_MDRP(interp, ins);
    }
    else if (ins >= TTY_MIAP && ins <= TTY_MIAP_MAX) {
        tty_MIAP(interp, ins);
    }
    else if (ins >= TTY_MIRP && ins <= TTY_MIRP_MAX) {
        tty_MIRP(interp, ins);
    }
    else if (ins >= TTY_SHP && ins <= TTY_SHP_MAX) {
        tty_SHP(interp, ins);
        return;
    }
    else {
        TTY_LOG_UNKNOWN_INS(ins);
        TTY_ASSERT(0);
    }
}

static TTY_bool tty_try_execute_shared_ins(TTY_Interp* interp, TTY_uint8 ins) {
    switch (ins) {
        case TTY_ABS:
            tty_ABS(interp);
            return TTY_TRUE;
        case TTY_ADD:
            tty_ADD(interp);
            return TTY_TRUE;
        case TTY_AND:
            tty_AND(interp);
            return TTY_TRUE;
        case TTY_CALL:
            tty_CALL(interp);
            return TTY_TRUE;
        case TTY_CINDEX:
            tty_CINDEX(interp);
            return TTY_TRUE;
        case TTY_DELTAC1:
            tty_DELTAC1(interp);
            return TTY_TRUE;
        case TTY_DELTAC2:
            tty_DELTAC2(interp);
            return TTY_TRUE;
        case TTY_DELTAC3:
            tty_DELTAC3(interp);
            return TTY_TRUE;
        case TTY_DEPTH:
            tty_DEPTH(interp);
            return TTY_TRUE;
        case TTY_DIV:
            tty_DIV(interp);
            return TTY_TRUE;
        case TTY_DUP:
            tty_DUP(interp);
            return TTY_TRUE;
        case TTY_EQ:
            tty_EQ(interp);
            return TTY_TRUE;
        case TTY_FLOOR:
            tty_FLOOR(interp);
            return TTY_TRUE;
        case TTY_GETINFO:
            tty_GETINFO(interp);
            return TTY_TRUE;
        case TTY_GPV:
            tty_GPV(interp);
            return TTY_TRUE;
        case TTY_GT:
            tty_GT(interp);
            return TTY_TRUE;
        case TTY_GTEQ:
            tty_GTEQ(interp);
            return TTY_TRUE;
        case TTY_IF:
            tty_IF(interp);
            return TTY_TRUE;
        case TTY_JROT:
            tty_JROT(interp);
            return TTY_TRUE;
        case TTY_JMPR:
            tty_JMPR(interp);
            return TTY_TRUE;
        case TTY_LOOPCALL:
            tty_LOOPCALL(interp);
            return TTY_TRUE;
        case TTY_LT:
            tty_LT(interp);
            return TTY_TRUE;
        case TTY_LTEQ:
            tty_LTEQ(interp);
            return TTY_TRUE;
        case TTY_MINDEX:
            tty_MINDEX(interp);
            return TTY_TRUE;
        case TTY_MPPEM:
            tty_MPPEM(interp);
            return TTY_TRUE;
        case TTY_MUL:
            tty_MUL(interp);
            return TTY_TRUE;
        case TTY_NEG:
            tty_NEG(interp);
            return TTY_TRUE;
        case TTY_NEQ:
            tty_NEQ(interp);
            return TTY_TRUE;
        case TTY_NPUSHB:
            tty_NPUSHB(interp);
            return TTY_TRUE;
        case TTY_NPUSHW:
            tty_NPUSHW(interp);
            return TTY_TRUE;
        case TTY_OR:
            tty_OR(interp);
            return TTY_TRUE;
        case TTY_POP:
            tty_POP(interp);
            return TTY_TRUE;
        case TTY_RCVT:
            tty_RCVT(interp);
            return TTY_TRUE;
        case TTY_RDTG:
            tty_RDTG(interp);
            return TTY_TRUE;
        case TTY_ROLL:
            tty_ROLL(interp);
            return TTY_TRUE;
        case TTY_RS:
            tty_RS(interp);
            return TTY_TRUE;
        case TTY_RTG:
            tty_RTG(interp);
            return TTY_TRUE;
        case TTY_RTHG:
            tty_RTHG(interp);
            return TTY_TRUE;
        case TTY_RUTG:
            tty_RUTG(interp);
            return TTY_TRUE;
        case TTY_SCANCTRL:
            tty_SCANCTRL(interp);
            return TTY_TRUE;
        case TTY_SCANTYPE:
            tty_SCANTYPE(interp);
            return TTY_TRUE;
        case TTY_SCVTCI:
            tty_SCVTCI(interp);
            return TTY_TRUE;
        case TTY_SDB:
            tty_SDB(interp);
            return TTY_TRUE;
        case TTY_SDS:
            tty_SDS(interp);
            return TTY_TRUE;
        case TTY_SLOOP:
            tty_SLOOP(interp);
            return TTY_TRUE;
        case TTY_SUB:
            tty_SUB(interp);
            return TTY_TRUE;
        case TTY_SWAP:
            tty_SWAP(interp);
            return TTY_TRUE;
        case TTY_WCVTF:
            tty_WCVTF(interp);
            return TTY_TRUE;
        case TTY_WCVTP:
            tty_WCVTP(interp);
            return TTY_TRUE;
        case TTY_WS:
            tty_WS(interp);
            return TTY_TRUE;
    }

    if (ins >= TTY_PUSHB && ins <= TTY_PUSHB_MAX) {
        tty_PUSHB(interp, ins);
    }
    else if (ins >= TTY_PUSHW && ins <= TTY_PUSHW_MAX) {
        tty_PUSHW(interp, ins);
    }
    else if (ins >= TTY_ROUND && ins <= TTY_ROUND_MAX) {
        tty_ROUND(interp, ins);
    }
    else if (ins >= TTY_SVTCA && ins <= TTY_SVTCA_MAX) {
        tty_SVTCA(interp, ins);
    }
    else {
        return TTY_FALSE;
    }

    return TTY_TRUE;
}

static void tty_ins_stream_init(TTY_Ins_Stream* stream, TTY_uint8* buffer) {
    stream->buffer = buffer;
    stream->off    = 0;
}

static TTY_uint8 tty_ins_stream_next(TTY_Ins_Stream* stream) {
    return stream->buffer[stream->off++];
}

static TTY_uint8 tty_ins_stream_peek(TTY_Ins_Stream* stream) {
    return stream->buffer[stream->off];
}

static void tty_ins_stream_consume(TTY_Ins_Stream* stream) {
    stream->off++;
}

static void tty_ins_stream_jump(TTY_Ins_Stream* stream, TTY_int32 count) {
    stream->off += count;
}

static void tty_stack_clear(TTY_Stack* stack) {
    stack->count = 0;
}

static void tty_stack_push(TTY_Stack* stack, TTY_int32 val) {
    TTY_ASSERT(stack->count < stack->cap);
    stack->buffer[stack->count++] = val;
}

static TTY_int32 tty_stack_pop(TTY_Stack* stack) {
    TTY_ASSERT(stack->count > 0);
    return stack->buffer[--stack->count];
}

static void tty_reset_graphics_state(TTY_Interp* interp) {
    interp->gState.autoFlip          = TTY_TRUE;
    interp->gState.controlValueCutIn = 68;
    interp->gState.deltaBase         = 9;
    interp->gState.deltaShift        = 3;
    interp->gState.dualProjVec.x     = 0x4000;
    interp->gState.dualProjVec.y     = 0;
    interp->gState.freedomVec.x      = 0x4000;
    interp->gState.freedomVec.y      = 0;
    interp->gState.gep0              = 1;
    interp->gState.gep1              = 1;
    interp->gState.gep2              = 1;
    interp->gState.loop              = 1;
    interp->gState.minDist           = 0x40;
    interp->gState.projVec.x         = 0x4000;
    interp->gState.projVec.y         = 0;
    interp->gState.rp0               = 0;
    interp->gState.rp1               = 0;
    interp->gState.rp2               = 0;
    interp->gState.roundState        = TTY_ROUND_TO_GRID;
    interp->gState.scanControl       = TTY_FALSE;
    interp->gState.singleWidthCutIn  = 0;
    interp->gState.singleWidthValue  = 0;

    if (interp->temp->glyphData != NULL) {
        interp->gState.zp0 = &interp->temp->glyphData->zone1;
        interp->gState.zp1 = &interp->temp->glyphData->zone1;
        interp->gState.zp2 = &interp->temp->glyphData->zone1;
    }
    else {
        interp->gState.zp0 = NULL;
        interp->gState.zp1 = NULL;
        interp->gState.zp2 = NULL;
    }

    interp->gState.activeTouchFlags = TTY_TOUCH_X;
    interp->gState.projDotFree      = 0x40000000;
}

static void tty_ABS(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_F26Dot6 val = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, labs(val));
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_ADD(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_F26Dot6 n1 = tty_stack_pop(&interp->stack);
    TTY_F26Dot6 n2 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, n1 + n2);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_ALIGNRP(TTY_Interp* interp) {
    TTY_LOG_INS();

    TTY_ASSERT(interp->gState.rp0 < interp->gState.zp0->numPoints);
    TTY_F26Dot6_V2* rp0Cur = interp->gState.zp0->cur + interp->gState.rp0;

    for (TTY_uint32 i = 0; i < interp->gState.loop; i++) {
        TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);
        TTY_ASSERT(pointIdx < interp->gState.zp1->numPoints);

        TTY_F26Dot6 dist = tty_sub_proj(interp, rp0Cur, interp->gState.zp1->cur + pointIdx);
        tty_move_point(interp, interp->gState.zp1, pointIdx, dist);

        TTY_LOG_POINT(interp->gState.zp1->cur[pointIdx]);
    }

    interp->gState.loop = 1;
}

static void tty_AND(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 e2 = tty_stack_pop(&interp->stack);
    TTY_uint32 e1 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, e1 != 0 && e2 != 0 ? 1 : 0);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_CALL(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_call_func(interp, tty_stack_pop(&interp->stack), 1);
}

static void tty_CINDEX(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 pos = tty_stack_pop(&interp->stack);
    TTY_uint32 val = interp->stack.buffer[interp->stack.count - pos];
    tty_stack_push(&interp->stack, val);
    TTY_LOG_VALUE(val);
}

static void tty_DELTAC1(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_deltac(interp, 0);
}

static void tty_DELTAC2(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_deltac(interp, 16);
}

static void tty_DELTAC3(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_deltac(interp, 32);
}

static void tty_DELTAP1(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_deltap(interp, 0);
}

static void tty_DELTAP2(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_deltap(interp, 16);
}

static void tty_DELTAP3(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_deltap(interp, 32);
}

static void tty_DEPTH(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_stack_push(&interp->stack, interp->stack.count);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_DIV(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_F26Dot6 n1 = tty_stack_pop(&interp->stack);
    TTY_F26Dot6 n2 = tty_stack_pop(&interp->stack);
    TTY_ASSERT(n1 != 0);

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

    tty_stack_push(&interp->stack, result);
    TTY_LOG_VALUE(result);
}

static void tty_DUP(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 e = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, e);
    tty_stack_push(&interp->stack, e);
    TTY_LOG_VALUE(e);
}

static void tty_EQ(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_int32 e2 = tty_stack_pop(&interp->stack);
    TTY_int32 e1 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, e1 == e2 ? 1 : 0);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_FDEF(TTY_Interp* interp) {
    TTY_LOG_INS();

    TTY_uint32 funcId = tty_stack_pop(&interp->stack);
    TTY_ASSERT(funcId < interp->funcs.cap);

    interp->funcs.buffer[funcId] = interp->temp->stream.buffer + interp->temp->stream.off;

    while (tty_ins_stream_next(&interp->temp->stream) != TTY_ENDF);

    TTY_LOG_VALUE(funcId);
}

static void tty_FLOOR(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_F26Dot6 val = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, tty_f26dot6_floor(val));
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_GC(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();

    TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(pointIdx < interp->gState.zp2->numPoints);

    TTY_F26Dot6 val =
        ins & 0x1 ?
        tty_dual_proj(interp, interp->gState.zp2->orgScaled + pointIdx) :
        tty_proj(interp, interp->gState.zp2->cur + pointIdx);

    tty_stack_push(&interp->stack, val);
    TTY_LOG_VALUE(val);
}

static void tty_GETINFO(TTY_Interp* interp) {
    TTY_LOG_INS();

    // These are the only supported selector bits for scalar version 35
    enum {
        TTY_VERSION                  = 0x01,
        TTY_GLYPH_ROTATED            = 0x02,
        TTY_GLYPH_STRETCHED          = 0x04,
        TTY_FONT_SMOOTHING_GRAYSCALE = 0x20,
    };

    TTY_uint32 result   = 0;
    TTY_uint32 selector = tty_stack_pop(&interp->stack);

    if (selector & TTY_VERSION) {
        result = TTY_SCALAR_VERSION;
    }
    if (selector & TTY_GLYPH_ROTATED) {
        if (interp->temp->instance->isRotated) {
            result |= 0x100;
        }
    }
    if (selector & TTY_GLYPH_STRETCHED) {
        if (interp->temp->instance->isStretched) {
            result |= 0x200;
        }
    }
    if (selector & TTY_FONT_SMOOTHING_GRAYSCALE) {
        // result |= 0x1000;
    }

    tty_stack_push(&interp->stack, result);
    TTY_LOG_VALUE(result);
}

static void tty_GPV(TTY_Interp* interp) {
    // TODO
    TTY_LOG_INS();
    TTY_ASSERT(0);
}

static void tty_GT(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_int32 e2 = tty_stack_pop(&interp->stack);
    TTY_int32 e1 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, e1 > e2 ? 1 : 0);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_GTEQ(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_int32 e2 = tty_stack_pop(&interp->stack);
    TTY_int32 e1 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, e1 >= e2 ? 1 : 0);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_IDEF(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_ASSERT(0);
}

static void tty_IF(TTY_Interp* interp) {
    TTY_LOG_INS();

    if (tty_stack_pop(&interp->stack) == 0) {
        TTY_LOG_VALUE(0);
        if (tty_jump_to_else_or_eif(&interp->temp->stream) == TTY_EIF) {
            // Condition is false and there is no else instruction
            return;
        }
    }
    else {
        TTY_LOG_VALUE(1);
    }

    while (TTY_TRUE) {
        TTY_uint8 ins = tty_ins_stream_peek(&interp->temp->stream);

        if (ins == TTY_ELSE) {
            tty_ins_stream_consume(&interp->temp->stream);
            tty_jump_to_else_or_eif(&interp->temp->stream);
            return;
        }

        if (ins == TTY_EIF) {
            tty_ins_stream_consume(&interp->temp->stream);
            return;
        }

        interp->temp->execute_next_ins(interp);
    }
}

static void tty_IP(TTY_Interp* interp) {
    TTY_LOG_INS();

    TTY_ASSERT(interp->gState.rp1 < interp->gState.zp0->numPoints);
    TTY_ASSERT(interp->gState.rp2 < interp->gState.zp1->numPoints);

    TTY_F26Dot6_V2* rp1Cur = interp->gState.zp0->cur + interp->gState.rp1;
    TTY_F26Dot6_V2* rp2Cur = interp->gState.zp1->cur + interp->gState.rp2;

    TTY_bool isTwilightZone = 
        interp->gState.gep0 == 0 || interp->gState.gep1 == 0 || interp->gState.gep2 == 0;

    TTY_F26Dot6_V2* rp1Org, *rp2Org;

    if (isTwilightZone) {
        // Twilight zone doesn't have unscaled coordinates
        rp1Org = interp->gState.zp0->orgScaled + interp->gState.rp1;
        rp2Org = interp->gState.zp1->orgScaled + interp->gState.rp2;
    }
    else {
        // Use unscaled coordinates for more precision
        rp1Org = interp->gState.zp0->org + interp->gState.rp1;
        rp2Org = interp->gState.zp1->org + interp->gState.rp2;
    }

    TTY_F26Dot6 totalDistCur = tty_sub_proj(interp, rp2Cur, rp1Cur);
    TTY_F26Dot6 totalDistOrg = tty_sub_dual_proj(interp, rp2Org, rp1Org);

    for (TTY_uint32 i = 0; i < interp->gState.loop; i++) {
        TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);
        TTY_ASSERT(pointIdx < interp->gState.zp2->numPoints);

        TTY_F26Dot6_V2* pointCur = interp->gState.zp2->cur + pointIdx;
        TTY_F26Dot6_V2* pointOrg = 
            (isTwilightZone ? interp->gState.zp2->orgScaled : interp->gState.zp2->org) + pointIdx;

        TTY_F26Dot6 distCur = tty_sub_proj(interp, pointCur, rp1Cur);
        TTY_F26Dot6 distOrg = tty_sub_dual_proj(interp, pointOrg, rp1Org);
        
        TTY_F26Dot6 distNew = 
            TTY_F26DOT6_DIV(TTY_F26DOT6_MUL(distOrg, totalDistCur), totalDistOrg);

        tty_move_point(interp, interp->gState.zp2, pointIdx, distNew - distCur);

        TTY_LOG_POINT(*pointCur);
    }

    interp->gState.loop = 1;
}

static void tty_IUP(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();

    // Applying IUP to zone0 is an error
    // TODO: How are composite glyphs handled?
    TTY_ASSERT(interp->gState.gep2 == 1);
    TTY_ASSERT(interp->temp->glyphData->numContours >= 0);

    if (interp->temp->glyphData->numContours == 0) {
        return;
    }

    TTY_Touch_Flag  touchFlag  = ins & 0x1 ? TTY_TOUCH_X : TTY_TOUCH_Y;
    TTY_Touch_Flag* touchFlags = interp->temp->glyphData->zone1.touchFlags;
    TTY_uint32      pointIdx   = 0;

    for (TTY_uint16 i = 0; i < interp->temp->glyphData->numContours; i++) {
        TTY_uint16 startPointIdx = pointIdx;
        TTY_uint16 endPointIdx   = tty_get_uint16(interp->temp->glyphData->glyfBlock + 10 + 2 * i);
        TTY_uint16 touch0        = 0;
        TTY_bool   findingTouch1 = TTY_FALSE;

        while (pointIdx <= endPointIdx) {
            if (touchFlags[pointIdx] & touchFlag) {
                if (findingTouch1) {
                    tty_iup_interpolate_or_shift(
                        &interp->temp->glyphData->zone1, touchFlag, startPointIdx, endPointIdx, 
                        touch0, pointIdx);

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
                    tty_iup_interpolate_or_shift(
                        &interp->temp->glyphData->zone1, touchFlag, startPointIdx, endPointIdx, 
                        touch0, i);

                    break;
                }
            }
        }
    }
}

static void tty_JROT(TTY_Interp* interp) {
    TTY_LOG_INS();
    
    TTY_uint32 val = tty_stack_pop(&interp->stack);
    TTY_int32  off = tty_stack_pop(&interp->stack); 

    if (val != 0) {
        tty_ins_stream_jump(&interp->temp->stream, off - 1);
        TTY_LOG_VALUE(off - 1);
    }
    else {
        TTY_LOG_VALUE(0);
    }
}

static void tty_JMPR(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_int32 off = tty_stack_pop(&interp->stack);
    tty_ins_stream_jump(&interp->temp->stream, off - 1);
    TTY_LOG_VALUE(off - 1);
}

static void tty_LOOPCALL(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 funcId = tty_stack_pop(&interp->stack);
    TTY_uint32 times  = tty_stack_pop(&interp->stack);
    tty_call_func(interp, funcId, times);
}

static void tty_LT(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_int32 e2 = tty_stack_pop(&interp->stack);
    TTY_int32 e1 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, e1 < e2 ? 1 : 0);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_LTEQ(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_int32 e2 = tty_stack_pop(&interp->stack);
    TTY_int32 e1 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, e1 <= e2 ? 1 : 0);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_MD(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();
    
    TTY_uint32  pointIdx0 = tty_stack_pop(&interp->stack);
    TTY_uint32  pointIdx1 = tty_stack_pop(&interp->stack);
    TTY_F26Dot6 dist;

    TTY_ASSERT(pointIdx0 < interp->gState.zp1->numPoints);
    TTY_ASSERT(pointIdx1 < interp->gState.zp0->numPoints);

    // TODO: Spec says if ins & 0x1 = 1 then use original outline, but FreeType
    //       uses current outline.

    if (ins & 0x1) {
        dist = tty_sub_proj(
            interp, interp->gState.zp0->cur + pointIdx1, interp->gState.zp1->cur + pointIdx0);
    }
    else {
        TTY_bool isTwilightZone = interp->gState.gep0 == 0 || interp->gState.gep1 == 0;

        if (isTwilightZone) {
            dist = tty_sub_dual_proj(
                interp, interp->gState.zp0->orgScaled + pointIdx1, 
                interp->gState.zp1->orgScaled + pointIdx0);
        }
        else {
            dist = tty_sub_dual_proj(
                interp, interp->gState.zp0->org + pointIdx1, interp->gState.zp1->org + pointIdx0);

            dist = TTY_F10DOT22_MUL(dist << 6, interp->temp->instance->scale);
        }
    }

    tty_stack_push(&interp->stack, dist);
    TTY_LOG_VALUE(dist);
}

static void tty_MDAP(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();

    TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(pointIdx < interp->gState.zp0->numPoints);

    TTY_F26Dot6_V2* point = interp->gState.zp0->cur + pointIdx;

    if (ins & 0x1) {
        TTY_F26Dot6 curDist     = tty_proj(interp, point);
        TTY_F26Dot6 roundedDist = tty_round(interp, curDist);
        tty_move_point(interp, interp->gState.zp0, pointIdx, roundedDist - curDist);
    }
    else {
        // Don't move the point, just mark it as touched
        interp->gState.zp0->touchFlags[pointIdx] |= interp->gState.activeTouchFlags;
    }

    interp->gState.rp0 = pointIdx;
    interp->gState.rp1 = pointIdx;

    TTY_LOG_POINT(*point);
}

static void tty_MDRP(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();

    TTY_ASSERT(interp->gState.rp0 < interp->gState.zp0->numPoints);

    TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(pointIdx < interp->gState.zp1->numPoints);

    TTY_F26Dot6_V2* rp0Cur         = interp->gState.zp0->cur + interp->gState.rp0;
    TTY_F26Dot6_V2* pointCur       = interp->gState.zp1->cur + pointIdx;
    TTY_bool        isTwilightZone = interp->gState.gep0 == 0 || interp->gState.gep1 == 0;

    TTY_F26Dot6_V2* rp0Org, *pointOrg;

    if (isTwilightZone) {
        // Twilight zone doesn't have unscaled coordinates
        rp0Org   = interp->gState.zp0->orgScaled + interp->gState.rp0;
        pointOrg = interp->gState.zp1->orgScaled + pointIdx;
    }
    else {
        // Use unscaled coordinates for more precision
        rp0Org   = interp->gState.zp0->org + interp->gState.rp0;
        pointOrg = interp->gState.zp1->org + pointIdx;
    }

    TTY_F26Dot6 distCur = tty_sub_proj(interp, pointCur, rp0Cur);
    TTY_F26Dot6 distOrg = tty_sub_dual_proj(interp, pointOrg, rp0Org);

    if (!isTwilightZone) {
        distOrg = TTY_F10DOT22_MUL(distOrg << 6, interp->temp->instance->scale);
    }

    distOrg = tty_apply_single_width_cut_in(interp, distOrg);

    if (ins & 0x04) {
        distOrg = tty_round(interp, distOrg);
    }

    if (ins & 0x08) {
        distOrg = tty_apply_min_dist(interp, distOrg);
    }

    if (ins & 0x10) {
        interp->gState.rp0 = pointIdx;
    }

    tty_move_point(interp, interp->gState.zp1, pointIdx, distOrg - distCur);

    interp->gState.rp1 = interp->gState.rp0;
    interp->gState.rp2 = pointIdx;

    TTY_LOG_POINT(*pointCur);
}

static void tty_MIAP(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();

    TTY_uint32 cvtIdx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(cvtIdx < interp->temp->instance->cvt.cap);

    TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(pointIdx < interp->gState.zp0->numPoints);

    TTY_F26Dot6 newDist = interp->temp->instance->cvt.buffer[cvtIdx];

    if (interp->gState.gep0 == 0) {
        TTY_F26Dot6_V2* org = interp->gState.zp0->orgScaled + pointIdx;

        org->x = TTY_F2DOT14_MUL(newDist, interp->gState.freedomVec.x);
        org->y = TTY_F2DOT14_MUL(newDist, interp->gState.freedomVec.y);

        interp->gState.zp0->cur[pointIdx] = *org;
    }

    TTY_F26Dot6 curDist = tty_proj(interp, interp->gState.zp0->cur + pointIdx);
    
    if (ins & 0x1) {
        if (labs(newDist - curDist) > interp->gState.controlValueCutIn) {
            newDist = curDist;
        }
        newDist = tty_round(interp, newDist);
    }

    tty_move_point(interp, interp->gState.zp0, pointIdx, newDist - curDist);
    
    interp->gState.rp0 = pointIdx;
    interp->gState.rp1 = pointIdx;

    TTY_LOG_POINT(interp->gState.zp0->cur[pointIdx]);
}

static void tty_MINDEX(TTY_Interp* interp) {
    TTY_LOG_INS();

    TTY_uint32 idx  = interp->stack.count - interp->stack.buffer[interp->stack.count - 1] - 1;
    size_t     size = sizeof(TTY_int32) * (interp->stack.count - idx - 1);

    interp->stack.count--;
    interp->stack.buffer[interp->stack.count] = interp->stack.buffer[idx];
    memmove(interp->stack.buffer + idx, interp->stack.buffer + idx + 1, size);
}

static void tty_MIRP(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();

    TTY_uint32 cvtIdx   = tty_stack_pop(&interp->stack);
    TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);

    TTY_ASSERT(cvtIdx   < interp->temp->instance->cvt.cap);
    TTY_ASSERT(pointIdx < interp->gState.zp1->numPoints);

    TTY_F26Dot6 cvtVal = tty_apply_single_width_cut_in(
        interp, interp->temp->instance->cvt.buffer[cvtIdx]);

    TTY_F26Dot6_V2* rp0Org = interp->gState.zp0->orgScaled + interp->gState.rp0;
    TTY_F26Dot6_V2* rp0Cur = interp->gState.zp0->cur       + interp->gState.rp0;

    TTY_F26Dot6_V2* pointOrg = interp->gState.zp1->orgScaled + pointIdx;
    TTY_F26Dot6_V2* pointCur = interp->gState.zp1->cur       + pointIdx;

    if (interp->gState.gep1 == 0) {
        pointOrg->x = rp0Org->x + TTY_F2DOT14_MUL(cvtVal, interp->gState.freedomVec.x);
        pointOrg->y = rp0Org->y + TTY_F2DOT14_MUL(cvtVal, interp->gState.freedomVec.y);
        *pointCur   = *pointOrg;
    }

    TTY_int32 distCur = tty_sub_proj(interp, pointCur, rp0Cur);
    TTY_int32 distOrg = tty_sub_dual_proj(interp, pointOrg, rp0Org);

    if (interp->gState.autoFlip) {
        if ((distOrg ^ cvtVal) < 0) {
            // Match the sign of distOrg
            cvtVal = -cvtVal;
        }
    }

    TTY_int32 distNew;
    
    if (ins & 0x4) {
        if (interp->gState.gep0 == interp->gState.gep1) {
            if (labs(cvtVal - distOrg) > interp->gState.controlValueCutIn) {
                cvtVal = distOrg;
            }
        }
        distNew = tty_round(interp, cvtVal);
    }
    else {
        distNew = cvtVal;
    }

    if (ins & 0x08) {
        distNew = tty_apply_min_dist(interp, distNew);
    }

    tty_move_point(interp, interp->gState.zp1, pointIdx, distNew - distCur);

    interp->gState.rp1 = interp->gState.rp0;
    interp->gState.rp2 = pointIdx;

    if (ins & 0x10) {
        interp->gState.rp0 = pointIdx;
    }

    TTY_LOG_POINT(*pointCur);
}

static void tty_MPPEM(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_stack_push(&interp->stack, interp->temp->instance->ppem);
    TTY_LOG_VALUE(interp->temp->instance->ppem);
}

static void tty_MUL(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_F26Dot6 n1     = tty_stack_pop(&interp->stack);
    TTY_F26Dot6 n2     = tty_stack_pop(&interp->stack);
    TTY_F26Dot6 result = TTY_F26DOT6_MUL(n1, n2);
    tty_stack_push(&interp->stack, result);
    TTY_LOG_VALUE(result);
}

static void tty_NEG(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_F26Dot6 val = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, -val);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_NEQ(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_int32 e2 = tty_stack_pop(&interp->stack);
    TTY_int32 e1 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, e1 != e2 ? 1 : 0);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_NPUSHB(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_push_bytes(interp, tty_ins_stream_next(&interp->temp->stream));
}

static void tty_NPUSHW(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_push_words(interp, tty_ins_stream_next(&interp->temp->stream));
}

static void tty_OR(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_int32 e1 = tty_stack_pop(&interp->stack);
    TTY_int32 e2 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, (e1 != 0 || e2 != 0) ? 1 : 0);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_POP(TTY_Interp* interp) {
    TTY_LOG_INS();
    tty_stack_pop(&interp->stack);
}

static void tty_PUSHB(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();
    tty_push_bytes(interp, 1 + (ins & 0x7));
}

static void tty_PUSHW(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();
    tty_push_words(interp, 1 + (ins & 0x7));
}

static void tty_RCVT(TTY_Interp* interp) {
    TTY_LOG_INS();
    
    TTY_uint32 cvtIdx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(cvtIdx < interp->temp->instance->cvt.cap);

    tty_stack_push(&interp->stack, interp->temp->instance->cvt.buffer[cvtIdx]);
    TTY_LOG_VALUE(interp->temp->instance->cvt.buffer[cvtIdx]);
}

static void tty_RDTG(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.roundState = TTY_ROUND_DOWN_TO_GRID;
}

static void tty_ROLL(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 a = tty_stack_pop(&interp->stack);
    TTY_uint32 b = tty_stack_pop(&interp->stack);
    TTY_uint32 c = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, b);
    tty_stack_push(&interp->stack, a);
    tty_stack_push(&interp->stack, c);
}

static void tty_ROUND(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();
    TTY_F26Dot6 dist = tty_stack_pop(&interp->stack);
    dist = tty_round(interp, dist);
    tty_stack_push(&interp->stack, dist);
    TTY_LOG_VALUE(dist);
}

static void tty_RS(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 idx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(idx < interp->temp->instance->storage.cap);
    tty_stack_push(&interp->stack, interp->temp->instance->storage.buffer[idx]);
    TTY_LOG_VALUE(interp->temp->instance->storage.buffer[idx]);
}

static void tty_RTG(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.roundState = TTY_ROUND_TO_GRID;
}

static void tty_RTHG(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.roundState = TTY_ROUND_TO_HALF_GRID;
}

static void tty_RUTG(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.roundState = TTY_ROUND_UP_TO_GRID;
}

static void tty_SCANCTRL(TTY_Interp* interp) {
    TTY_LOG_INS();

    TTY_uint16 flags  = tty_stack_pop(&interp->stack);
    TTY_uint8  thresh = flags & 0xFF;
    
    if (thresh == 0xFF) {
        interp->gState.scanControl = TTY_TRUE;
    }
    else if (thresh == 0x0) {
        interp->gState.scanControl = TTY_FALSE;
    }
    else {
        if (flags & 0x100) {
            if (interp->temp->instance->ppem <= thresh) {
                interp->gState.scanControl = TTY_TRUE;
            }
        }

        if (flags & 0x200) {
            if (interp->temp->instance->isRotated) {
                interp->gState.scanControl = TTY_TRUE;
            }
        }

        if (flags & 0x400) {
            if (interp->temp->instance->isStretched) {
                interp->gState.scanControl = TTY_TRUE;
            }
        }

        if (flags & 0x800) {
            if (thresh > interp->temp->instance->ppem) {
                interp->gState.scanControl = TTY_FALSE;
            }
        }

        if (flags & 0x1000) {
            if (!interp->temp->instance->isRotated) {
                interp->gState.scanControl = TTY_FALSE;
            }
        }

        if (flags & 0x2000) {
            if (!interp->temp->instance->isStretched) {
                interp->gState.scanControl = TTY_FALSE;
            }
        }
    }

    TTY_LOG_VALUE(interp->gState.scanControl);
}

static void tty_SCANTYPE(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.scanType = tty_stack_pop(&interp->stack);
    TTY_LOG_VALUE(interp->gState.scanType);
}

static void tty_SCVTCI(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.controlValueCutIn = tty_stack_pop(&interp->stack);
    TTY_LOG_VALUE(interp->gState.controlValueCutIn);
}

static void tty_SDB(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.deltaBase = tty_stack_pop(&interp->stack);
    TTY_LOG_VALUE(interp->gState.deltaBase);
}

static void tty_SDS(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.deltaShift = tty_stack_pop(&interp->stack);
    TTY_LOG_VALUE(interp->gState.deltaShift);
}

static void tty_SHP(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();

    TTY_F26Dot6_V2 dist;
    {
        TTY_F26Dot6_V2* refPointCur, *refPointOrg;

        if (ins & 0x1) {
            TTY_ASSERT(interp->gState.rp1 < interp->gState.zp0->numPoints);
            refPointCur = interp->gState.zp0->cur       + interp->gState.rp1;
            refPointOrg = interp->gState.zp0->orgScaled + interp->gState.rp1;
        }
        else {
            TTY_ASSERT(interp->gState.rp2 < interp->gState.zp1->numPoints);
            refPointCur = interp->gState.zp1->cur       + interp->gState.rp2;
            refPointOrg = interp->gState.zp1->orgScaled + interp->gState.rp2;
        }

        TTY_F26Dot6 d = tty_sub_proj(interp, refPointCur, refPointOrg);

        dist.x = tty_rounded_div(
            (TTY_int64)d * (interp->gState.freedomVec.x << 16), interp->gState.projDotFree);

        dist.y = tty_rounded_div(
            (TTY_int64)d * (interp->gState.freedomVec.y << 16), interp->gState.projDotFree);
    }

    for (TTY_uint32 i = 0; i < interp->gState.loop; i++) {
        TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);
        TTY_ASSERT(pointIdx < interp->gState.zp2->numPoints);

        interp->gState.zp2->cur[pointIdx].x      += dist.x;
        interp->gState.zp2->cur[pointIdx].y      += dist.y;
        interp->gState.zp2->touchFlags[pointIdx] |= interp->gState.activeTouchFlags;

        TTY_LOG_POINT(interp->gState.zp2->cur[pointIdx]);
    }

    interp->gState.loop = 1;
}

static void tty_SHPIX(TTY_Interp* interp) {
    TTY_LOG_INS();
    
    TTY_F26Dot6_V2 dist;
    {
        TTY_F26Dot6 amt = tty_stack_pop(&interp->stack);
        dist.x = TTY_F2DOT14_MUL(amt, interp->gState.freedomVec.x);
        dist.y = TTY_F2DOT14_MUL(amt, interp->gState.freedomVec.y);
    }

    for (TTY_uint32 i = 0; i < interp->gState.loop; i++) {
        TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);
        TTY_ASSERT(pointIdx < interp->gState.zp2->numPoints);

        interp->gState.zp2->cur[pointIdx].x      += dist.x;
        interp->gState.zp2->cur[pointIdx].y      += dist.y;
        interp->gState.zp2->touchFlags[pointIdx] |= interp->gState.activeTouchFlags;

        TTY_LOG_POINT(interp->gState.zp2->cur[pointIdx]);
    }

    interp->gState.loop = 1;
}

static void tty_SLOOP(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.loop = tty_stack_pop(&interp->stack);
    TTY_LOG_VALUE(interp->gState.loop);
}

static void tty_SMD(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.minDist = tty_stack_pop(&interp->stack);
    TTY_LOG_VALUE(interp->gState.minDist);
}

static void tty_SRP0(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.rp0 = tty_stack_pop(&interp->stack);
    TTY_LOG_VALUE(interp->gState.rp0);
}

static void tty_SRP1(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.rp1 = tty_stack_pop(&interp->stack);
    TTY_LOG_VALUE(interp->gState.rp1);
}

static void tty_SRP2(TTY_Interp* interp) {
    TTY_LOG_INS();
    interp->gState.rp2 = tty_stack_pop(&interp->stack);
    TTY_LOG_VALUE(interp->gState.rp2);
}

static void tty_SUB(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_F26Dot6 n1 = tty_stack_pop(&interp->stack);
    TTY_F26Dot6 n2 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, n2 - n1);
    TTY_LOG_PUSHED_VALUE(interp->stack);
}

static void tty_SVTCA(TTY_Interp* interp, TTY_uint8 ins) {
    TTY_LOG_INS();

    if (ins & 0x1) {
        interp->gState.freedomVec.x     = 0x4000;
        interp->gState.freedomVec.y     = 0;
        interp->gState.activeTouchFlags = TTY_TOUCH_X;
    }
    else {
        interp->gState.freedomVec.x     = 0;
        interp->gState.freedomVec.y     = 0x4000;
        interp->gState.activeTouchFlags = TTY_TOUCH_Y;
    }

    interp->gState.projVec     = interp->gState.freedomVec;
    interp->gState.dualProjVec = interp->gState.freedomVec;
    interp->gState.projDotFree = 0x40000000;
}

static void tty_SWAP(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 e2 = tty_stack_pop(&interp->stack);
    TTY_uint32 e1 = tty_stack_pop(&interp->stack);
    tty_stack_push(&interp->stack, e2);
    tty_stack_push(&interp->stack, e1);
}

static void tty_SZPS(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 zone = tty_stack_pop(&interp->stack);
    interp->gState.zp0  = tty_get_zone_pointer(interp, zone);
    interp->gState.zp1  = interp->gState.zp0;
    interp->gState.zp2  = interp->gState.zp0;
    interp->gState.gep0 = zone;
    interp->gState.gep1 = zone;
    interp->gState.gep2 = zone;
    TTY_LOG_VALUE(zone);
}

static void tty_SZP0(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 zone = tty_stack_pop(&interp->stack);
    interp->gState.zp0  = tty_get_zone_pointer(interp, zone);
    interp->gState.gep0 = zone;
    TTY_LOG_VALUE(zone);
}

static void tty_SZP1(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 zone = tty_stack_pop(&interp->stack);
    interp->gState.zp1  = tty_get_zone_pointer(interp, zone);
    interp->gState.gep1 = zone;
    TTY_LOG_VALUE(zone);
}

static void tty_SZP2(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_uint32 zone = tty_stack_pop(&interp->stack);
    interp->gState.zp2  = tty_get_zone_pointer(interp, zone);
    interp->gState.gep2 = zone;
    TTY_LOG_VALUE(zone);
}

static void tty_WCVTF(TTY_Interp* interp) {
    TTY_LOG_INS();

    TTY_uint32 funits = tty_stack_pop(&interp->stack);
    TTY_uint32 cvtIdx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(cvtIdx < interp->temp->instance->cvt.cap);

    interp->temp->instance->cvt.buffer[cvtIdx] = 
        TTY_F10DOT22_MUL(funits << 6, interp->temp->instance->scale);

    TTY_LOG_VALUE(interp->temp->instance->cvt.buffer[cvtIdx]);
}

static void tty_WCVTP(TTY_Interp* interp) {
    TTY_LOG_INS();

    TTY_uint32 pixels = tty_stack_pop(&interp->stack);

    TTY_uint32 cvtIdx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(cvtIdx < interp->temp->instance->cvt.cap);

    interp->temp->instance->cvt.buffer[cvtIdx] = pixels;
    TTY_LOG_VALUE(interp->temp->instance->cvt.buffer[cvtIdx]);
}

static void tty_WS(TTY_Interp* interp) {
    TTY_LOG_INS();
    TTY_int32 value = tty_stack_pop(&interp->stack);
    
    TTY_uint32 idx = tty_stack_pop(&interp->stack);
    TTY_ASSERT(idx < interp->temp->instance->storage.cap);

    interp->temp->instance->storage.buffer[idx] = value;
    TTY_LOG_VALUE(interp->temp->instance->storage.buffer[idx]);
}

static void tty_call_func(TTY_Interp* interp, TTY_uint32 funcId, TTY_uint32 times) {
    TTY_ASSERT(funcId < interp->funcs.cap);
    TTY_ASSERT(interp->funcs.buffer[funcId] != NULL);

    TTY_LOG_VALUE(funcId);

    TTY_Ins_Stream streamCopy = interp->temp->stream;

    while (times > 0) {
        tty_ins_stream_init(&interp->temp->stream, interp->funcs.buffer[funcId]);

        while (tty_ins_stream_peek(&interp->temp->stream) != TTY_ENDF) {
            interp->temp->execute_next_ins(interp);
        }

        tty_ins_stream_consume(&interp->temp->stream);
        times--;
    }

    interp->temp->stream = streamCopy;
}

static void tty_push_bytes(TTY_Interp* interp, TTY_uint32 count) {
    do {
        TTY_uint8 byte = tty_ins_stream_next(&interp->temp->stream);
        tty_stack_push(&interp->stack, byte);
    } while (--count != 0);
}

static void tty_push_words(TTY_Interp* interp, TTY_uint32 count) {
    do {
        TTY_int8  ms  = tty_ins_stream_next(&interp->temp->stream);
        TTY_uint8 ls  = tty_ins_stream_next(&interp->temp->stream);
        TTY_int32 val = (ms << 8) | ls;
        tty_stack_push(&interp->stack, val);
    } while (--count != 0);
}

static TTY_uint8 tty_jump_to_else_or_eif(TTY_Ins_Stream* stream) {
    TTY_uint32 numNested = 0;

    while (TTY_TRUE) {
        TTY_uint8 ins = tty_ins_stream_next(stream);

        if (ins >= TTY_PUSHB && ins <= TTY_PUSHB_MAX) {
            tty_ins_stream_jump(stream, 1 + (ins & 0x7));
        }
        else if (ins >= TTY_PUSHW && ins <= TTY_PUSHW_MAX) {
            tty_ins_stream_jump(stream, 2 * (1 + (ins & 0x7)));
        }
        else if (ins == TTY_NPUSHB) {
            tty_ins_stream_jump(stream, tty_ins_stream_next(stream));
        }
        else if (ins == TTY_NPUSHW) {
            tty_ins_stream_jump(stream, 2 * tty_ins_stream_next(stream));
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

    TTY_ASSERT(0);
    return 0;
}

static TTY_int32 tty_proj(TTY_Interp* interp, TTY_Fix_V2* v) {
    return 
        TTY_F2DOT14_MUL(v->x, interp->gState.projVec.x) + 
        TTY_F2DOT14_MUL(v->y, interp->gState.projVec.y);
}

static TTY_int32 tty_dual_proj(TTY_Interp* interp, TTY_Fix_V2* v) {
    return 
        TTY_F2DOT14_MUL(v->x, interp->gState.dualProjVec.x) + 
        TTY_F2DOT14_MUL(v->y, interp->gState.dualProjVec.y);
}

static TTY_int32 tty_sub_proj(TTY_Interp* interp, TTY_Fix_V2* a, TTY_Fix_V2* b) {
    TTY_Fix_V2 diff;
    TTY_FIX_V2_SUB(a, b, &diff);
    return tty_proj(interp, &diff);
}

static TTY_int32 tty_sub_dual_proj(TTY_Interp* interp, TTY_Fix_V2* a, TTY_Fix_V2* b) {
    TTY_Fix_V2 diff;
    TTY_FIX_V2_SUB(a, b, &diff);
    return tty_dual_proj(interp, &diff);
}

static void tty_move_point(TTY_Interp* interp, TTY_Zone* zone, TTY_uint32 idx, TTY_F26Dot6 dist) {
    zone->cur[idx].x      += TTY_F2DOT14_MUL(dist, interp->gState.freedomVec.x);
    zone->cur[idx].y      += TTY_F2DOT14_MUL(dist, interp->gState.freedomVec.y);
    zone->touchFlags[idx] |= interp->gState.activeTouchFlags;
}

static TTY_F26Dot6 tty_round(TTY_Interp* interp, TTY_F26Dot6 val) {
    // TODO: No idea how to apply "engine compensation" described in the spec

    switch (interp->gState.roundState) {
        case TTY_ROUND_TO_HALF_GRID:
            return (val & 0xFFFFFFC0) | 0x20;
        case TTY_ROUND_TO_GRID:
            return tty_f26dot6_round(val);
        case TTY_ROUND_TO_DOUBLE_GRID:
            // TODO
            TTY_ASSERT(0);
            break;
        case TTY_ROUND_DOWN_TO_GRID:
            return tty_f26dot6_floor(val);
        case TTY_ROUND_UP_TO_GRID:
            return tty_f26dot6_ceil(val);
        case TTY_ROUND_OFF:
            // TODO
            TTY_ASSERT(0);
            break;
    }

    TTY_ASSERT(0);
    return 0;
}

static TTY_F26Dot6 tty_apply_single_width_cut_in(TTY_Interp* interp, TTY_F26Dot6 value) {
    TTY_F26Dot6 absDiff = labs(value - interp->gState.singleWidthValue);
    if (absDiff < interp->gState.singleWidthCutIn) {
        if (value < 0) {
            return -interp->gState.singleWidthValue;
        }
        return interp->gState.singleWidthValue;
    }
    return value;
}

static TTY_F26Dot6 tty_apply_min_dist(TTY_Interp* interp, TTY_F26Dot6 value) {
    if (labs(value) < interp->gState.minDist) {
        if (value < 0) {
            return -interp->gState.minDist;
        }
        return interp->gState.minDist;
    }
    return value;
}

static void tty_deltac(TTY_Interp* interp, TTY_uint8 range) {
    TTY_uint32 count = tty_stack_pop(&interp->stack);

    while (count > 0) {
        TTY_uint32 cvtIdx = tty_stack_pop(&interp->stack);
        TTY_ASSERT(cvtIdx < interp->temp->instance->cvt.cap);

        TTY_uint32 exc = tty_stack_pop(&interp->stack);

        TTY_F26Dot6 deltaVal;
        if (tty_get_delta_value(interp, exc, range, &deltaVal)) {
            interp->temp->instance->cvt.buffer[cvtIdx] += deltaVal;
            TTY_LOG_VALUE(deltaVal);
        }

        count--;
    }
}

static void tty_deltap(TTY_Interp* interp, TTY_uint8 range) {
    TTY_uint32 count = tty_stack_pop(&interp->stack);

    while (count > 0) {
        TTY_uint32 pointIdx = tty_stack_pop(&interp->stack);
        TTY_ASSERT(pointIdx < interp->gState.zp0->numPoints);

        TTY_uint32 exc = tty_stack_pop(&interp->stack);

        TTY_F26Dot6 deltaVal;
        if (tty_get_delta_value(interp, exc, range, &deltaVal)) {
            tty_move_point(interp, interp->gState.zp0, pointIdx, deltaVal);
            TTY_LOG_VALUE(deltaVal);
        }

        count--;
    }
}

static TTY_bool tty_get_delta_value(TTY_Interp*   interp,
                                    TTY_uint32    exc, 
                                    TTY_uint8     range, 
                                    TTY_F26Dot6*  deltaVal) {
    TTY_uint32 ppem = ((exc & 0xF0) >> 4) + interp->gState.deltaBase + range;

    if (interp->temp->instance->ppem != ppem) {
        return TTY_FALSE;
    }

    TTY_int8 numSteps = (exc & 0xF) - 8;
    if (numSteps > 0) {
        numSteps++;
    }

    *deltaVal = numSteps * (1l << (6 - interp->gState.deltaShift));
    return TTY_TRUE;
}

static void tty_iup_interpolate_or_shift(TTY_Zone*      zone1, 
                                         TTY_Touch_Flag touchFlag, 
                                         TTY_uint16     startPointIdx, 
                                         TTY_uint16     endPointIdx, 
                                         TTY_uint16     touch0, 
                                         TTY_uint16     touch1) {
    #define TTY_IUP_INTERPOLATE(coord)                                                      \
        TTY_F26Dot6 totalDistCur = zone1->cur[touch1].coord - zone1->cur[touch0].coord;     \
        TTY_int32   totalDistOrg = zone1->org[touch1].coord - zone1->org[touch0].coord;     \
        TTY_int32   orgDist      = zone1->org[i].coord      - zone1->org[touch0].coord;     \
                                                                                            \
        TTY_F10Dot22 scale   = tty_rounded_div((TTY_int64)totalDistCur << 16, totalDistOrg);\
        TTY_F26Dot6  newDist = TTY_F10DOT22_MUL(orgDist << 6, scale);                       \
        zone1->cur[i].coord  = zone1->cur[touch0].coord + newDist;                          \
                                                                                            \
        TTY_LOG_CUSTOM_F( "Interp %3d: %5d", i, zone1->cur[i].coord);

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
        TTY_LOG_CUSTOM_F( "Shift %3d: %5d", i, zone1->cur[i].coord);

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
        tty_max_min(zone1->org[touch0].x, zone1->org[touch1].x, &coord1, &coord0);
    }
    else {
        tty_max_min(zone1->org[touch0].y, zone1->org[touch1].y, &coord1, &coord0);
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

static TTY_Zone* tty_get_zone_pointer(TTY_Interp* interp, TTY_uint32 zone) {
    switch (zone) {
        case 0:
            return &interp->temp->instance->zone0;
        case 1:
            return &interp->temp->glyphData->zone1;
    }
    TTY_ASSERT(0);
    return NULL;
}


/* ---------------- */
/* Fixed-point Math */
/* ---------------- */
static TTY_int64 tty_rounded_div(TTY_int64 a, TTY_int64 b) {
    return b == 0 ? 0 : (a < 0) ^ (b < 0) ? (a - b / 2) / b : (a + b / 2) / b;
}

static TTY_F26Dot6 tty_f26dot6_round(TTY_F26Dot6 val) {
    return ((val & 0x20) << 1) + (val & 0xFFFFFFC0);
}

static TTY_F26Dot6 tty_f26dot6_ceil(TTY_F26Dot6 val) {
    return (val & 0x3F) ? (val & 0xFFFFFFC0) + 0x40 : val;
}

static TTY_F26Dot6 tty_f26dot6_floor(TTY_F26Dot6 val) {
    return val & 0xFFFFFFC0;
}


/* ---- */
/* Util */
/* ---- */
static TTY_uint16 tty_get_uint16(TTY_uint8* data) {
    return data[0] << 8 | data[1];
}

static TTY_uint32 tty_get_uint32(TTY_uint8* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static TTY_int16 tty_get_int16(TTY_uint8* data) {
    return data[0] << 8 | data[1];
}

static TTY_int32 tty_min(TTY_int32 a, TTY_int32 b) {
    return a < b ? a : b;
}

static void tty_max_min(TTY_int32 a, TTY_int32 b, TTY_int32* max, TTY_int32* min) {
    if (a > b) {
        *max = a;
        *min = b;
    }
    else {
        *max = b;
        *min = a;
    }
}

static TTY_uint16 tty_get_glyph_advance_width(TTY* font, TTY_uint32 glyphIdx) {
    TTY_uint8* hmtxData    = font->data + font->hmtx.off;
    TTY_uint16 numHMetrics = tty_get_uint16(font->data + font->hhea.off + 34);
    if (glyphIdx < numHMetrics) {
        return tty_get_uint16(hmtxData + 4 * glyphIdx);
    }
    return 0;
}

static TTY_int16 tty_get_glyph_left_side_bearing(TTY* font, TTY_uint32 glyphIdx) {
    TTY_uint8* hmtxData    = font->data + font->hmtx.off;
    TTY_uint16 numHMetrics = tty_get_uint16(font->data + font->hhea.off + 34);
    if (glyphIdx < numHMetrics) {
        return tty_get_int16(hmtxData + 4 * glyphIdx + 2);
    }
    return tty_get_int16(hmtxData + 4 * numHMetrics + 2 * (numHMetrics - glyphIdx));
}

static TTY_int32 tty_get_glyph_advance_height(TTY* font) {
    if (font->vmtx.exists) {
        // TODO: Get from vmtx
        TTY_ASSERT(0);
    }
    
    if (font->OS2.exists) {
        TTY_uint8* os2Data = font->data + font->OS2.off;
        TTY_int16 ascender  = tty_get_int16(os2Data + 68);
        TTY_int16 descender = tty_get_int16(os2Data + 70);
        return ascender - descender;
    }
    
    // Use hhea
    return font->ascender - font->descender;
}

static TTY_int32 tty_get_glyph_top_side_bearing(TTY* font, TTY_int16 yMax) {
    if (font->vmtx.exists) {
        // TODO: Get from vmtx
        TTY_ASSERT(0);
    }

    if (font->OS2.exists) {
        TTY_int16 ascender = tty_get_int16(font->data + font->OS2.off + 68);
        return ascender - yMax;
    }

    // Use hhea
    return font->ascender - yMax;
}

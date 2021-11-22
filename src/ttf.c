#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "ttf.h"


#define TTF_EDGES_PER_CHUNK      10
#define TTF_PIXELS_PER_SCANLINE  0.25f
#define TTF_SUBDIVIDE_SQRD_ERROR 0.01f
#define TTF_SCALAR_VERSION       35


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

enum {
    TTF_CALL      = 0x2B,
    TTF_DUP       = 0x20,
    TTF_EIF       = 0x59,
    TTF_ELSE      = 0x1B,
    TTF_ENDF      = 0x2D,
    TTF_EQ        = 0x54,
    TTF_FDEF      = 0x2C,
    TTF_GETINFO   = 0x88,
    TTF_GPV       = 0x0C,
    TTF_GTEQ      = 0x53,
    TTF_IDEF      = 0x89,
    TTF_IF        = 0x58,
    TTF_LT        = 0x50,
    TTF_MPPEM     = 0x4B,
    TTF_MUL       = 0x63,
    TTF_NPUSHB    = 0x40,
    TTF_NPUSHW    = 0x41,
    TTF_PUSHB     = 0xB0,
    TTF_PUSHB_MAX = 0xB7,
    TTF_PUSHW     = 0xB8,
    TTF_PUSHW_MAX = 0xBF,
    TTF_ROLL      = 0x8A,
    TTF_SCANCTRL  = 0x85,
    TTF_SCVTCI    = 0x1D,
    TTF_SDB       = 0x5E,
    TTF_SDS       = 0x5F,
    TTF_WCVTF     = 0x70,
};

typedef struct {
    float x, y;
} TTF_Point;

typedef struct {
    TTF_Point p0;
    TTF_Point p1;
    TTF_Point p2;
} TTF_Curve;

typedef struct {
    TTF_Curve* curves;
    TTF_uint32 cap;
    TTF_uint32 count;
} TTF_Curve_Array;

typedef struct TTF_Edge {
    TTF_Point p0;
    TTF_Point p1;
    float     invSlope;
    float     yMin;
    float     yMax;
    TTF_int8  dir;
} TTF_Edge;

typedef struct {
    TTF_Edge*  edges;
    TTF_uint32 count;
} TTF_Edge_Array;

typedef struct TTF_Active_Edge {
    TTF_Edge*               edge;
    float                   xIntersection;
    struct TTF_Active_Edge* next;
} TTF_Active_Edge;

typedef struct TTF_Active_Chunk {
    TTF_Active_Edge          edges[TTF_EDGES_PER_CHUNK];
    TTF_uint16               numEdges;
    struct TTF_Active_Chunk* next;
} TTF_Active_Chunk;

typedef struct {
    TTF_Active_Chunk* headChunk;
    TTF_Active_Edge*  headEdge;
    TTF_Active_Edge*  reusableEdges;
} TTF_Active_Edge_List;

typedef struct {
    TTF_uint8* bytes;
    TTF_uint32       off;
} TTF_Ins_Stream;


#define TTF_DEBUG

#ifdef TTF_DEBUG
    #define TTF_PRINT(S)                        printf(S)
    #define TTF_PRINTF(S, ...)                  printf(S, __VA_ARGS__)
    #define TTF_PRINT_EDGE(edge)                ttf__print_edge(edge)
    #define TTF_PRINT_CURVE(curve)              ttf__print_curve(curve)
    #define TTF_PRINT_CVT(instance, numEntries) ttf__print_cvt(instance, numEntries)
    
    static void ttf__print_edge(TTF_Edge* edge) {
        printf("p0=(%f, %f), ", edge->p0.x, edge->p0.y);
        printf("p1=(%f, %f)\n", edge->p1.x, edge->p1.y);
    }

    static void ttf__print_curve(TTF_Curve* curve) {
        printf("p0 = (%.4f, %.4f), ", curve->p0.x, curve->p0.y);
        printf("p1 = (%.4f, %.4f), ", curve->p1.x, curve->p1.y);
        printf("p2 = (%.4f, %.4f)\n", curve->p2.x, curve->p2.y);
    }

    static void ttf__print_cvt(TTF_Instance* instance, TTF_uint32 numEntries) {
        printf("\n-- CVT --\n");
        for (TTF_uint32 i = 0; i < numEntries; i++) {
            printf("%d) %d\n", i, instance->cvt[i]);
        }
    }
#else
    #define TTF_PRINT(S) 
    #define TTF_PRINTF(S)
    #define TTF_PRINT_EDGE(edge)
    #define TTF_PRINT_CURVE(curve)
    #define TTF_PRINT_CVT(font)
#endif


/* -------------- */
/* Initialization */
/* -------------- */
static TTF_bool ttf__read_file_into_buffer            (TTF* font, const char* path);
static TTF_bool ttf__extract_info_from_table_directory(TTF* font);
static TTF_bool ttf__extract_char_encoding            (TTF* font);
static TTF_bool ttf__alloc_mem_for_ins_processing     (TTF* font);


/* ------------------------ */
/* Codepoint to glyph index */
/* ------------------------ */
static TTF_uint32 ttf__get_char_glyph_index         (TTF* font, TTF_uint32 cp);
static TTF_uint16 ttf__get_char_glyph_index_format_4(TTF_uint8* subtable, TTF_uint32 cp);


/* --------- */
/* Rendering */
/* --------- */
static TTF_uint8*       ttf__get_glyf_data_block         (TTF* font, TTF_uint32 idx);
static TTF_bool         ttf__render_simple_glyph         (TTF* font, TTF_Image* image, TTF_uint8* glyphData, TTF_int16 numContours);
static TTF_bool         ttf__render_composite_glyph      (TTF* font, TTF_Image* image, TTF_uint8* glyphData);
static TTF_bool         ttf__get_simple_glyph_curves     (TTF_uint8* glyphData, TTF_Curve_Array* array, TTF_int16 numContours);
static TTF_uint8        ttf__get_next_simple_glyph_flags (TTF_uint8** flagData, TTF_uint8* flagsReps);
static void             ttf__get_next_simple_glyph_point (TTF_uint8 flags, TTF_uint8** xData, TTF_uint8** yData, TTF_Point* absPos, TTF_Point* point);
static void             ttf__peek_next_simple_glyph_point(TTF_uint8 flags, TTF_uint8** xData, TTF_uint8** yData, TTF_Point* absPos, TTF_Point* point);
static TTF_uint8        ttf__get_next_simple_glyph_offset(TTF_uint8* data, TTF_uint8 dualFlag, TTF_uint8 shortFlag, TTF_uint8 flags, float* offset);
static void             ttf__subdivide_curve_into_edges  (TTF_Point* p0, TTF_Point* p1, TTF_Point* p2, TTF_uint8 dir, TTF_Edge_Array* array);
static void             ttf__add_edge_to_array           (TTF_Edge_Array* array, TTF_Point* p0, TTF_Point* p1, TTF_uint8 dir);
static int              ttf__compare_edges               (const void* e0, const void* e1);
static float            ttf__get_scanline_x_intersection (TTF_Edge* edge, float scanline);
static TTF_bool         ttf__active_edge_list_init       (TTF_Active_Edge_List* list);
static TTF_Active_Edge* ttf__get_available_active_edge   (TTF_Active_Edge_List* list);
static TTF_Active_Edge* ttf__insert_active_edge_first    (TTF_Active_Edge_List* list);
static TTF_Active_Edge* ttf__insert_active_edge_after    (TTF_Active_Edge_List* list, TTF_Active_Edge* edge);
static void             ttf__remove_active_edge          (TTF_Active_Edge_List* list, TTF_Active_Edge* prev, TTF_Active_Edge* edge);
static void             ttf__active_edge_list_free       (TTF_Active_Edge_List* list);


/* ---------------------- */
/* Instruction Processing */
/* ---------------------- */
#define ttf__stack_push_F2Dot14(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_push_F26Dot6(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_pop_F2Dot14(font)       ttf__stack_pop_int32(font)
#define ttf__stack_pop_F26Dot6(font)       ttf__stack_pop_int32(font)

#define TTF_GET_NUM_VALS_TO_PUSH(ins) (1 + (ins & 0x7)) /* For PUSHB and PUSHW */

static void       ttf__execute_font_program(TTF* font);
static void       ttf__execute_cv_program  (TTF* font);
static void       ttf__execute_ins         (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void       ttf__CALL                (TTF* font);
static void       ttf__DUP                 (TTF* font);
static void       ttf__EQ                  (TTF* font);
static void       ttf__FDEF                (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__GETINFO             (TTF* font);
static void       ttf__GPV                 (TTF* font);
static void       ttf__GTEQ                (TTF* font);
static void       ttf__IDEF                (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__IF                  (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__LT                  (TTF* font);
static void       ttf__MPPEM               (TTF* font);
static void       ttf__MUL                 (TTF* font);
static void       ttf__NPUSHB              (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__NPUSHW              (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__PUSHB               (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void       ttf__PUSHW               (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void       ttf__ROLL                (TTF* font);
static void       ttf__SCANCTRL            (TTF* font);
static void       ttf__SCVTCI              (TTF* font);
static void       ttf__SDB                 (TTF* font);
static void       ttf__SDS                 (TTF* font);
static void       ttf__WCVTF               (TTF* font);
static void       ttf__ins_stream_init     (TTF_Ins_Stream* stream, TTF_uint8* bytes);
static TTF_uint8  ttf__ins_stream_next     (TTF_Ins_Stream* stream);
static void       ttf__stack_push_uint32   (TTF* font, TTF_uint32 val);
static void       ttf__stack_push_int32    (TTF* font, TTF_int32  val);
static TTF_uint32 ttf__stack_pop_uint32    (TTF* font);
static TTF_int32  ttf__stack_pop_int32     (TTF* font);
static TTF_uint8  ttf__jump_to_else_or_eif (TTF_Ins_Stream* stream);


/* ------- */
/* Utility */
/* ------- */
#define ttf__get_Offset16(data)       ttf__get_uint16(data)
#define ttf__get_Offset32(data)       ttf__get_uint32(data)
#define ttf__get_Version16Dot16(data) ttf__get_uint32(data)

static TTF_uint16 ttf__get_uint16   (TTF_uint8* data);
static TTF_uint32 ttf__get_uint32   (TTF_uint8* data);
static TTF_int16  ttf__get_int16    (TTF_uint8* data);
static TTF_uint16 ttf__get_upem     (TTF* font);
static float      ttf__linear_interp(float p0, float p1, float t);
static void       ttf__get_min_max  (float v0, float v1, float* min, float* max);
static float      ttf__get_inv_slope(TTF_Point* p0, TTF_Point* p1);

/* ---------------------- */
/* Fixed-point operations */
/* ---------------------- */

/* https://stackoverflow.com/a/18067292 */
#define TTF_ROUNDED_DIV(a, b) ((a < 0) ^ (b < 0) ? (a - b / 2) / b : (a + b / 2) / b)

/*
 * The proof: 
 * round(x/y) = floor(x/y + 0.5) = floor((x + y/2)/y) = shift-of-n(x + 2^(n-1)) 
 *
 * https://en.wikipedia.org/wiki/Fixed-point_arithmetic
 */
#define TTF_ROUNDED_DIV_POW2(a, shift) ((a + (1 << (shift-1))) >> shift)

TTF_int32 ttf__rounded_div_32(TTF_int32 a, TTF_int32 b);
TTF_int64 ttf__rounded_div_64(TTF_int64 a, TTF_int64 b);
TTF_int32 ttf__fix_mult      (TTF_int32 a, TTF_int32 b, TTF_uint8 bShift);
TTF_int32 ttf__fix_div       (TTF_int32 a, TTF_int32 b, TTF_int32 bShift);
TTF_int32 ttf__fix_add       (TTF_int32 a, TTF_int32 b);
TTF_int32 ttf__fix_sub       (TTF_int32 a, TTF_int32 b);


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

    if (!ttf__alloc_mem_for_ins_processing(font)) {
        goto init_failure;
    }

    ttf__execute_font_program(font);

    return TTF_TRUE;

init_failure:
    ttf_free(font);
    return TTF_FALSE;
}

TTF_bool ttf_instance_init(TTF* font, TTF_Instance* instance, TTF_uint32 ppem) {
    instance->scale         = ttf__rounded_div_64(ppem << 22, ttf__get_upem(font));
    instance->ppem          = ppem;
    instance->cvtIsOutdated = TTF_TRUE;

    if (font->cvt.exists) {
        // Allocate memory for the Control Value Table and graphics state
        size_t cvtSize = font->cvt.size / sizeof(TTF_FWORD) * sizeof(TTF_F26Dot6);
        size_t gsSize  = sizeof(TTF_Graphics_State);

        instance->mem = calloc(cvtSize + gsSize, 1);
        if (instance->mem == NULL) {
            return TTF_FALSE;
        }

        instance->cvt           = (TTF_F26Dot6*)       (instance->mem);
        instance->graphicsState = (TTF_Graphics_State*)(instance->mem + cvtSize);

        // Set default graphics state values
        instance->graphicsState->scanControl       = TTF_FALSE;
        instance->graphicsState->controlValueCutIn = 68;        // 17/16 (26.6)
        instance->graphicsState->deltaShift        = 3;
        
        // Convert default CVT values, given in FUnits, to 26.6 fixed point 
        // pixel units
        TTF_uint32 idx = 0;
        TTF_uint8* cvt = font->data + font->cvt.off;

        for (TTF_uint32 off = 0; off < font->cvt.size; off += 2) {
            TTF_F26Dot6 funits = ttf__get_int16(cvt + off) << 6;
            instance->cvt[idx++] = ttf__fix_mult(funits, instance->scale, 22);
        }

        TTF_PRINT_CVT(instance, font->cvt.size / 2);
    }
    else {
        instance->cvt = NULL;
    }

    return TTF_TRUE;
}

TTF_bool ttf_image_init(TTF_Image* image, TTF_uint8* pixels, TTF_uint32 w, TTF_uint32 h, TTF_uint32 stride) {
    if (pixels == NULL) {
        pixels = calloc(stride * h, 1);
        if (pixels == NULL) {
            return TTF_FALSE;
        }
    }

    image->pixels = pixels;
    image->w      = w;
    image->h      = h;
    image->stride = stride;
    return TTF_TRUE;
}

void ttf_free(TTF* font) {
    if (font) {
        free(font->data);
        free(font->insMem);
    }
}

void ttf_free_instance(TTF* font, TTF_Instance* instance) {
    if (instance) {
        if (font->instance == instance) {
            font->instance = NULL;
        }
        free(instance->cvt);
    }
}

void ttf_free_image(TTF_Image* image) {
    if (image) {
        free(image->pixels);
    }
}

void ttf_set_current_instance(TTF* font, TTF_Instance* instance) {
    font->instance = instance;

    if (font->cvt.exists && instance->cvtIsOutdated) {
        ttf__execute_cv_program(font);
        font->instance->cvtIsOutdated = TTF_FALSE;
    }
}

TTF_bool ttf_render_glyph(TTF* font, TTF_Image* image, TTF_uint32 cp) {
    // TODO
    assert(0);
    return TTF_FALSE;
}

TTF_bool ttf_render_glyph_to_existing_image(TTF* font, TTF_Image* image, TTF_uint32 cp, TTF_uint32 x, TTF_uint32 y) {
    assert(font->instance != NULL);

    TTF_uint32 idx         = ttf__get_char_glyph_index(font, cp);
    TTF_uint8* glyphData   = ttf__get_glyf_data_block(font, idx);
    TTF_int16  numContours = ttf__get_int16(glyphData);

    if (numContours < 0) {
        return ttf__render_composite_glyph(font, image, glyphData);
    }

    return ttf__render_simple_glyph(font, image, glyphData, numContours);
}


/* -------------- */
/* Initialization */
/* -------------- */
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
        else if (!font->maxp.exists && !memcmp(tag, "maxp", 4)) {
            table = &font->maxp;
        }
        else if (!font->loca.exists && !memcmp(tag, "loca", 4)) {
            table = &font->loca;
        }
        else if (!font->prep.exists && !memcmp(tag, "prep", 4)) {
            table = &font->prep;
        }

        if (table) {
            table->exists = TTF_TRUE;
            table->off    = ttf__get_Offset32(record + 8);
            table->size   = ttf__get_uint32(record + 12);
        }
    }

    return 
        font->cmap.exists && 
        font->glyf.exists && 
        font->head.exists && 
        font->maxp.exists && 
        font->loca.exists;
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
        }
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


/* ------------------------ */
/* Codepoint to glyph index */
/* ------------------------ */
static TTF_uint32 ttf__get_char_glyph_index(TTF* font, TTF_uint32 cp) {
    TTF_uint8* subtable = font->data + font->cmap.off + font->encoding.off;

    switch (ttf__get_uint16(subtable)) {
        case 4:
            return ttf__get_char_glyph_index_format_4(subtable, cp);
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

static TTF_uint16 ttf__get_char_glyph_index_format_4(TTF_uint8* subtable, TTF_uint32 cp) {
    #define TTF_GET_END_CODE(index) ttf__get_uint16(subtable + 14 + 2 * (index))
    
    // TODO: Not sure how these values are supposed to be used in the binary search so it will be
    //       done without them.
    //
    // TTF_uint16 searchRange   = ttf__get_uint16(data + 8);
    // TTF_uint16 entrySelector = ttf__get_uint16(data + 10);
    // TTF_uint16 rangeShift    = ttf__get_uint16(data + 12);
    
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


/* --------- */
/* Rendering */
/* --------- */
static TTF_uint8* ttf__get_glyf_data_block(TTF* font, TTF_uint32 idx) {
    TTF_int16 version = ttf__get_int16(font->data + font->head.off + 50);
        
    TTF_Offset32 blockOff  = 
        version == 0 ? 
        ttf__get_Offset16(font->data + font->loca.off + (2 * idx)) * 2 :
        ttf__get_Offset32(font->data + font->loca.off + (4 * idx));
    
    return font->data + font->glyf.off + blockOff;
}

static TTF_bool ttf__render_simple_glyph(TTF* font, TTF_Image* image,TTF_uint8* glyphData, TTF_int16 numContours) {
    TTF_Curve_Array curveArray;
    if (!ttf__get_simple_glyph_curves(glyphData, &curveArray, numContours)) {
        return TTF_FALSE;
    }
    
    
    {
        #define TTF_TO_BITMAP_SPACE(p)         \
            p.x = sf * (p.x + xOff),           \
            p.y = sf * (-(p.y + yOff) + height)
        
        float xMin   = ttf__get_int16(glyphData + 2);
        float yMin   = ttf__get_int16(glyphData + 4);
        float yMax   = ttf__get_int16(glyphData + 8);
        float xOff   = xMin < 0.0f ? fabs(xMin) : 0.0f;
        float yOff   = yMin < 0.0f ? fabs(yMin) : 0.0f;
        float sf     = (float)font->instance->ppem / ttf__get_upem(font);
        float height = fabs(yMin) + fabs(yMax);
        
        for (TTF_uint32 i = 0; i < curveArray.count; i++) {
            TTF_Curve* curve = curveArray.curves + i;
            
            TTF_TO_BITMAP_SPACE(curve->p0);
            TTF_TO_BITMAP_SPACE(curve->p1);
            TTF_TO_BITMAP_SPACE(curve->p2);
        }
        
        #undef TTF_TO_BITMAP_SPACE
    }
    
    
    TTF_Edge_Array edgeArray = { 0 };
    
    // Count the number of edges needed
    for (TTF_uint32 i = 0; i < curveArray.count; i++) {
        TTF_Curve* curve = curveArray.curves + i;
        
        if (curve->p1.x == curve->p2.x && curve->p1.y == curve->p2.y) {
            // The curve is a straight line, no need to flatten it
            edgeArray.count++;
        }
        else {
            ttf__subdivide_curve_into_edges(&curve->p0, &curve->p1, &curve->p2, 0, &edgeArray);
        }
    }
    
    edgeArray.edges = malloc(sizeof(TTF_Edge) * edgeArray.count);
    if (edgeArray.edges == NULL) {
        free(curveArray.curves);
        return TTF_FALSE;
    }
    
    edgeArray.count = 0;
    for (TTF_uint32 i = 0; i < curveArray.count; i++) {
        TTF_Curve* curve = curveArray.curves + i;
        TTF_uint8  dir   = curve->p2.y < curve->p0.y ? 1 : -1;
        
        if (curve->p1.x == curve->p2.x && curve->p1.y == curve->p2.y) {
            ttf__add_edge_to_array(&edgeArray, &curve->p0, &curve->p2, dir);
        }
        else {
            ttf__subdivide_curve_into_edges(&curve->p0, &curve->p1, &curve->p2, dir, &edgeArray);
        }
    }
    
    
    free(curveArray.curves);
    qsort(edgeArray.edges, edgeArray.count, sizeof(TTF_Edge), ttf__compare_edges);
    
    
    TTF_Active_Edge_List activeEdgeList;
    if (!ttf__active_edge_list_init(&activeEdgeList)) {
        free(edgeArray.edges);
        return TTF_FALSE;
    }
    
    
    float y    = floorf(fmaxf(edgeArray.edges->yMin, 0.0f));
    float yEnd = fminf(ceilf(edgeArray.edges[edgeArray.count - 1].yMax), image->h);
    
    TTF_uint32 edgeOffset = 0;
    
    while (y <= yEnd) {
        {
            // If an edge is no longer active, remove it from the list, else 
            // update its x-intersection with the current scanline.
            //
            // TODO: Resort edges based on new x-intersections?
            
            TTF_Active_Edge* activeEdge     = activeEdgeList.headEdge;
            TTF_Active_Edge* prevActiveEdge = NULL;
        
            while (activeEdge != NULL) {
                TTF_Active_Edge* next = activeEdge->next;
                
                if (activeEdge->edge->yMax <= y) {
                    ttf__remove_active_edge(&activeEdgeList, prevActiveEdge, activeEdge);
                }
                else {
                    activeEdge->xIntersection = 
                        ttf__get_scanline_x_intersection(activeEdge->edge, y);
                    
                    prevActiveEdge = activeEdge;
                }
                
                activeEdge = next;
            }
        }
        
        
        // Find any edges that intersect the current scanline and insert them
        // into the active edge list.
        for (TTF_uint32 i = edgeOffset; i < edgeArray.count; i++) {
            TTF_Edge* edge = edgeArray.edges + i;
            
            if (edge->yMin > y) {
                break;
            }
            
            if (edge->yMax > y) {
                float xIntersection = ttf__get_scanline_x_intersection(edge, y);
                
                TTF_Active_Edge* activeEdge     = activeEdgeList.headEdge;
                TTF_Active_Edge* prevActiveEdge = NULL;
                
                while (activeEdge != NULL) {
                    if (xIntersection <= activeEdge->xIntersection) {
                        break;
                    }
                    prevActiveEdge = activeEdge;
                    activeEdge     = activeEdge->next;
                }
                
                TTF_Active_Edge* newActiveEdge = 
                    prevActiveEdge == NULL ?
                    ttf__insert_active_edge_first(&activeEdgeList) :
                    ttf__insert_active_edge_after(&activeEdgeList, prevActiveEdge);
                
                if (newActiveEdge == NULL) {
                    free(edgeArray.edges);
                    ttf__active_edge_list_free(&activeEdgeList);
                    return TTF_FALSE;
                }
                
                newActiveEdge->edge          = edge;
                newActiveEdge->xIntersection = xIntersection;
            }
            
            edgeOffset++;
        }
        
        
        // Set the opacity of pixels along the scanline
        if (activeEdgeList.headEdge != NULL) {
            TTF_Active_Edge* activeEdge    = activeEdgeList.headEdge;
            TTF_int32        windingNumber = 0;
            
            TTF_uint32 x      = ceilf(fabs(activeEdge->xIntersection));
            TTF_uint32 xPrev  = x == 0 ? x : x - 1;
            TTF_uint32 rowOff = (TTF_uint32)y * image->stride;
            
            float weightedAlpha  = 255.0f * TTF_PIXELS_PER_SCANLINE;
            float fullPixelAlpha = 0.0f;
            
            while(TTF_TRUE) {
                if (windingNumber == 0) {
                    image->pixels[xPrev + rowOff] += 
                        weightedAlpha * (x - activeEdge->xIntersection);
                }
                else {
                    image->pixels[xPrev + rowOff] += 
                        weightedAlpha * (activeEdge->xIntersection - xPrev);
                }
                
                windingNumber  += activeEdge->edge->dir;
                activeEdge     =  activeEdge->next;
                fullPixelAlpha =  weightedAlpha * windingNumber;
                
                if (activeEdge == NULL) {
                    break;
                }
                
                if (x < activeEdge->xIntersection) {
                    xPrev = x;
                    x++;
                    while (x < activeEdge->xIntersection) {
                        image->pixels[xPrev + rowOff] += fullPixelAlpha;
                        xPrev = x;
                        x++;
                    }
                }
            }
        }
        
        y += TTF_PIXELS_PER_SCANLINE;
    }
    
    
    free(edgeArray.edges);
    ttf__active_edge_list_free(&activeEdgeList);
    return TTF_TRUE;
}

static TTF_bool ttf__render_composite_glyph(TTF* font, TTF_Image* image, TTF_uint8* glyphData) {
    // TODO
    assert(0);
    return TTF_TRUE;
}

static TTF_bool ttf__get_simple_glyph_curves(TTF_uint8* glyphData, TTF_Curve_Array* array, TTF_int16 numContours) {
    assert(numContours >= 0 && "The glyph is a composite glyph.");


    // The number of glyph curves is <= the number of glyph points
    array->count  = 0;
    array->cap    = 1 + ttf__get_uint16(glyphData + 8 + 2 * numContours);
    array->curves = malloc(array->cap * sizeof(TTF_Curve));
    if (array->curves == NULL) {
        return TTF_FALSE;
    }
    
    
    TTF_uint8* flagData, *xData, *yData;
    {
        flagData  = glyphData + (10 + 2 * numContours);
        flagData += 2 + ttf__get_uint16(flagData);
        
        TTF_uint32 flagsSize = 0;
        TTF_uint32 xDataSize = 0;
        
        for (TTF_uint32 i = 0; i < array->cap;) {
            TTF_uint8 flags = flagData[flagsSize];
            
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

            TTF_uint8 xSize;
            if (flags & TTF_GLYF_X_SHORT_VECTOR) {
                xSize = 1;
            }
            else if (flags & TTF_GLYF_X_DUAL) {
                xSize = 0;
            }
            else {
                xSize = 2;
            }

            while (flagsReps > 0) {
                xDataSize += xSize;
                flagsReps--;
            }
        }
        
        xData = flagData + flagsSize;
        yData = xData    + xDataSize;
    }
    
    
    TTF_uint16 startPointIdx = 0;
    TTF_uint8  flagsReps     = 0;
    TTF_Point  absPos        = { 0 };
    
    for (TTF_uint32 i = 0; i < numContours; i++) {
        TTF_uint16 endPointIdx   = ttf__get_uint16(glyphData + 10 + 2 * i);
        TTF_bool   addFinalCurve = TTF_TRUE;

        TTF_Point startPoint;
        {
            TTF_uint8 flags = ttf__get_next_simple_glyph_flags(&flagData, &flagsReps);
            ttf__get_next_simple_glyph_point(flags, &xData, &yData, &absPos, &startPoint);
        }

        TTF_Point* nextP0 = &startPoint;

        for (TTF_uint16 j = startPointIdx + 1; j <= endPointIdx; j++) {
            TTF_Curve* curve = array->curves + array->count++;
            TTF_uint8  flags = ttf__get_next_simple_glyph_flags(&flagData, &flagsReps);
            
            ttf__get_next_simple_glyph_point(flags, &xData, &yData, &absPos, &curve->p1);
            
            if (flags & TTF_GLYF_ON_CURVE_POINT) {
                curve->p2 = curve->p1;
            }
            else if (j == endPointIdx) {
                curve->p2     = startPoint;
                addFinalCurve = TTF_FALSE;
            }
            else {
                flags = *flagData;
                
                if (flags & TTF_GLYF_ON_CURVE_POINT) {
                    ttf__get_next_simple_glyph_flags(&flagData, &flagsReps);
                    ttf__get_next_simple_glyph_point(flags, &xData, &yData, &absPos, &curve->p2);
                    j++;
                }
                else { // Implied on curve point
                    ttf__peek_next_simple_glyph_point(flags, &xData, &yData, &absPos, &curve->p2);
                    curve->p2.x = ttf__linear_interp(curve->p2.x, curve->p1.x, 0.5f);
                    curve->p2.y = ttf__linear_interp(curve->p2.y, curve->p1.y, 0.5f);
                }
            }

            curve->p0 = *nextP0;
            nextP0    = &curve->p2;
        }

        if (addFinalCurve) {
            TTF_Curve* finalCurve = array->curves + array->count;
            finalCurve->p0 = *nextP0;
            finalCurve->p1 = startPoint;
            finalCurve->p2 = startPoint;
            array->count++;
        }
        
        startPointIdx = endPointIdx + 1;
    }
    
    return TTF_TRUE;
}

static TTF_uint8 ttf__get_next_simple_glyph_flags(TTF_uint8** flagData, TTF_uint8* flagsReps) {
    TTF_uint8 flags = **flagData;
    
    if (*flagsReps > 0) {
        (*flagsReps)--;

        if (*flagsReps == 0) {
            (*flagData) += 2;
        }
    }
    else if (flags & TTF_GLYF_REPEAT_FLAG) {
        *flagsReps = (*flagData)[1];
    }
    else {
        (*flagData)++;
    }
    
    return flags;
}

static void ttf__get_next_simple_glyph_point(TTF_uint8 flags, TTF_uint8** xData, TTF_uint8** yData, TTF_Point* absPos, TTF_Point* point) {
    (*xData) += ttf__get_next_simple_glyph_offset(
        *xData, TTF_GLYF_X_DUAL, TTF_GLYF_X_SHORT_VECTOR, flags, &point->x);
    
    (*yData) += ttf__get_next_simple_glyph_offset(
        *yData, TTF_GLYF_Y_DUAL, TTF_GLYF_Y_SHORT_VECTOR, flags, &point->y);
    
    point->x += absPos->x;
    point->y += absPos->y;
    *absPos  = *point;
}

static void ttf__peek_next_simple_glyph_point(TTF_uint8 flags, TTF_uint8** xData, TTF_uint8** yData, TTF_Point* absPos, TTF_Point* point) {
    ttf__get_next_simple_glyph_offset(
        *xData, TTF_GLYF_X_DUAL, TTF_GLYF_X_SHORT_VECTOR, flags, &point->x);
    
    ttf__get_next_simple_glyph_offset(
        *yData, TTF_GLYF_Y_DUAL, TTF_GLYF_Y_SHORT_VECTOR, flags, &point->y);
    
    point->x += absPos->x;
    point->y += absPos->y;
}

static TTF_uint8 ttf__get_next_simple_glyph_offset(TTF_uint8* data, TTF_uint8 dualFlag, TTF_uint8 shortFlag, TTF_uint8 flags, float* offset) {
    if (flags & shortFlag) {
        if ((flags & dualFlag) == 0) {
            *offset = -(*data);
        }
        else {
            *offset = *data;
        }
        return 1;
    }
    else if (flags & dualFlag) {
        *offset = 0.0f;
        return 0;
    }

    *offset = ttf__get_int16(data);
    return 2;
}

static void ttf__subdivide_curve_into_edges(TTF_Point* p0, TTF_Point* p1, TTF_Point* p2, TTF_uint8 dir, TTF_Edge_Array* array) {
    #define TTF_DIVIDE(a, b)        \
        { ((a)->x + (b)->x) / 2.0f, \
          ((a)->y + (b)->y) / 2.0f }
    
    TTF_Point mid0 = TTF_DIVIDE(p0, p1);
    TTF_Point mid1 = TTF_DIVIDE(p1, p2);
    TTF_Point mid2 = TTF_DIVIDE(&mid0, &mid1);
    
    {
        TTF_Point d = TTF_DIVIDE(p0, p2);
        d.x -= mid2.x;
        d.y -= mid2.y;
        
        if (d.x * d.x + d.y * d.y <= TTF_SUBDIVIDE_SQRD_ERROR) {
            if (array->edges == NULL) {
                // The edges are just being counted, not created
                array->count++;
            }
            else {
                ttf__add_edge_to_array(array, p0, p2, dir);
            }
            return;
        }
    }
    
    ttf__subdivide_curve_into_edges(p0, &mid0, &mid2, dir, array);
    ttf__subdivide_curve_into_edges(&mid2, &mid1, p2, dir, array);
    
    #undef TTF_DIVIDE
}

static void ttf__add_edge_to_array(TTF_Edge_Array* array, TTF_Point* p0, TTF_Point* p1, TTF_uint8 dir) {
    TTF_Edge* edge = array->edges + array->count++;
    edge->p0       = *p0;
    edge->p1       = *p1;
    edge->invSlope = ttf__get_inv_slope(p0, p1);
    edge->dir      = dir;
    ttf__get_min_max(p0->y, p1->y, &edge->yMin, &edge->yMax);
}

static int ttf__compare_edges(const void* e0, const void* e1) {
    if (((TTF_Edge*)e0)->yMin < ((TTF_Edge*)e1)->yMin) {
        return -1;
    }
    
    if (((TTF_Edge*)e0)->yMin > ((TTF_Edge*)e1)->yMin) {
        return 1;
    }
    
    return 0;
}

static float ttf__get_scanline_x_intersection(TTF_Edge* edge, float scanline) {
    return (scanline - edge->p0.y) * edge->invSlope + edge->p0.x;
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

static TTF_Active_Edge* ttf__get_available_active_edge(TTF_Active_Edge_List* list) {
    if (list->reusableEdges != NULL) {
        TTF_Active_Edge* edge = list->reusableEdges;
        list->reusableEdges = list->reusableEdges->next;
        return edge;
    }
    
    if (list->headChunk->numEdges == TTF_EDGES_PER_CHUNK) {
        TTF_Active_Chunk* chunk = calloc(1, sizeof(TTF_Active_Chunk));
        if (chunk == NULL) {
            return NULL;
        }
        chunk->next     = list->headChunk;
        list->headChunk = chunk;
    }

    TTF_Active_Edge* edge = list->headChunk->edges + list->headChunk->numEdges;
    memset(edge, 0, sizeof(TTF_Active_Edge));
    list->headChunk->numEdges++;
    return edge;
}

static TTF_Active_Edge* ttf__insert_active_edge_first(TTF_Active_Edge_List* list) {
    TTF_Active_Edge* edge = ttf__get_available_active_edge(list);
    edge->next     = list->headEdge;
    list->headEdge = edge;
    return edge;
}

static TTF_Active_Edge* ttf__insert_active_edge_after(TTF_Active_Edge_List* list, TTF_Active_Edge* edge) {
    TTF_Active_Edge* newEdge = ttf__get_available_active_edge(list);
    newEdge->next = edge->next;
    edge->next    = newEdge;
    return newEdge;
}

static void ttf__remove_active_edge(TTF_Active_Edge_List* list, TTF_Active_Edge* prev, TTF_Active_Edge* edge) {
    if (prev == NULL) {
        list->headEdge = list->headEdge->next;
    }
    else {
        prev->next = edge->next;
    }
    
    edge->next          = list->reusableEdges;
    list->reusableEdges = edge;
}

static void ttf__active_edge_list_free(TTF_Active_Edge_List* list) {
    TTF_Active_Chunk* chunk = list->headChunk;
    while (chunk != NULL) {
        TTF_Active_Chunk* next = chunk->next;
        free(chunk);
        chunk = next;
    }
}


/* ---------------------- */
/* Instruction Processing */
/* ---------------------- */
static void ttf__execute_font_program(TTF* font) {
    TTF_PRINT("\n-- Font Program --\n");

    TTF_Ins_Stream stream;
    ttf__ins_stream_init(&stream, font->data + font->fpgm.off);
    
    while (stream.off < font->fpgm.size) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        assert(ins == TTF_PUSHB || ins == TTF_FDEF || ins == TTF_IDEF);
        ttf__execute_ins(font, &stream, ins);
    }
}

static void ttf__execute_cv_program(TTF* font) {
    TTF_PRINT("\n-- CV Program --\n");

    TTF_Ins_Stream stream;
    ttf__ins_stream_init(&stream, font->data + font->prep.off);
    
    while (stream.off < font->prep.size) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        ttf__execute_ins(font, &stream, ins);
    }
}

static void ttf__execute_ins(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    switch (ins) {
        case TTF_CALL:
            ttf__CALL(font);
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
        case TTF_GETINFO:
            ttf__GETINFO(font);
            return;
        case TTF_GPV:
            ttf__GPV(font);
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
        case TTF_LT:
            ttf__LT(font);
            return;
        case TTF_MPPEM:
            ttf__MPPEM(font);
            return;
        case TTF_MUL:
            ttf__MUL(font);
            return;
        case TTF_NPUSHB:
            ttf__NPUSHB(font, stream);
            return;
        case TTF_NPUSHW:
            ttf__NPUSHW(font, stream);
            return;
        case TTF_ROLL:
            ttf__ROLL(font);
            return;
        case TTF_SCANCTRL:
            ttf__SCANCTRL(font);
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
        case TTF_WCVTF:
            ttf__WCVTF(font);
            return;
    }

    if (ins >= TTF_PUSHB && ins <= TTF_PUSHB_MAX) {
        ttf__PUSHB(font, stream, ins);
        return;
    }
    else if (ins >= TTF_PUSHW && ins <= TTF_PUSHW_MAX) {
        ttf__PUSHW(font, stream, ins);
        return;
    }

    TTF_PRINTF("Unknown instruction: %#x\n", ins);
    assert(0);
}

static void ttf__CALL(TTF* font) {
    // Starting with the first instruction, a call to a function executes 
    // instructions until instruction 0x2D (ENDF) is reached.
    TTF_Ins_Stream stream;
    {
        TTF_uint32 funcId = ttf__stack_pop_uint32(font);
        assert(funcId < font->funcArray.count);

        ttf__ins_stream_init(&stream, font->funcArray.funcs[funcId].firstIns);
        TTF_PRINTF("CALL %#X\n", funcId);
    }

    while (TTF_TRUE) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        
        if (ins == TTF_ENDF) {
            break;
        }

        ttf__execute_ins(font, &stream, ins);
    };
    
    TTF_PRINT("\n");
}

static void ttf__DUP(TTF* font) {
    TTF_PRINT("DUP\n");
    TTF_uint32 e = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e);
    ttf__stack_push_uint32(font, e);
}

static void ttf__EQ(TTF* font) {
    TTF_PRINT("EQ\n");
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 == e2 ? 1 : 0);
}

static void ttf__FDEF(TTF* font, TTF_Ins_Stream* stream) {
    assert(font->funcArray.count < font->funcArray.cap);

    TTF_uint32 funcId = ttf__stack_pop_uint32(font);

    font->funcArray.funcs[funcId].firstIns = stream->bytes + stream->off;
    font->funcArray.count++;

    TTF_PRINTF("FDEF %#X\n", funcId);

    while (ttf__ins_stream_next(stream) != TTF_ENDF);
}

static void ttf__GETINFO(TTF* font) {
    // These are the only supported selector bits for scalar version 35
    enum {
        TTF_VERSION                  = 0x01,
        TTF_GLYPH_ROTATED            = 0x02,
        TTF_GLYPH_STRETCHED          = 0x04,
        TTF_FONT_SMOOTHING_GRAYSCALE = 0x20,
    };

    TTF_uint32 result   = 0;
    TTF_uint32 selector = ttf__stack_pop_uint32(font);

    TTF_PRINTF("GETINFO %d\n", selector);

    if (selector & TTF_VERSION) {
        result = TTF_SCALAR_VERSION;
    }
    if (selector & TTF_GLYPH_ROTATED) {
        if (font->instance->rotated) {
            result |= 0x100;
        }
    }
    if (selector & TTF_GLYPH_STRETCHED) {
        if (font->instance->stretched) {
            result |= 0x200;
        }
    }
    if (selector & TTF_FONT_SMOOTHING_GRAYSCALE) {
        result |= 0x1000;
    }

    ttf__stack_push_uint32(font, result);
}

static void ttf__GPV(TTF* font) {
    // TODO
    assert(0);
}

static void ttf__GTEQ(TTF* font) {
    TTF_PRINT("GTEQ\n");
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 >= e2 ? 1 : 0);
}

static void ttf__IDEF(TTF* font, TTF_Ins_Stream* stream) {
    // TODO
    assert(0);
}

static void ttf__IF(TTF* font, TTF_Ins_Stream* stream) {
    TTF_PRINT("IF ");

    if (ttf__stack_pop_uint32(font) == 0) {
        TTF_PRINT("(FALSE)\n");

        if (ttf__jump_to_else_or_eif(stream) == TTF_EIF) {
            // Condition is false and there is no else instruction
            return;
        }
    }
    else {
        TTF_PRINT("(TRUE)\n");
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

static void ttf__LT(TTF* font) {
    TTF_PRINT("LT\n");
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 < e2 ? 1 : 0);
}

static void ttf__MPPEM(TTF* font) {
    // TODO: If the font is stretched, (i.e. horizontal ppem != vertical ppem),
    //       then ppem must be measured in the direction of the projection 
    //       vector.
    TTF_PRINT("MPPEM\n");
    ttf__stack_push_uint32(font, font->instance->ppem);
}

static void ttf__MUL(TTF* font) {
    TTF_PRINT("MUL\n");
    TTF_F26Dot6 n1 = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 n2 = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, ttf__fix_mult(n1, n2, 6));
}

static void ttf__NPUSHB(TTF* font, TTF_Ins_Stream* stream) {
    assert(0);
}

static void ttf__NPUSHW(TTF* font, TTF_Ins_Stream* stream) {
    assert(0);
}

static void ttf__PUSHB(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    TTF_uint8 n = TTF_GET_NUM_VALS_TO_PUSH(ins);
    TTF_PRINTF("PUSHB %d\n", n);

    do {
        TTF_uint8 byte = ttf__ins_stream_next(stream);
        ttf__stack_push_uint32(font, byte);
    } while (--n);
}

static void ttf__PUSHW(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    TTF_uint32 n = TTF_GET_NUM_VALS_TO_PUSH(ins);
    TTF_PRINTF("PUSHW %d\n", n);

    do {
        TTF_uint8 ms  = ttf__ins_stream_next(stream);
        TTF_uint8 ls  = ttf__ins_stream_next(stream);
        TTF_int32 val = (ms << 8) | ls;
        TTF_PRINTF("\t%d\n", val);
        ttf__stack_push_int32(font, val);
    } while (--n);
}

static void ttf__ROLL(TTF* font) {
    TTF_PRINT("ROLL\n");
    TTF_uint32 a = ttf__stack_pop_uint32(font);
    TTF_uint32 b = ttf__stack_pop_uint32(font);
    TTF_uint32 c = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, b);
    ttf__stack_push_uint32(font, a);
    ttf__stack_push_uint32(font, c);
}

static void ttf__SCANCTRL(TTF* font) {
    TTF_PRINT("SCANCTRL\n");
    
    TTF_uint16 flags  = ttf__stack_pop_uint32(font);
    TTF_uint8  thresh = flags & 0xFF;
    
    if (thresh == 0xFF) {
        font->instance->graphicsState->scanControl = TTF_TRUE;
    }
    else if (thresh == 0x0) {
        font->instance->graphicsState->scanControl = TTF_FALSE;
    }
    else {
        if (flags & 0x100) {
            if (font->instance->ppem <= thresh) {
                font->instance->graphicsState->scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x200) {
            if (font->instance->rotated) {
                font->instance->graphicsState->scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x400) {
            if (font->instance->stretched) {
                font->instance->graphicsState->scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x800) {
            if (thresh > font->instance->ppem) {
                font->instance->graphicsState->scanControl = TTF_FALSE;
            }
        }

        if (flags & 0x1000) {
            if (!font->instance->rotated) {
                font->instance->graphicsState->scanControl = TTF_FALSE;
            }
        }

        if (flags & 0x2000) {
            if (!font->instance->stretched) {
                font->instance->graphicsState->scanControl = TTF_FALSE;
            }
        }
    }
}

static void ttf__SCVTCI(TTF* font) {
    TTF_PRINT("SCVTCI\n");
    font->instance->graphicsState->controlValueCutIn = ttf__stack_pop_F26Dot6(font);
}

static void ttf__SDB(TTF* font) {
    TTF_PRINT("SDB\n");
    font->instance->graphicsState->deltaBase = ttf__stack_pop_uint32(font);
}

static void ttf__SDS(TTF* font) {
    TTF_PRINT("SDS\n");
    font->instance->graphicsState->deltaShift = ttf__stack_pop_uint32(font);
}

static void ttf__WCVTF(TTF* font) {
    TTF_uint32 funits = ttf__stack_pop_uint32(font);
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);

    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    font->instance->cvt[cvtIdx] = ttf__fix_mult(funits << 6, font->instance->scale, 22);

    TTF_PRINTF("WCVTF cvt[%d] = %d\n", cvtIdx, font->instance->cvt[cvtIdx]);
}

static void ttf__ins_stream_init(TTF_Ins_Stream* stream, TTF_uint8* bytes) {
    stream->bytes = bytes;
    stream->off   = 0;
}

static TTF_uint8 ttf__ins_stream_next(TTF_Ins_Stream* stream) {
    return stream->bytes[stream->off++];
}

static void ttf__ins_stream_skip(TTF_Ins_Stream* stream, TTF_uint32 count) {
    stream->off += count;
}

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

static TTF_uint8 ttf__jump_to_else_or_eif(TTF_Ins_Stream* stream) {
    TTF_uint32 numNested = 0;

    while (TTF_TRUE) {
        TTF_uint8 ins = ttf__ins_stream_next(stream);
        
        if (ins == TTF_PUSHB) {
            ttf__ins_stream_skip(stream, TTF_GET_NUM_VALS_TO_PUSH(ins));
        }
        else if (ins == TTF_PUSHW) {
            ttf__ins_stream_skip(stream, 2 * TTF_GET_NUM_VALS_TO_PUSH(ins));
        }
        else if (ins == TTF_NPUSHB) {
            ttf__ins_stream_skip(stream, ttf__ins_stream_next(stream));
        }
        else if (ins == TTF_NPUSHW) {
            ttf__ins_stream_skip(stream, 2 * ttf__ins_stream_next(stream));
        }
        else if (ins == TTF_IF) {
            numNested++;
        }
        else if (numNested == 0){
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


/* ------- */
/* Utility */
/* ------- */
static TTF_uint16 ttf__get_uint16(TTF_uint8* data) {
    return data[0] << 8 | data[1];
}

static TTF_uint32 ttf__get_uint32(TTF_uint8* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static TTF_int16 ttf__get_int16(TTF_uint8* data) {
    return data[0] << 8 | data[1];
}

static float ttf__linear_interp(float p0, float p1, float t) {
    return p0 + t * (p1 - p0);
}

static TTF_uint16 ttf__get_upem(TTF* font) {
    return ttf__get_uint16(font->data + font->head.off + 18);
}

static void ttf__get_min_max(float v0, float v1, float* min, float* max) {
    if (v0 < v1) {
        *min = v0;
        *max = v1;
    }
    else {
        *min = v1;
        *max = v0;
    }
}

static float ttf__get_inv_slope(TTF_Point* p0, TTF_Point* p1) {
    if (p0->x == p1->x) {
        return 0.0f;
    }
    if (p0->y == p1->y) {
        return 0.0f;
    }
    return 1.0f / ((p1->y - p0->y) / (p1->x - p0->x));
}


/* ---------------------- */
/* Fixed-point operations */
/* ---------------------- */
TTF_int32 ttf__rounded_div_32(TTF_int32 a, TTF_int32 b) {
    return TTF_ROUNDED_DIV(a, b);
}

TTF_int64 ttf__rounded_div_64(TTF_int64 a, TTF_int64 b) {
    return TTF_ROUNDED_DIV(a, b);
}

TTF_int32 ttf__fix_mult(TTF_int32 a, TTF_int32 b, TTF_uint8 bShift) {
    return TTF_ROUNDED_DIV_POW2((TTF_uint64)a * (TTF_uint64)b, bShift);
}

TTF_int32 ttf__fix_div(TTF_int32 a, TTF_int32 b, TTF_int32 bShift) {
    return ttf__rounded_div_32(a, TTF_ROUNDED_DIV_POW2(a, bShift));
}

TTF_int32 ttf__fix_add(TTF_int32 a, TTF_int32 b) {
    return a + b;
}

TTF_int32 ttf__fix_sub(TTF_int32 a, TTF_int32 b) {
    return a - b;
}

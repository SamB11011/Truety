#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "ttf.h"


/* -------------- */
/* Initialization */
/* -------------- */
static TTF_bool ttf__read_file_into_buffer            (TTF* font, const char* path);
static TTF_bool ttf__extract_info_from_table_directory(TTF* font);
static TTF_bool ttf__extract_char_encoding            (TTF* font);
static TTF_bool ttf__format_is_supported              (TTF_uint16 format);
static TTF_bool ttf__alloc_mem_for_ins_processing     (TTF* font);


/* ------------------- */
/* Glyph Index Mapping */
/* ------------------- */
static TTF_uint32 ttf__get_glyph_index         (TTF* font, TTF_uint32 cp);
static TTF_uint16 ttf__get_glyph_index_format_4(TTF_uint8* subtable, TTF_uint32 cp);


/* --------------- */
/* glyf Operations */
/* --------------- */
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

static TTF_uint8* ttf__get_glyf_data_block   (TTF* font);
static TTF_int16  ttf__get_num_glyph_contours(TTF* font);
static TTF_int32  ttf__get_num_glyph_points  (TTF* font);
static TTF_bool   ttf__extract_glyph_points  (TTF* font);
static TTF_int32  ttf__get_next_coord_off    (TTF_uint8** data, TTF_uint8 dualFlag, TTF_uint8 shortFlag, TTF_uint8 flags);
static void       ttf__scale_glyph_points    (TTF* font, TTF_V2* points, TTF_F26Dot6_V2* scaled, TTF_uint32 numPoints);


/* --------------- */
/* Zone Operations */
/* --------------- */
static TTF_bool ttf__zone_init(TTF_Zone* zone, TTF_uint32 cap);
static void     ttf__zone_free(TTF_Zone* zone);


/* ---------------------------- */
/* Interpreter Stack Operations */
/* ---------------------------- */
#define ttf__stack_push_F2Dot14(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_push_F26Dot6(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_pop_F2Dot14(font)       ttf__stack_pop_int32(font)
#define ttf__stack_pop_F26Dot6(font)       ttf__stack_pop_int32(font)

static void        ttf__stack_push_uint32   (TTF* font, TTF_uint32 val);
static void        ttf__stack_push_int32    (TTF* font, TTF_int32  val);
static TTF_uint32  ttf__stack_pop_uint32    (TTF* font);
static TTF_int32   ttf__stack_pop_int32     (TTF* font);
static void        ttf__stack_clear         (TTF* font);
static TTF_uint32  ttf__get_num_vals_to_push(TTF_uint8 ins); /* For PUSHB and PUSHW */


/* ----------------------------- */
/* Instruction Stream Operations */
/* ----------------------------- */
typedef struct {
    TTF_uint8* bytes;
    TTF_uint32 off;
} TTF_Ins_Stream;

static void      ttf__ins_stream_init(TTF_Ins_Stream* stream, TTF_uint8* bytes);
static TTF_uint8 ttf__ins_stream_next(TTF_Ins_Stream* stream);
static void      ttf__ins_stream_skip(TTF_Ins_Stream* stream, TTF_uint32 count);


/* --------------------- */
/* Instruction Execution */
/* --------------------- */
#define TTF_SCALAR_VERSION 35

enum {
    TTF_ADD       = 0x60,
    TTF_CALL      = 0x2B,
    TTF_CINDEX    = 0x25,
    TTF_DELTAC1   = 0x73,
    TTF_DELTAC2   = 0x74,
    TTF_DELTAC3   = 0x75,
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
    TTF_IP        = 0x39,
    TTF_IUP       = 0x30,
    TTF_IUP_MAX   = 0x31,
    TTF_LOOPCALL  = 0x2A,
    TTF_LT        = 0x50,
    TTF_MDAP      = 0x2E,
    TTF_MDAP_MAX  = 0x2F,
    TTF_MDRP      = 0xC0,
    TTF_MDRP_MAX  = 0xDF,
    TTF_MINDEX    = 0x26,
    TTF_MIRP      = 0xE0,
    TTF_MIRP_MAX  = 0xFF,
    TTF_MPPEM     = 0x4B,
    TTF_MUL       = 0x63,
    TTF_NPUSHB    = 0x40,
    TTF_NPUSHW    = 0x41,
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
    TTF_RTG       = 0x18,
    TTF_SCANCTRL  = 0x85,
    TTF_SCVTCI    = 0x1D,
    TTF_SDB       = 0x5E,
    TTF_SDS       = 0x5F,
    TTF_SRP0      = 0x10,
    TTF_SRP1      = 0x11,
    TTF_SRP2      = 0x12,
    TTF_SVTCA     = 0x00,
    TTF_SVTCA_MAX = 0x01,
    TTF_SWAP      = 0x23,
    TTF_WCVTF     = 0x70,
    TTF_WCVTP     = 0x44,
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

static void ttf__ADD     (TTF* font);
static void ttf__CALL    (TTF* font);
static void ttf__DELTAC1 (TTF* font);
static void ttf__DELTAC2 (TTF* font);
static void ttf__DELTAC3 (TTF* font);
static void ttf__DELTAC  (TTF* font, TTF_uint8 range);
static void ttf__CINDEX  (TTF* font);
static void ttf__DUP     (TTF* font);
static void ttf__EQ      (TTF* font);
static void ttf__FDEF    (TTF* font, TTF_Ins_Stream* stream);
static void ttf__GETINFO (TTF* font);
static void ttf__GPV     (TTF* font);
static void ttf__GTEQ    (TTF* font);
static void ttf__IDEF    (TTF* font, TTF_Ins_Stream* stream);
static void ttf__IF      (TTF* font, TTF_Ins_Stream* stream);
static void ttf__IP      (TTF* font);
static void ttf__IUP     (TTF* font, TTF_uint8 ins);
static void ttf__LOOPCALL(TTF* font);
static void ttf__LT      (TTF* font);
static void ttf__MDAP    (TTF* font, TTF_uint8 ins);
static void ttf__MDRP    (TTF* font, TTF_uint8 ins);
static void ttf__MINDEX  (TTF* font);
static void ttf__MIRP    (TTF* font, TTF_uint8 ins);
static void ttf__MPPEM   (TTF* font);
static void ttf__MUL     (TTF* font);
static void ttf__NPUSHB  (TTF* font, TTF_Ins_Stream* stream);
static void ttf__NPUSHW  (TTF* font, TTF_Ins_Stream* stream);
static void ttf__POP     (TTF* font);
static void ttf__PUSHB   (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void ttf__PUSHW   (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void ttf__RCVT    (TTF* font);
static void ttf__RDTG    (TTF* font);
static void ttf__ROLL    (TTF* font);
static void ttf__ROUND   (TTF* font, TTF_uint8 ins);
static void ttf__RTG     (TTF* font);
static void ttf__SCANCTRL(TTF* font);
static void ttf__SCVTCI  (TTF* font);
static void ttf__SDB     (TTF* font);
static void ttf__SDS     (TTF* font);
static void ttf__SRP0    (TTF* font);
static void ttf__SRP1    (TTF* font);
static void ttf__SRP2    (TTF* font);
static void ttf__SVTCA   (TTF* font, TTF_uint8 ins);
static void ttf__SWAP    (TTF* font);
static void ttf__WCVTF   (TTF* font);
static void ttf__WCVTP   (TTF* font);

static void        ttf__reset_graphics_state     (TTF* font);
static void        ttf__call_func                (TTF* font, TTF_uint32 funcId, TTF_uint32 times);
static TTF_uint8   ttf__jump_to_else_or_eif      (TTF_Ins_Stream* stream);
static TTF_F26Dot6 ttf__round                    (TTF* font, TTF_F26Dot6 v);
static void        ttf__move_point               (TTF* font, TTF_F26Dot6_V2* point, TTF_F26Dot6 amount);
static TTF_F26Dot6 ttf__apply_single_width_cut_in(TTF* font, TTF_F26Dot6 value);
static TTF_F26Dot6 ttf__apply_min_dist           (TTF* font, TTF_F26Dot6 value);
static void        ttf__IUP_interpolate_or_shift (TTF_Zone* zone1, TTF_Touch_Flag touchFlag, TTF_uint16 startPointIdx, TTF_uint16 endPointIdx, TTF_uint16 touch0, TTF_uint16 touch1);


/* ------- */
/* Utility */
/* ------- */
#define ttf__get_Offset16(data)       ttf__get_uint16(data)
#define ttf__get_Offset32(data)       ttf__get_uint32(data)
#define ttf__get_Version16Dot16(data) ttf__get_uint32(data)

static TTF_uint16 ttf__get_uint16(TTF_uint8* data);
static TTF_uint32 ttf__get_uint32(TTF_uint8* data);
static TTF_int16  ttf__get_int16 (TTF_uint8* data);
static void       ttf__max_min   (TTF_int32* max, TTF_int32* min, TTF_int32 a, TTF_int32 b);
static TTF_uint16 ttf__get_upem  (TTF* font);


/* ---------------------- */
/* Fixed-point operations */
/* ---------------------- */
static TTF_int64 ttf__rounded_div     (TTF_int64 a, TTF_int64 b);
static TTF_int32 ttf__rounded_div_pow2(TTF_int64 a, TTF_int64 b);
static TTF_int32 ttf__fix_mul         (TTF_int32 a, TTF_int32 b, TTF_uint8 shift);
static TTF_int32 ttf__fix_div         (TTF_int32 a, TTF_int32 b, TTF_uint8 aShift, TTF_uint8 bShift);
static void      ttf__fix_v2_add      (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result);
static void      ttf__fix_v2_sub      (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result);
static void      ttf__fix_v2_mul      (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result, TTF_uint8 shift);
static void      ttf__fix_v2_div      (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result, TTF_uint8 aShift, TTF_uint8 bShift);
static TTF_int32 ttf__fix_v2_dot      (TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_uint8 shift);
static TTF_int32 ttf__fix_v2_sub_dot(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* c, TTF_uint8 shift);
static void      ttf__fix_v2_scale    (TTF_Fix_V2* v, TTF_int32 scale, TTF_uint8 shift);


#define TTF_DEBUG

#ifdef TTF_DEBUG
    #define TTF_PRINT_INS()            printf("%s\n", __func__ + 5)
    #define TTF_PRINT_UNKNOWN_INS(ins) printf("Unknown instruction: %#X\n", ins)
    #define TTF_PRINT_PROGRAM(program) printf("\n--- %s ---\n", program)
#else
    #define TTF_PRINT_INS()           
    #define TTF_PRINT_UNKNOWN_INS(ins)
    #define TTF_PRINT_PROGRAM(program)
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

    if (font->hasHinting) {
        if (!ttf__alloc_mem_for_ins_processing(font)) {
            goto init_failure;
        }

        ttf__reset_graphics_state(font);
        ttf__execute_font_program(font);
    }

    return TTF_TRUE;

init_failure:
    ttf_free(font);
    return TTF_FALSE;
}

TTF_bool ttf_instance_init(TTF* font, TTF_Instance* instance, TTF_uint32 ppem) {
    // Scale is 10.22 since upem already has a scale factor of 1
    instance->scale         = ttf__rounded_div((TTF_int64)ppem << 22, ttf__get_upem(font));
    instance->ppem          = ppem;
    instance->rotated       = TTF_FALSE;
    instance->stretched     = TTF_FALSE;
    instance->cvtIsOutdated = TTF_TRUE;

    if (font->hasHinting) {
        size_t cvtSize = font->cvt.size / sizeof(TTF_FWORD) * sizeof(TTF_F26Dot6);

        instance->cvt = malloc(cvtSize);
        if (instance->cvt == NULL) {
            return TTF_FALSE;
        }
        
        // Convert default CVT values, given in FUnits, to 26.6 fixed point 
        // pixel units
        TTF_uint32 idx = 0;
        TTF_uint8* cvt = font->data + font->cvt.off;

        for (TTF_uint32 off = 0; off < font->cvt.size; off += 2) {
            TTF_int32 funits = ttf__get_int16(cvt + off);
            instance->cvt[idx++] = ttf__fix_mul(funits << 6, instance->scale, 22);
        }
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

    if (font->hasHinting) {
        if (instance->cvtIsOutdated) {
            ttf__execute_cv_program(font);
            font->instance->cvtIsOutdated = TTF_FALSE;
        }
    }
}

TTF_bool ttf_render_glyph(TTF* font, TTF_Image* image, TTF_uint32 cp) {
    // TODO
    assert(0);
    return TTF_FALSE;
}

TTF_bool ttf_render_glyph_to_existing_image(TTF* font, TTF_Image* image, TTF_uint32 cp, TTF_uint32 x, TTF_uint32 y) {
    assert(font->instance != NULL);

    font->glyph.idx         = ttf__get_glyph_index(font, cp);
    font->glyph.glyfBlock   = ttf__get_glyf_data_block(font);
    font->glyph.numContours = ttf__get_num_glyph_contours(font);
    font->glyph.numPoints   = ttf__get_num_glyph_points(font);

    if (font->hasHinting) {
        if (!ttf__execute_glyph_program(font)) {
            return TTF_FALSE;
        }
    }
    else {
        if (!ttf__extract_glyph_points(font)) {
            return TTF_FALSE;
        }
    }

    // TODO: do stuff

    if (font->hasHinting) {
        ttf__zone_free(font->glyph.zones + 1);
    }
    else {
        free(font->glyph.points);
    }

    return TTF_TRUE;
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
static TTF_uint32 ttf__get_glyph_index(TTF* font, TTF_uint32 cp) {
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


/* --------------- */
/* glyf Operations */
/* --------------- */
static TTF_uint8* ttf__get_glyf_data_block(TTF* font) {
    TTF_int16 version = ttf__get_int16(font->data + font->head.off + 50);
        
    TTF_Offset32 blockOff  = 
        version == 0 ? 
        ttf__get_Offset16(font->data + font->loca.off + (2 * font->glyph.idx)) * 2 :
        ttf__get_Offset32(font->data + font->loca.off + (4 * font->glyph.idx));
    
    return font->data + font->glyf.off + blockOff;
}

static TTF_int16 ttf__get_num_glyph_contours(TTF* font) {
    return ttf__get_int16(font->glyph.glyfBlock);
}

static TTF_int32 ttf__get_num_glyph_points(TTF* font) {
    return 1 + ttf__get_uint16(font->glyph.glyfBlock + 8 + 2 * font->glyph.numContours);
}

static TTF_bool ttf__extract_glyph_points(TTF* font) {
    TTF_uint32 pointOff = 0;
    TTF_V2*    points;

    if (font->hasHinting) {
        // Add 4 to the number of points because there are 4 "phantom points"
        if (!ttf__zone_init(font->glyph.zones + 1, font->glyph.numPoints + 4)) {
            return TTF_FALSE;
        }
        
        points = font->glyph.zones[1].org;
    }
    else {
        font->glyph.points = malloc(sizeof(TTF_V2) * font->glyph.numPoints);
        if (font->glyph.points == NULL) {
            return TTF_FALSE;
        }

        points = font->glyph.points;
    }


    // Get the glyph points from the glyf table data
    {
        TTF_uint32 flagsSize = 0;
        TTF_uint32 xDataSize = 0;
        TTF_V2     absPos    = { 0 };

        TTF_uint8* flagData, *xData, *yData;

        flagData  = font->glyph.glyfBlock + (10 + 2 * font->glyph.numContours);
        flagData += 2 + ttf__get_uint16(flagData);
        
        for (TTF_uint32 i = 0; i < font->glyph.numPoints;) {
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

        while (pointOff < font->glyph.numPoints) {
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

                points[pointOff].x          = absPos.x + xOff;
                points[pointOff].y          = absPos.y + yOff;
                points[pointOff].touchFlags = TTF_UNTOUCHED;
                points[pointOff].isOnCurve  = flags & TTF_GLYF_ON_CURVE_POINT;
                absPos                      = points[pointOff];

                pointOff++;
                flagsReps--;
            }
        }
    }


    if (!font->hasHinting) {
        ttf__scale_glyph_points(font, points, points, font->glyph.numPoints);
        return TTF_TRUE;
    }


    // Get the phantom points
    {
        TTF_int16  xMin        = ttf__get_int16(font->glyph.glyfBlock + 2);
        TTF_int16  yMax        = ttf__get_int16(font->glyph.glyfBlock + 8);
        TTF_uint16 numHMetrics = ttf__get_uint16(font->data + font->hhea.off + 48);

        TTF_uint16 advanceWidth;
        TTF_int32  advanceHeight; 
        TTF_int16  leftSideBearing;
        TTF_int32  topSideBearing;

        if (font->glyph.idx < numHMetrics) {
            TTF_uint8* hMetricData = font->data + font->hmtx.off + 4 * font->glyph.idx;
            advanceWidth    = ttf__get_uint16(hMetricData);
            leftSideBearing = ttf__get_int16(hMetricData + 2);
        }
        else {
            // TODO: Test this
            assert(0);

            TTF_uint32 off         = 4 * numHMetrics + 2 * (numHMetrics - font->glyph.idx);
            TTF_uint8* hMetricData = font->data + font->hmtx.off + off;

            advanceWidth    = 0;
            leftSideBearing = ttf__get_int16(hMetricData);
        }

        if (font->vmtx.exists) {
            // TODO: Get vertical phantom points from vmtx
            assert(0);
        }
        else  {
            TTF_int16 defaultAscender, defaultDescender;

            if (font->OS2.exists) {
                defaultAscender  = ttf__get_int16(font->data + font->OS2.off + 68);
                defaultDescender = ttf__get_int16(font->data + font->OS2.off + 70);
            }
            else {
                // TODO: Get defaultAscender and defaultDescender from hhea
                assert(0);
            }

            advanceHeight  = defaultAscender - defaultDescender;
            topSideBearing = defaultAscender;
        }

        points[pointOff].x = xMin - leftSideBearing;
        points[pointOff].y = 0;

        pointOff += 1;
        points[pointOff].x = points[pointOff - 1].x + advanceWidth;
        points[pointOff].y = 0;

        pointOff += 1;
        points[pointOff].y = yMax + topSideBearing;
        points[pointOff].x = 0;

        pointOff += 1;
        points[pointOff].y = points[pointOff - 1].y - advanceHeight;
        points[pointOff].x = 0;

        font->glyph.zones[1].count = pointOff + 1;
        assert(font->glyph.zones[1].count == font->glyph.zones[1].cap);
    }


    ttf__scale_glyph_points(
        font, points, font->glyph.zones[1].orgScaled, font->glyph.zones[1].count);
    

    // Set the current zone points, phantom points 2 and 4 are rounded
    memcpy(
        font->glyph.zones[1].cur, 
        font->glyph.zones[1].orgScaled, 
        sizeof(TTF_Fix_V2) * font->glyph.zones[1].count);

    font->glyph.zones[1].cur[pointOff - 2].x = 
        ttf__round(font, font->glyph.zones[1].cur[pointOff - 3].x);

    font->glyph.zones[1].cur[pointOff].y = 
        ttf__round(font, font->glyph.zones[1].cur[pointOff].y);

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

static void ttf__scale_glyph_points(TTF* font, TTF_V2* points, TTF_F26Dot6_V2* scaled, TTF_uint32 numPoints) {
    for (TTF_uint32 i = 0; i < numPoints; i++) {
        scaled[i].x          = ttf__fix_mul(points[i].x << 6, font->instance->scale, 22);
        scaled[i].y          = ttf__fix_mul(points[i].y << 6, font->instance->scale, 22);
        scaled[i].touchFlags = TTF_UNTOUCHED;
        scaled[i].isOnCurve  = points[i].isOnCurve;
    }
}


/* --------------- */
/* Zone Operations */
/* --------------- */
static TTF_bool ttf__zone_init(TTF_Zone* zone, TTF_uint32 cap) {
    size_t size = cap * sizeof(TTF_V2);
    
    zone->mem = malloc(3 * size);
    if (zone->mem == NULL) {
        return TTF_FALSE;
    }
    zone->org       = (TTF_Fix_V2*)(zone->mem);
    zone->orgScaled = (TTF_Fix_V2*)(zone->mem + size);
    zone->cur       = (TTF_Fix_V2*)(zone->mem + size + size);
    zone->count     = 0;
    zone->cap       = cap;

    return TTF_TRUE;
}

static void ttf__zone_free(TTF_Zone* zone) {
    free(zone->mem);
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

static TTF_uint32 ttf__get_num_vals_to_push(TTF_uint8 ins) {
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

static void ttf__ins_stream_skip(TTF_Ins_Stream* stream, TTF_uint32 count) {
    stream->off += count;
}


/* --------------------- */
/* Instruction Execution */
/* --------------------- */
static void ttf__execute_font_program(TTF* font) {
    TTF_PRINT_PROGRAM("Font Program");

    TTF_Ins_Stream stream;
    ttf__ins_stream_init(&stream, font->data + font->fpgm.off);
    
    while (stream.off < font->fpgm.size) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        assert(ins == TTF_PUSHB || ins == TTF_FDEF || ins == TTF_IDEF);
        ttf__execute_ins(font, &stream, ins);
    }
}

static void ttf__execute_cv_program(TTF* font) {
    TTF_PRINT_PROGRAM("CV Program");

    TTF_Ins_Stream stream;
    ttf__ins_stream_init(&stream, font->data + font->prep.off);
    
    while (stream.off < font->prep.size) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        ttf__execute_ins(font, &stream, ins);
    }
}

static TTF_bool ttf__execute_glyph_program(TTF* font) {
    TTF_PRINT_PROGRAM("Glyph Program");

    ttf__reset_graphics_state(font);
    ttf__stack_clear(font);

    // TODO: handle composite glyphs
    assert(font->glyph.numContours >= 0);

    TTF_uint16 maxTwilightPoints = ttf__get_uint16(font->data + font->maxp.off + 16);
    TTF_uint32 insOff            = 10 + font->glyph.numContours * 2;
    TTF_uint16 numIns            = ttf__get_int16(font->glyph.glyfBlock + insOff);

    TTF_Ins_Stream stream;
    ttf__ins_stream_init(&stream, font->glyph.glyfBlock + insOff + 2);

    if (!ttf__zone_init(font->glyph.zones, maxTwilightPoints)) {
        return TTF_FALSE;
    }
    
    if (!ttf__extract_glyph_points(font)) {
        ttf__zone_free(font->glyph.zones);
        return TTF_FALSE;
    }

    while (stream.off < numIns) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        ttf__execute_ins(font, &stream, ins);
    }

    free(font->glyph.zones[0].mem);
    return TTF_TRUE;
}

static void ttf__execute_ins(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    switch (ins) {
        case TTF_ADD:
            ttf__ADD(font);
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
        case TTF_IP:
            ttf__IP(font);
            return;
        case TTF_LOOPCALL:
            ttf__LOOPCALL(font);
            return;
        case TTF_LT:
            ttf__LT(font);
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
        case TTF_NPUSHB:
            ttf__NPUSHB(font, stream);
            return;
        case TTF_NPUSHW:
            ttf__NPUSHW(font, stream);
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
        case TTF_RTG:
            ttf__RTG(font);
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
        case TTF_SRP0:
            ttf__SRP0(font);
            return;
        case TTF_SRP1:
            ttf__SRP1(font);
            return;
        case TTF_SRP2:
            ttf__SRP2(font);
            return;
        case TTF_SWAP:
            ttf__SWAP(font);
            return;
        case TTF_WCVTF:
            ttf__WCVTF(font);
            return;
        case TTF_WCVTP:
            ttf__WCVTP(font);
            return;
    }

    if (ins >= TTF_IUP && ins <= TTF_IUP_MAX) {
        ttf__IUP(font, ins);
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

    TTF_PRINT_UNKNOWN_INS(ins);
    assert(0);
}

static void ttf__ADD(TTF* font) {
    TTF_PRINT_INS();
    TTF_F26Dot6 n1 = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 n2 = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, n1 + n2);
}

static void ttf__CALL(TTF* font) {
    TTF_PRINT_INS();
    ttf__call_func(font, ttf__stack_pop_uint32(font), 1);
}

static void ttf__CINDEX(TTF* font) {
    TTF_PRINT_INS();
    TTF_uint32 idx = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, font->stack.frames[idx].uValue);
}

static void ttf__DELTAC1(TTF* font) {
    TTF_PRINT_INS();
    ttf__DELTAC(font, 0);
}

static void ttf__DELTAC2(TTF* font) {
    TTF_PRINT_INS();
    ttf__DELTAC(font, 16);
}

static void ttf__DELTAC3(TTF* font) {
    TTF_PRINT_INS();
    ttf__DELTAC(font, 32);
}

static void ttf__DELTAC(TTF* font, TTF_uint8 range) {
    TTF_PRINT_INS();

    TTF_uint32 n = ttf__stack_pop_uint32(font);

    while (n > 0) {
        TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
        TTF_uint32 exc    = ttf__stack_pop_uint32(font);
        TTF_uint32 ppem   = ((exc & 0xF0) >> 4) + font->gState.deltaBase + range;

        if (font->instance->ppem == ppem) {
            TTF_int8 numSteps = (exc & 0xF) - 8;
            if (numSteps > 0) {
                numSteps++;
            }
            
            // numSteps * stepVal is 26.6 since numSteps already has a scale
            // factor of 1

            TTF_int32 stepVal = 1 << (6 - font->gState.deltaShift);
            font->instance->cvt[cvtIdx] += numSteps * stepVal;
            printf("\tEqual\n");
        }
        else {
            printf("\tNot equal\n");
        }

        n--;
    }
}

static void ttf__DUP(TTF* font) {
    TTF_PRINT_INS();
    TTF_uint32 e = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e);
    ttf__stack_push_uint32(font, e);
}

static void ttf__EQ(TTF* font) {
    TTF_PRINT_INS();
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 == e2 ? 1 : 0);
}

static void ttf__FDEF(TTF* font, TTF_Ins_Stream* stream) {
    TTF_PRINT_INS();

    assert(font->funcArray.count < font->funcArray.cap);

    TTF_uint32 funcId = ttf__stack_pop_uint32(font);
    assert(funcId < font->funcArray.cap);

    font->funcArray.funcs[funcId].firstIns = stream->bytes + stream->off;
    font->funcArray.count++;

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

    TTF_PRINT_INS();

    TTF_uint32 result   = 0;
    TTF_uint32 selector = ttf__stack_pop_uint32(font);

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
        // result |= 0x1000;
    }

    printf("\t%d\n", result);
    ttf__stack_push_uint32(font, result);
}

static void ttf__GPV(TTF* font) {
    // TODO
    TTF_PRINT_INS();
    assert(0);
}

static void ttf__GTEQ(TTF* font) {
    TTF_PRINT_INS();
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 >= e2 ? 1 : 0);
}

static void ttf__IDEF(TTF* font, TTF_Ins_Stream* stream) {
    // TODO
    TTF_PRINT_INS();
    assert(0);
}

static void ttf__IF(TTF* font, TTF_Ins_Stream* stream) {
    TTF_PRINT_INS();

    if (ttf__stack_pop_uint32(font) == 0) {
        if (ttf__jump_to_else_or_eif(stream) == TTF_EIF) {
            // Condition is false and there is no else instruction
            return;
        }
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
    TTF_PRINT_INS();
    assert(font->gState.rp1 < font->glyph.zones[1].count);
    assert(font->gState.rp2 < font->glyph.zones[1].count);

    TTF_F26Dot6_V2* rp1Cur = font->gState.zp0->cur + font->gState.rp1;
    TTF_F26Dot6_V2* rp2Cur = font->gState.zp1->cur + font->gState.rp2;

    TTF_bool isTwilightZone = 
        (font->gState.zp0 == &font->glyph.zones[0]) ||
        (font->gState.zp1 == &font->glyph.zones[0]) ||
        (font->gState.zp2 == &font->glyph.zones[0]);

    TTF_F26Dot6_V2* rp1Org, *rp2Org;

    if (isTwilightZone) {
        // Twilight zone doesn't have unscaled coordinates
        rp1Org = font->gState.zp0->orgScaled + font->gState.rp1;
        rp2Org = font->gState.zp1->orgScaled + font->gState.rp2;
    }
    else {
        // Use unscaled coordinates for more precision, then scale the results
        rp1Org = font->gState.zp0->org + font->gState.rp1;
        rp2Org = font->gState.zp1->org + font->gState.rp2;
    }

    TTF_F26Dot6 totalDistCur = ttf__fix_v2_sub_dot(rp2Cur, rp1Cur, &font->gState.projVec, 14);
    TTF_F26Dot6 totalDistOrg = ttf__fix_v2_sub_dot(rp2Org, rp1Org, &font->gState.dualProjVec, 14);

    if (!isTwilightZone) {
        ttf__fix_mul(totalDistOrg, font->instance->scale, 22);
    }

    for (TTF_uint32 i = 0; i < font->gState.loop; i++) {
        TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);
        assert(pointIdx < font->gState.zp2->count);

        TTF_F26Dot6_V2* pointCur = font->gState.zp2->cur + pointIdx;
        TTF_F26Dot6_V2* pointOrg = font->gState.zp2->org + pointIdx;

        TTF_F26Dot6 distCur = ttf__fix_v2_sub_dot(pointCur, rp1Cur, &font->gState.projVec, 14);
        TTF_F26Dot6 distOrg = ttf__fix_v2_sub_dot(pointOrg, rp1Org, &font->gState.dualProjVec, 14);

        if (!isTwilightZone) {
            ttf__fix_mul(distOrg, font->instance->scale, 22);
        }

        // Scale distOrg by however many times bigger totalDistCur is than
        // totalDistOrg. 
        //
        // This ensures D(p,rp1)/D(p',rp1') = D(p,rp2)/D(p',rp2') holds true.
        TTF_F26Dot6 distNew = 
            ttf__fix_div(ttf__fix_mul(distOrg, totalDistCur, 6), totalDistOrg, 6, 6);

        ttf__move_point(font, pointCur, distNew - distCur);

        printf("\t(%d, %d)\n", pointCur->x, pointCur->y);
    }
}

static void ttf__IUP(TTF* font, TTF_uint8 ins) {
    TTF_PRINT_INS();

    // Applying IUP to zone0 is an error
    assert(font->gState.zp2 == &font->glyph.zones[1]);

    TTF_int16 numContours = ttf__get_int16(font->glyph.glyfBlock);    
    if (numContours == 0) {
        return;
    }

    // TODO: How are composite glyphs handled?
    assert(numContours > 0);

    TTF_Touch_Flag  touchFlag = ins & 0x1 ? TTF_TOUCH_X : TTF_TOUCH_Y;
    TTF_F26Dot6_V2* points    = font->glyph.zones[1].cur;
    TTF_uint32      pointIdx  = 0;

    for (TTF_uint16 contourIdx = 0; contourIdx < numContours; contourIdx++) {
        TTF_uint16 startPointIdx = pointIdx;
        TTF_uint16 endPointIdx   = ttf__get_uint16(font->glyph.glyfBlock + 10 + 2 * contourIdx);
        TTF_uint16 touch0        = 0;
        TTF_bool   findingTouch1 = TTF_FALSE;

        while (pointIdx <= endPointIdx) {
            if (points[pointIdx].touchFlags & touchFlag) {
                if (findingTouch1) {
                    ttf__IUP_interpolate_or_shift(
                        &font->glyph.zones[1], touchFlag, startPointIdx, endPointIdx, touch0, 
                        pointIdx);

                    if (pointIdx == endPointIdx || points[pointIdx + 1].touchFlags & touchFlag) {
                        findingTouch1 = TTF_FALSE;
                    }
                    else {
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
                if (points[i].touchFlags & touchFlag) {
                    ttf__IUP_interpolate_or_shift(
                        &font->glyph.zones[1], touchFlag, startPointIdx, endPointIdx, touch0, i);
                    break;
                }
            }
        }
    }
}

static void ttf__LOOPCALL(TTF* font) {
    TTF_PRINT_INS();
    TTF_uint32 funcId = ttf__stack_pop_uint32(font);
    TTF_uint32 times  = ttf__stack_pop_uint32(font);
    ttf__call_func(font, funcId, times);
}

static void ttf__LT(TTF* font) {
    TTF_PRINT_INS();
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 < e2 ? 1 : 0);
}

static void ttf__MDAP(TTF* font, TTF_uint8 ins) {
    TTF_PRINT_INS();

    TTF_uint32      pointIdx = ttf__stack_pop_uint32(font);
    TTF_F26Dot6_V2* point    = font->gState.zp0->cur + pointIdx;

    if (ins & 0x1) {
        // Move the point to its rounded position
        TTF_F26Dot6 curDist     = ttf__fix_v2_dot(point, &font->gState.projVec, 14);
        TTF_F26Dot6 roundedDist = ttf__round(font, curDist);
        ttf__move_point(font, point, roundedDist - curDist);
    }
    else {
        // Don't move the point, just mark it as touched
        point->touchFlags |= font->gState.touchFlags;
    }

    font->gState.rp0 = pointIdx;
    font->gState.rp1 = pointIdx;

    printf("\t(%d, %d)\n", point->x, point->y);
}

static void ttf__MDRP(TTF* font, TTF_uint8 ins) {
    TTF_PRINT_INS();

    assert(font->gState.rp0 < font->gState.zp0->count);

    TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);
    assert(pointIdx < font->gState.zp1->count);

    TTF_F26Dot6_V2* rp0Cur   = font->gState.zp0->cur + font->gState.rp0;
    TTF_F26Dot6_V2* pointCur = font->gState.zp1->cur + pointIdx;

    TTF_bool isTwilightZone = 
        (font->gState.zp0 == &font->glyph.zones[0]) || 
        (font->gState.zp1 == &font->glyph.zones[0]);

    TTF_F26Dot6_V2* rp0Org, *pointOrg;

    if (isTwilightZone) {
        // Twilight zone doesn't have unscaled coordinates
        rp0Org   = font->gState.zp0->orgScaled + font->gState.rp0;
        pointOrg = font->gState.zp1->orgScaled + pointIdx;
    }
    else {
        // Use unscaled coordinates for more precision, then scale the results
        rp0Org   = font->gState.zp0->org + font->gState.rp0;
        pointOrg = font->gState.zp1->org + pointIdx;
    }

    TTF_F26Dot6 distCur = ttf__fix_v2_sub_dot(pointCur, rp0Cur, &font->gState.projVec, 14);
    TTF_F26Dot6 distOrg = ttf__fix_v2_sub_dot(pointOrg, rp0Org, &font->gState.dualProjVec, 14);

    if (!isTwilightZone) {
        distOrg = ttf__fix_mul(distOrg, font->instance->scale, 22);
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

    ttf__move_point(font, pointCur, distOrg - distCur);

    printf("\t(%d, %d)\n", pointCur->x, pointCur->y);
}

static void ttf__MINDEX(TTF* font) {
    TTF_PRINT_INS();
    
    TTF_uint32       idx   = ttf__stack_pop_uint32(font);
    TTF_Stack_Frame* ptr   = font->stack.frames + idx;
    TTF_Stack_Frame  frame = *ptr;
    TTF_uint32       size  = sizeof(TTF_Stack_Frame) * (font->stack.count - idx - 1);
    memmove(ptr, ptr, size);
    
    font->stack.count--;
    ttf__stack_push_uint32(font, frame.uValue);
}

static void ttf__MIRP(TTF* font, TTF_uint8 ins) {
    // Note: There is a lot of undocumented stuff involving this instruction

    TTF_PRINT_INS();

    TTF_uint32 cvtIdx   = ttf__stack_pop_uint32(font);
    TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);

    assert(cvtIdx   < font->cvt.size / sizeof(TTF_FWORD));
    assert(pointIdx < font->gState.zp1->count);

    TTF_F26Dot6 cvtVal = font->instance->cvt[cvtIdx];
    cvtVal = ttf__apply_single_width_cut_in(font, cvtVal);

    TTF_F26Dot6_V2* rp0Org = font->gState.zp0->orgScaled + font->gState.rp0;
    TTF_F26Dot6_V2* rp0Cur = font->gState.zp0->cur       + font->gState.rp0;

    TTF_F26Dot6_V2* pointOrg = font->gState.zp1->orgScaled + pointIdx;
    TTF_F26Dot6_V2* pointCur = font->gState.zp1->cur + pointIdx;

    if (font->gState.zp1 == &font->glyph.zones[0]) {
        // Madness
        pointOrg->x = rp0Org->x + ttf__fix_mul(cvtVal, font->gState.freedomVec.x, 14);
        pointOrg->y = rp0Org->y + ttf__fix_mul(cvtVal, font->gState.freedomVec.y, 14);
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

    ttf__move_point(font, pointCur, distNew - distCur);

    font->gState.rp1 = font->gState.rp0;
    font->gState.rp2 = pointIdx;

    if (ins & 0x10) {
        font->gState.rp0 = pointIdx;
    }

    printf("\t(%d, %d)\n", pointCur->x, pointCur->y);
}

static void ttf__MPPEM(TTF* font) {
    TTF_PRINT_INS();
    ttf__stack_push_uint32(font, font->instance->ppem);
    printf("\t%d\n", font->instance->ppem);
}

static void ttf__MUL(TTF* font) {
    TTF_PRINT_INS();
    TTF_F26Dot6 n1 = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 n2 = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, ttf__fix_mul(n1, n2, 6));
}

static void ttf__NPUSHB(TTF* font, TTF_Ins_Stream* stream) {
    // TODO
    assert(0);
}

static void ttf__NPUSHW(TTF* font, TTF_Ins_Stream* stream) {
    // TODO
    assert(0);
}

static void ttf__POP(TTF* font) {
    TTF_PRINT_INS();
    ttf__stack_pop_uint32(font);
}

static void ttf__PUSHB(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    TTF_PRINT_INS();

    TTF_uint8 n = ttf__get_num_vals_to_push(ins);

    do {
        TTF_uint8 byte = ttf__ins_stream_next(stream);
        ttf__stack_push_uint32(font, byte);
    } while (--n);
}

static void ttf__PUSHW(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    TTF_PRINT_INS();

    TTF_uint32 n = ttf__get_num_vals_to_push(ins);

    do {
        TTF_uint8 ms  = ttf__ins_stream_next(stream);
        TTF_uint8 ls  = ttf__ins_stream_next(stream);
        TTF_int32 val = (ms << 8) | ls;
        ttf__stack_push_int32(font, val);
    } while (--n);
}

static void ttf__RCVT(TTF* font) {
    TTF_PRINT_INS();
    
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    ttf__stack_push_F26Dot6(font, font->instance->cvt[cvtIdx]);
}

static void ttf__RDTG(TTF* font) {
    TTF_PRINT_INS();
    font->gState.roundState = TTF_ROUND_DOWN_TO_GRID;
}

static void ttf__ROLL(TTF* font) {
    TTF_PRINT_INS();
    TTF_uint32 a = ttf__stack_pop_uint32(font);
    TTF_uint32 b = ttf__stack_pop_uint32(font);
    TTF_uint32 c = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, b);
    ttf__stack_push_uint32(font, a);
    ttf__stack_push_uint32(font, c);
}

static void ttf__ROUND(TTF* font, TTF_uint8 ins) {
    TTF_PRINT_INS();
    TTF_F26Dot6 dist = ttf__stack_pop_F26Dot6(font);
    dist = ttf__round(font, dist);
    printf("\t%d\n", dist);
    ttf__stack_push_F26Dot6(font, dist);
}

static void ttf__RTG(TTF* font) {
    TTF_PRINT_INS();
    font->gState.roundState = TTF_ROUND_TO_GRID;
}

static void ttf__SCANCTRL(TTF* font) {
    TTF_PRINT_INS();

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
            if (font->instance->ppem <= thresh) {
                font->gState.scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x200) {
            if (font->instance->rotated) {
                font->gState.scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x400) {
            if (font->instance->stretched) {
                font->gState.scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x800) {
            if (thresh > font->instance->ppem) {
                font->gState.scanControl = TTF_FALSE;
            }
        }

        if (flags & 0x1000) {
            if (!font->instance->rotated) {
                font->gState.scanControl = TTF_FALSE;
            }
        }

        if (flags & 0x2000) {
            if (!font->instance->stretched) {
                font->gState.scanControl = TTF_FALSE;
            }
        }
    }
}

static void ttf__SCVTCI(TTF* font) {
    TTF_PRINT_INS();
    font->gState.controlValueCutIn = ttf__stack_pop_F26Dot6(font);
}

static void ttf__SDB(TTF* font) {
    TTF_PRINT_INS();
    font->gState.deltaBase = ttf__stack_pop_uint32(font);
}

static void ttf__SDS(TTF* font) {
    TTF_PRINT_INS();
    font->gState.deltaShift = ttf__stack_pop_uint32(font);
}

static void ttf__SRP0(TTF* font) {
    TTF_PRINT_INS();
    font->gState.rp0 = ttf__stack_pop_uint32(font);
}

static void ttf__SRP1(TTF* font) {
    TTF_PRINT_INS();
    font->gState.rp1 = ttf__stack_pop_uint32(font);
}

static void ttf__SRP2(TTF* font) {
    TTF_PRINT_INS();
    font->gState.rp2 = ttf__stack_pop_uint32(font);
}

static void ttf__SVTCA(TTF* font, TTF_uint8 ins) {
    TTF_PRINT_INS();

    if (ins & 0x1) {
        font->gState.freedomVec.x = 1 << 14;
        font->gState.freedomVec.y = 0;
        font->gState.touchFlags   = TTF_TOUCH_X;
    }
    else {
        font->gState.freedomVec.x = 0;
        font->gState.freedomVec.y = 1 << 14;
        font->gState.touchFlags   = TTF_TOUCH_Y;
    }

    font->gState.projVec     = font->gState.freedomVec;
    font->gState.dualProjVec = font->gState.freedomVec;
}

static void ttf__SWAP(TTF* font) {
    TTF_PRINT_INS();
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1);
    ttf__stack_push_uint32(font, e2);
}

static void ttf__WCVTF(TTF* font) {
    TTF_PRINT_INS();

    TTF_uint32 funits = ttf__stack_pop_uint32(font);
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    font->instance->cvt[cvtIdx] = ttf__fix_mul(funits << 6, font->instance->scale, 22);
}

static void ttf__WCVTP(TTF* font) {
    TTF_PRINT_INS();

    TTF_uint32 pixels = ttf__stack_pop_uint32(font);
    
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    font->instance->cvt[cvtIdx] = pixels;
}

static void ttf__reset_graphics_state(TTF* font) {
    font->gState.autoFlip          = TTF_TRUE;
    font->gState.controlValueCutIn = 68;
    font->gState.deltaBase         = 9;
    font->gState.deltaShift        = 3;
    font->gState.dualProjVec.x     = 1 << 14;
    font->gState.dualProjVec.y     = 0;
    font->gState.freedomVec.x      = 1 << 14;
    font->gState.freedomVec.y      = 0;
    font->gState.loop              = 1;
    font->gState.minDist           = 1 << 6;
    font->gState.projVec.x         = 1 << 14;
    font->gState.projVec.y         = 0;
    font->gState.rp0               = 0;
    font->gState.rp1               = 0;
    font->gState.rp2               = 0;
    font->gState.roundState        = TTF_ROUND_TO_GRID;
    font->gState.scanControl       = TTF_FALSE;
    font->gState.singleWidthCutIn  = 0;
    font->gState.singleWidthValue  = 0;
    font->gState.touchFlags        = TTF_TOUCH_X;
    font->gState.zp0               = font->glyph.zones + 1;
    font->gState.zp1               = font->glyph.zones + 1;
    font->gState.zp2               = font->glyph.zones + 1;
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

        switch (ins) {
            case TTF_PUSHB:
                ttf__ins_stream_skip(stream, ttf__get_num_vals_to_push(ins));
                break;
            case TTF_PUSHW:
                ttf__ins_stream_skip(stream, 2 * ttf__get_num_vals_to_push(ins));
                break;
            case TTF_NPUSHB:
                ttf__ins_stream_skip(stream, ttf__ins_stream_next(stream));
                break;
            case TTF_NPUSHW:
                ttf__ins_stream_skip(stream, 2 * ttf__ins_stream_next(stream));
                break;
            case TTF_IF:
                numNested++;
                break;
            default:
                if (numNested == 0){
                    if (ins == TTF_EIF || ins == TTF_ELSE) {
                        return ins;
                    }
                }
                else if (ins == TTF_EIF) {
                    numNested--;
                }
                break;
        }
    }

    assert(0);
    return 0;
}

static TTF_F26Dot6 ttf__round(TTF* font, TTF_F26Dot6 v) {
    // TODO: No idea how to apply "engine compensation" described in the spec

    switch (font->gState.roundState) {
        case TTF_ROUND_TO_HALF_GRID:
            // TODO
            assert(0);
            break;
        case TTF_ROUND_TO_GRID:
            return ((v & 0x20) << 1) + (v & 0xFFFFFFC0);
        case TTF_ROUND_TO_DOUBLE_GRID:
            // TODO
            assert(0);
            break;
        case TTF_ROUND_DOWN_TO_GRID:
            return v & 0xFFFFFFC0;
        case TTF_ROUND_UP_TO_GRID:
            // TODO
            assert(0);
            break;
        case TTF_ROUND_OFF:
            // TODO
            assert(0);
            break;
    }
    assert(0);
    return 0;
}

static void ttf__move_point(TTF* font, TTF_F26Dot6_V2* point, TTF_F26Dot6 amount) {
    point->x += ttf__fix_mul(amount, font->gState.freedomVec.x, 14);
    point->y += ttf__fix_mul(amount, font->gState.freedomVec.y, 14);
    point->touchFlags |= font->gState.touchFlags;
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

static void ttf__IUP_interpolate_or_shift(TTF_Zone* zone1, TTF_Touch_Flag touchFlag, TTF_uint16 startPointIdx, TTF_uint16 endPointIdx, TTF_uint16 touch0, TTF_uint16 touch1) {
    #define TTF_IUP_INTERPOLATE(coord)\
        TTF_F26Dot6 totalDistCur = zone1->cur[touch1].coord - zone1->cur[touch0].coord;        \
        TTF_F26Dot6 totalDistOrg = zone1->org[touch1].coord - zone1->org[touch0].coord;        \
        TTF_F26Dot6 orgDist      = zone1->org[i].coord      - zone1->org[touch0].coord;        \
                                                                                               \
        TTF_F10Dot22 scale   = ttf__rounded_div((TTF_int64)totalDistCur << 16, totalDistOrg);  \
        TTF_F26Dot6  newDist = ttf__fix_mul(orgDist << 6, scale, 22);                          \
        zone1->cur[i].coord = zone1->cur[touch0].coord + newDist;                              \
                                                                                               \
        printf("\tInterp %3d: %5d\n", i, zone1->cur[i].coord);

    #define TTF_IUP_SHIFT(coord)\
        TTF_int32 diff0 = labs(zone1->org[touch0].coord - zone1->org[i].coord);        \
        TTF_int32 diff1 = labs(zone1->org[touch1].coord - zone1->org[i].coord);        \
                                                                                       \
        if (diff0 < diff1) {                                                           \
            TTF_int32 diff = zone1->cur[touch0].coord - zone1->orgScaled[touch0].coord;\
            zone1->cur[i].coord += diff;                                               \
        }                                                                              \
        else {                                                                         \
            TTF_int32 diff = zone1->cur[touch1].coord - zone1->orgScaled[touch1].coord;\
            zone1->cur[i].coord += diff;                                               \
        }                                                                              \
        printf("\tShift %3d: %5d\n", i, zone1->cur[i].coord);

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
        ttf__max_min(&coord1, &coord0, zone1->org[touch0].x, zone1->org[touch1].x);
    }
    else {
        ttf__max_min(&coord1, &coord0, zone1->org[touch0].y, zone1->org[touch1].y);
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

static void ttf__max_min(TTF_int32* max, TTF_int32* min, TTF_int32 a, TTF_int32 b) {
    if (a > b) {
        *max = a;
        *min = b;
    }
    else {
        *max = b;
        *min = a;
    }
}

static TTF_uint16 ttf__get_upem(TTF* font) {
    return ttf__get_uint16(font->data + font->head.off + 18);
}


/* ---------------------- */
/* Fixed-point operations */
/* ---------------------- */
static TTF_int64 ttf__rounded_div(TTF_int64 a, TTF_int64 b) {
    // https://stackoverflow.com/a/18067292
    return ((a < 0) ^ (b < 0) ? (a - b / 2) / b : (a + b / 2) / b);
}

static TTF_int32 ttf__rounded_div_pow2(TTF_int64 a, TTF_int64 shift) {
    // The proof:
    // round(x/y) = floor(x/y + 0.5) = floor((x + y/2)/y) = shift-of-n(x + 2^(n-1)) 
    //
    // https://en.wikipedia.org/wiki/Fixed-point_arithmetic
    return (a + (1 << (shift - 1))) >> shift;
}

static TTF_int32 ttf__fix_mul(TTF_int32 a, TTF_int32 b, TTF_uint8 shift) {
    return ttf__rounded_div_pow2((TTF_uint64)a * (TTF_uint64)b, shift);
}

static TTF_int32 ttf__fix_div(TTF_int32 a, TTF_int32 b, TTF_uint8 aShift, TTF_uint8 bShift) {
    TTF_int64 q = ttf__rounded_div((TTF_int64)a << 32, b);
    return ttf__rounded_div_pow2(q, 32 + aShift - (bShift << 1));
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
    result->x = ttf__fix_mul(a->x, b->x, shift);
    result->y = ttf__fix_mul(a->y, b->y, shift);
}

static void ttf__fix_v2_div(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* result, TTF_uint8 aShift, TTF_uint8 bShift) {
    result->x = ttf__fix_div(a->x, b->x, aShift, bShift);
    result->y = ttf__fix_div(a->y, b->y, aShift, bShift);
}

static TTF_int32 ttf__fix_v2_dot(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_uint8 shift) {
    return ttf__fix_mul(a->x, b->x, shift) + ttf__fix_mul(a->y, b->y, shift);
}

/* dot(a - b, c) */
static TTF_int32 ttf__fix_v2_sub_dot(TTF_Fix_V2* a, TTF_Fix_V2* b, TTF_Fix_V2* c, TTF_uint8 shift) {
    TTF_Fix_V2 diff;
    ttf__fix_v2_sub(a, b, &diff);
    return ttf__fix_v2_dot(&diff, c, shift);
}

static void ttf__fix_v2_scale(TTF_Fix_V2* v, TTF_int32 scale, TTF_uint8 shift) {
    v->x = ttf__fix_mul(v->x, scale, shift);
    v->y = ttf__fix_mul(v->y, scale, shift);
}

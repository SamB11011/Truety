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
    TTF_LOOPCALL  = 0x2A,
    TTF_LT        = 0x50,
    TTF_MINDEX    = 0x26,
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

typedef struct {
    TTF_uint8* bytes;
    TTF_uint32       off;
} TTF_Ins_Stream;


#define TTF_DEBUG

#ifdef TTF_DEBUG
    #define TTF_PRINT(S)                        printf(S)
    #define TTF_PRINTF(S, ...)                  printf(S, __VA_ARGS__)
    #define TTF_PRINT_CVT(instance, numEntries) ttf__print_cvt(instance, numEntries)

    static void ttf__print_cvt(TTF_Instance* instance, TTF_uint32 numEntries) {
        printf("\n-- CVT --\n");
        for (TTF_uint32 i = 0; i < numEntries; i++) {
            printf("%d) %d\n", i, instance->cvt[i]);
        }
    }
#else
    #define TTF_PRINT(S) 
    #define TTF_PRINTF(S)
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
static TTF_uint8* ttf__get_glyf_data_block (TTF* font, TTF_uint32 glyphIdx);
static TTF_bool   ttf__extract_glyph_points(TTF* font, TTF_uint8* glyphData, TTF_uint32 glyphIdx, TTF_int16 numContours);
static TTF_int32  ttf__get_next_glyph_offset(TTF_uint8** data, TTF_uint8 dualFlag, TTF_uint8 shortFlag, TTF_uint8 flags);


/* ---------------------- */
/* Instruction Processing */
/* ---------------------- */
#define ttf__stack_push_F2Dot14(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_push_F26Dot6(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_pop_F2Dot14(font)       ttf__stack_pop_int32(font)
#define ttf__stack_pop_F26Dot6(font)       ttf__stack_pop_int32(font)

#define TTF_GET_NUM_VALS_TO_PUSH(ins) (1 + (ins & 0x7)) /* For PUSHB and PUSHW */

static void       ttf__execute_font_program (TTF* font);
static void       ttf__execute_cv_program   (TTF* font);
static TTF_bool   ttf__execute_glyph_program(TTF* font, TTF_uint32 idx);
static void       ttf__execute_ins          (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void       ttf__ADD                  (TTF* font);
static void       ttf__CALL                 (TTF* font);
static void       ttf__DELTAC1              (TTF* font);
static void       ttf__DELTAC2              (TTF* font);
static void       ttf__DELTAC3              (TTF* font);
static void       ttf__DELTAC               (TTF* font, TTF_uint8 range);
static void       ttf__CINDEX               (TTF* font);
static void       ttf__DUP                  (TTF* font);
static void       ttf__EQ                   (TTF* font);
static void       ttf__FDEF                 (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__GETINFO              (TTF* font);
static void       ttf__GPV                  (TTF* font);
static void       ttf__GTEQ                 (TTF* font);
static void       ttf__IDEF                 (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__IF                   (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__IP                   (TTF* font);
static void       ttf__LOOPCALL             (TTF* font);
static void       ttf__LT                   (TTF* font);
static void       ttf__MINDEX               (TTF* font);
static void       ttf__MPPEM                (TTF* font);
static void       ttf__MUL                  (TTF* font);
static void       ttf__NPUSHB               (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__NPUSHW               (TTF* font, TTF_Ins_Stream* stream);
static void       ttf__POP                  (TTF* font);
static void       ttf__PUSHB                (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void       ttf__PUSHW                (TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins);
static void       ttf__RCVT                 (TTF* font);
static void       ttf__RDTG                 (TTF* font);
static void       ttf__ROLL                 (TTF* font);
static void       ttf__ROUND                (TTF* font, TTF_uint8 ins);
static void       ttf__RTG                  (TTF* font);
static void       ttf__SCANCTRL             (TTF* font);
static void       ttf__SCVTCI               (TTF* font);
static void       ttf__SDB                  (TTF* font);
static void       ttf__SDS                  (TTF* font);
static void       ttf__SRP0                 (TTF* font);
static void       ttf__SRP1                 (TTF* font);
static void       ttf__SRP2                 (TTF* font);
static void       ttf__SVTCA                (TTF* font, TTF_uint8 ins);
static void       ttf__SWAP                 (TTF* font);
static void       ttf__WCVTF                (TTF* font);
static void       ttf__WCVTP                (TTF* font);
static void       ttf__ins_stream_init      (TTF_Ins_Stream* stream, TTF_uint8* bytes);
static TTF_uint8  ttf__ins_stream_next      (TTF_Ins_Stream* stream);
static void       ttf__stack_push_uint32    (TTF* font, TTF_uint32 val);
static void       ttf__stack_push_int32     (TTF* font, TTF_int32  val);
static TTF_uint32 ttf__stack_pop_uint32     (TTF* font);
static TTF_int32  ttf__stack_pop_int32      (TTF* font);
static void       ttf__call_func            (TTF* font, TTF_uint32 funcId, TTF_uint32 times);
static TTF_uint8  ttf__jump_to_else_or_eif  (TTF_Ins_Stream* stream);


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
    // Scale is 10.22 since upem already has a scale factor of 1
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

        instance->cvt = (TTF_F26Dot6*)       (instance->mem);
        instance->gs  = (TTF_Graphics_State*)(instance->mem + cvtSize);

        // Set default graphics state values
        instance->gs->controlValueCutIn = 68;
        instance->gs->deltaBase         = 9;
        instance->gs->deltaShift        = 3;
        instance->gs->freedomVec.x      = 1 << 14;
        instance->gs->freedomVec.y      = 0;
        instance->gs->projVec.x         = 1 << 14;
        instance->gs->projVec.y         = 0;
        instance->gs->rp0               = 0;
        instance->gs->rp1               = 0;
        instance->gs->rp2               = 0;
        instance->gs->roundState        = TTF_ROUND_TO_GRID;
        instance->gs->scanControl       = TTF_FALSE;
        
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

    TTF_uint32 idx = ttf__get_char_glyph_index(font, cp);
    
    if (font->cvt.exists) {
        // The font has hinting
        if (!ttf__execute_glyph_program(font, idx)) {
            return TTF_FALSE;
        }
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
        else if (!font->prep.exists && !memcmp(tag, "vmtx", 4)) {
            table = &font->vmtx;
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
static TTF_uint8* ttf__get_glyf_data_block(TTF* font, TTF_uint32 glyphIdx) {
    TTF_int16 version = ttf__get_int16(font->data + font->head.off + 50);
        
    TTF_Offset32 blockOff  = 
        version == 0 ? 
        ttf__get_Offset16(font->data + font->loca.off + (2 * glyphIdx)) * 2 :
        ttf__get_Offset32(font->data + font->loca.off + (4 * glyphIdx));
    
    return font->data + font->glyf.off + blockOff;
}

static TTF_bool ttf__extract_glyph_points(TTF* font, TTF_uint8* glyphData, TTF_uint32 glyphIdx, TTF_int16 numContours) {
    TTF_uint32 numRegularPoints = 1 + ttf__get_uint16(glyphData + 8 + 2 * numContours);
    
    // The last four points are phantom points that are not a part of the glyph
    // outline
    font->instance->pointArray.count = 4 + numRegularPoints;

    font->instance->pointArray.points = 
        malloc(font->instance->pointArray.count * sizeof(TTF_Vec2));

    if (font->instance->pointArray.points == NULL) {
        return TTF_FALSE;
    }


    TTF_uint8* flagData, *xData, *yData;
    {
        flagData  = glyphData + (10 + 2 * numContours);
        flagData += 2 + ttf__get_uint16(flagData);
        
        TTF_uint32 flagsSize = 0;
        TTF_uint32 xDataSize = 0;
        
        for (TTF_uint32 i = 0; i < numRegularPoints;) {
            TTF_uint8 flags = flagData[flagsSize];
            TTF_uint8 xSize = flags & TTF_GLYF_X_SHORT_VECTOR ? 1 : flags & TTF_GLYF_X_DUAL ? 0 : 2;
            
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
    }


    TTF_uint32 off    = 0;
    TTF_Vec2   absPos = { 0 };

    while (off < numRegularPoints) {
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
            TTF_int32 xOff = ttf__get_next_glyph_offset(
                &xData, TTF_GLYF_X_DUAL, TTF_GLYF_X_SHORT_VECTOR, flags);

            TTF_int32 yOff = ttf__get_next_glyph_offset(
                &yData, TTF_GLYF_Y_DUAL, TTF_GLYF_Y_SHORT_VECTOR, flags);

            font->instance->pointArray.points[off].x = absPos.x + xOff;
            font->instance->pointArray.points[off].y = absPos.y + yOff;

            absPos = font->instance->pointArray.points[off];
            off++;
            flagsReps--;
        }
    }


    // Add the four phantom points
    // TODO: Round the phantom points using the current round_state

    font->instance->pointArray.points[off    ].y = 0;
    font->instance->pointArray.points[off + 1].y = 0;
    font->instance->pointArray.points[off + 2].x = 0;
    font->instance->pointArray.points[off + 3].x = 0;

    {
        TTF_uint8* hMetricData     = font->data + font->hmtx.off + 4 * glyphIdx;
        TTF_uint16 advanceWidth    = ttf__get_uint16(hMetricData);
        TTF_int16  leftSideBearing = ttf__get_int16(hMetricData + 2);
        TTF_int16  xMin            = ttf__get_int16(glyphData + 2);

        font->instance->pointArray.points[off].x = xMin - leftSideBearing;
        
        font->instance->pointArray.points[off + 1].x = 
            font->instance->pointArray.points[off].x + advanceWidth;
    }

    TTF_int16 yMax = ttf__get_int16(glyphData + 8);
    TTF_int32 advanceHeight, topSideBearing;

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
        topSideBearing = defaultAscender - yMax;
    }

    font->instance->pointArray.points[off + 2].y = yMax + topSideBearing;

    font->instance->pointArray.points[off + 3].y = 
        font->instance->pointArray.points[off + 2].y - advanceHeight;

    return TTF_TRUE;
}

static TTF_int32 ttf__get_next_glyph_offset(TTF_uint8** data, TTF_uint8 dualFlag, TTF_uint8 shortFlag, TTF_uint8 flags) {
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

static TTF_bool ttf__execute_glyph_program(TTF* font, TTF_uint32 glyphIdx) {
    TTF_PRINT("\n-- Glyph Program --\n");

    TTF_uint8* glyphData   = ttf__get_glyf_data_block(font, glyphIdx);
    TTF_int16  numContours = ttf__get_int16(glyphData);

    // TODO: handle composite glyphs
    assert(numContours >= 0);
    
    if (!ttf__extract_glyph_points(font, glyphData, glyphIdx, numContours)) {
        return TTF_FALSE;
    }

    TTF_uint32 insOff    = 10 + numContours * 2;
    TTF_uint16 insLen = ttf__get_int16(glyphData + insOff);

    TTF_Ins_Stream stream;
    ttf__ins_stream_init(&stream, glyphData + insOff + 2);

    while (stream.off < insLen) {
        TTF_uint8 ins = ttf__ins_stream_next(&stream);
        ttf__execute_ins(font, &stream, ins);
    }

    free(font->instance->pointArray.points);
    font->instance->pointArray.points = NULL;
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

    if (ins >= TTF_PUSHB && ins <= TTF_PUSHB_MAX) {
        ttf__PUSHB(font, stream, ins);
        return;
    }
    else if (ins >= TTF_PUSHW && ins <= TTF_PUSHW_MAX) {
        ttf__PUSHW(font, stream, ins);
        return;
    }
    else if (ins >= TTF_SVTCA && ins <= TTF_SVTCA_MAX) {
        ttf__SVTCA(font, ins);
        return;
    }
    else if (ins >= TTF_ROUND && ins <= TTF_ROUND_MAX) {
        ttf__ROUND(font, ins);
        return;
    }

    TTF_PRINTF("Unknown instruction: %#x\n", ins);
    assert(0);
}

static void ttf__ADD(TTF* font) {
    TTF_PRINT("ADD\n");
    TTF_F26Dot6 n1 = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 n2 = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, n1 + n2);
}

static void ttf__CALL(TTF* font) {
    TTF_PRINT("CALL\n");
    ttf__call_func(font, ttf__stack_pop_uint32(font), 1);
}

static void ttf__CINDEX(TTF* font) {
    TTF_PRINT("CINDEX\n");
    TTF_uint32 idx = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, font->stack.frames[idx].uValue);
}

static void ttf__DELTAC1(TTF* font) {
    TTF_PRINT("DELTAC1\n");
    ttf__DELTAC(font, 0);
}

static void ttf__DELTAC2(TTF* font) {
    TTF_PRINT("DELTAC2\n");
    ttf__DELTAC(font, 16);
}

static void ttf__DELTAC3(TTF* font) {
    TTF_PRINT("DELTAC3\n");
    ttf__DELTAC(font, 32);
}

static void ttf__DELTAC(TTF* font, TTF_uint8 range) {
    TTF_uint32 n = ttf__stack_pop_uint32(font);

    while (n > 0) {
        TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
        TTF_uint32 exc    = ttf__stack_pop_uint32(font);
        TTF_uint32 ppem   = ((exc & 0xF0) >> 4) + font->instance->gs->deltaBase + range;

        if (font->instance->ppem == ppem) {
            TTF_int8 numSteps = (exc & 0xF) - 8;
            if (numSteps > 0) {
                numSteps++;
            }
            
            // numSteps * stepVal is 26.6 since numSteps already has a scale
            // factor of 1

            TTF_int32 stepVal = 1 << (6 - font->instance->gs->deltaShift);
            font->instance->cvt[cvtIdx] += numSteps * stepVal;
            TTF_PRINTF("\tcvt[%d] = %d\n", cvtIdx, font->instance->cvt[cvtIdx]);
        }
        else {
            TTF_PRINT("\tppems not equal\n");
        }

        n--;
    }
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

static void ttf__IP(TTF* font) {
    assert(font->instance->gs->rp1 < font->instance->pointArray.count);
    TTF_PRINT("IP\n");
    
    // TODO: use graphics state 'loop' variable
    
    TTF_uint32 pointIdx = ttf__stack_pop_uint32(font);


    assert(0);
}

static void ttf__LOOPCALL(TTF* font) {
    TTF_PRINT("LOOPCALL\n");
    TTF_uint32 funcId = ttf__stack_pop_uint32(font);
    TTF_uint32 times  = ttf__stack_pop_uint32(font);
    ttf__call_func(font, funcId, times);
}

static void ttf__LT(TTF* font) {
    TTF_PRINT("LT\n");
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 < e2 ? 1 : 0);
}

static void ttf__MINDEX(TTF* font) {
    TTF_PRINT("MINDEX\n");
    
    TTF_uint32       idx   = ttf__stack_pop_uint32(font);
    TTF_Stack_Frame* ptr   = font->stack.frames + idx;
    TTF_Stack_Frame  frame = *ptr;
    TTF_uint32       size  = sizeof(TTF_Stack_Frame) * (font->stack.count - idx - 1);
    memmove(ptr, ptr, size);

    font->stack.count--;

    ttf__stack_push_uint32(font, frame.uValue);
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
    // TODO
    assert(0);
}

static void ttf__NPUSHW(TTF* font, TTF_Ins_Stream* stream) {
    // TODO
    assert(0);
}

static void ttf__POP(TTF* font) {
    TTF_PRINT("POP\n");
    ttf__stack_pop_uint32(font);
}

static void ttf__PUSHB(TTF* font, TTF_Ins_Stream* stream, TTF_uint8 ins) {
    TTF_uint8 n = TTF_GET_NUM_VALS_TO_PUSH(ins);
    TTF_PRINTF("PUSHB %d\n\t", n);

    do {
        TTF_uint8 byte = ttf__ins_stream_next(stream);
        ttf__stack_push_uint32(font, byte);
        TTF_PRINTF("%d ", byte);
    } while (--n);

    TTF_PRINT("\n");
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

static void ttf__RCVT(TTF* font) {
    TTF_PRINT("RCVT\n");
    
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);
    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    ttf__stack_push_F26Dot6(font, font->instance->cvt[cvtIdx]);
}

static void ttf__RDTG(TTF* font) {
    TTF_PRINT("RDTG\n");
    font->instance->gs->roundState = TTF_ROUND_DOWN_TO_GRID;
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

static void ttf__ROUND(TTF* font, TTF_uint8 ins) {
    // TODO: No idea how to apply "engine compensation" described in the spec
    
    TTF_F26Dot6 dist = ttf__stack_pop_F26Dot6(font);

    TTF_PRINTF("ROUND (%d => ", dist);

    switch (font->instance->gs->roundState) {
        case TTF_ROUND_TO_HALF_GRID:
            // TODO
            assert(0);
            break;
        case TTF_ROUND_TO_GRID:
            dist = ((dist & 0x20) << 1) + (dist & 0xFFFFFFC0);
            break;
        case TTF_ROUND_TO_DOUBLE_GRID:
            // TODO
            assert(0);
            break;
        case TTF_ROUND_DOWN_TO_GRID:
            dist &= 0xFFFFFFC0;
            break;
        case TTF_ROUND_UP_TO_GRID:
            // TODO
            assert(0);
            break;
        case TTF_ROUND_OFF:
            // TODO
            assert(0);
            break;
        default:
            assert(0);
            break;
    }

    ttf__stack_push_F26Dot6(font, dist);

    TTF_PRINTF("%d)\n", dist);
}

static void ttf__RTG(TTF* font) {
    TTF_PRINT("RTG\n");
    font->instance->gs->roundState = TTF_ROUND_TO_GRID;
}

static void ttf__SCANCTRL(TTF* font) {
    TTF_uint16 flags  = ttf__stack_pop_uint32(font);
    TTF_uint8  thresh = flags & 0xFF;
    
    if (thresh == 0xFF) {
        font->instance->gs->scanControl = TTF_TRUE;
    }
    else if (thresh == 0x0) {
        font->instance->gs->scanControl = TTF_FALSE;
    }
    else {
        if (flags & 0x100) {
            if (font->instance->ppem <= thresh) {
                font->instance->gs->scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x200) {
            if (font->instance->rotated) {
                font->instance->gs->scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x400) {
            if (font->instance->stretched) {
                font->instance->gs->scanControl = TTF_TRUE;
            }
        }

        if (flags & 0x800) {
            if (thresh > font->instance->ppem) {
                font->instance->gs->scanControl = TTF_FALSE;
            }
        }

        if (flags & 0x1000) {
            if (!font->instance->rotated) {
                font->instance->gs->scanControl = TTF_FALSE;
            }
        }

        if (flags & 0x2000) {
            if (!font->instance->stretched) {
                font->instance->gs->scanControl = TTF_FALSE;
            }
        }
    }

    TTF_PRINTF("SCANCTRL (scan_control=%d)\n", font->instance->gs->scanControl);
}

static void ttf__SCVTCI(TTF* font) {
    TTF_PRINT("SCVTCI\n");
    font->instance->gs->controlValueCutIn = ttf__stack_pop_F26Dot6(font);
}

static void ttf__SDB(TTF* font) {
    font->instance->gs->deltaBase = ttf__stack_pop_uint32(font);
    TTF_PRINTF("SDB %d\n", font->instance->gs->deltaBase);
}

static void ttf__SDS(TTF* font) {
    font->instance->gs->deltaShift = ttf__stack_pop_uint32(font);
    TTF_PRINTF("SDS %d\n", font->instance->gs->deltaShift);
}

static void ttf__SRP0(TTF* font) {
    TTF_PRINT("SRP0\n");
    font->instance->gs->rp0 = ttf__stack_pop_uint32(font);
}

static void ttf__SRP1(TTF* font) {
    TTF_PRINT("SRP1\n");
    font->instance->gs->rp1 = ttf__stack_pop_uint32(font);
}

static void ttf__SRP2(TTF* font) {
    TTF_PRINT("SRP2\n");
    font->instance->gs->rp2 = ttf__stack_pop_uint32(font);
}

static void ttf__SVTCA(TTF* font, TTF_uint8 ins) {
    TTF_PRINT("SVTCA\n");

    if (ins == 0x1) {
        font->instance->gs->freedomVec.x = 1 << 14;
        font->instance->gs->freedomVec.y = 0;
        font->instance->gs->projVec      = font->instance->gs->freedomVec;
    }
    else {
        font->instance->gs->freedomVec.x = 0;
        font->instance->gs->freedomVec.y = 1 << 14;
        font->instance->gs->projVec      = font->instance->gs->freedomVec;
    }
}

static void ttf__SWAP(TTF* font) {
    TTF_PRINT("SWAP\n");
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1);
    ttf__stack_push_uint32(font, e2);
}

static void ttf__WCVTF(TTF* font) {
    TTF_uint32 funits = ttf__stack_pop_uint32(font);
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);

    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    font->instance->cvt[cvtIdx] = ttf__fix_mult(funits << 6, font->instance->scale, 22);

    TTF_PRINTF("WCVTF (cvt[%d] = %d)\n", cvtIdx, font->instance->cvt[cvtIdx]);
}

static void ttf__WCVTP(TTF* font) {
    TTF_uint32 pixels = ttf__stack_pop_uint32(font);
    TTF_uint32 cvtIdx = ttf__stack_pop_uint32(font);

    assert(cvtIdx < font->cvt.size / sizeof(TTF_FWORD));

    font->instance->cvt[cvtIdx] = pixels;

    TTF_PRINTF("WCVTP (cvt[%d] = %d)\n", cvtIdx, font->instance->cvt[cvtIdx]);
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

static void ttf__call_func(TTF* font, TTF_uint32 funcId, TTF_uint32 times) {
    assert(funcId < font->funcArray.count);

    while (times > 0) {
        TTF_PRINTF("\tCalling func %d\n", funcId);

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

        TTF_PRINT("\n");
    }
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

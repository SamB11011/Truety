#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "ttf.h"

#define TTF_DEBUG

#ifdef TTF_DEBUG
    #define TTF_PRINT(S)       printf(S)
    #define TTF_PRINTF(S, ...) printf(S, __VA_ARGS__)
#else
    #define TTF_PRINT(S)  assert(1)
    #define TTF_PRINTF(S) assert(1)
#endif


typedef enum {
    TTF_ON_CURVE_POINT = 0x01,
    TTF_X_SHORT_VECTOR = 0x02,
    TTF_Y_SHORT_VECTOR = 0x04,
    TTF_REPEAT_FLAG    = 0x08,
    TTF_X_DUAL         = 0x10, // X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR
    TTF_Y_DUAL         = 0x20, // Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR
    TTF_OVERLAP_SIMPLE = 0x40,
    TTF_RESERVED       = 0x80,
} TTF_Simple_Glyph_Flag;

typedef enum {
    TTF_NPUSHB    = 0x40,
    TTF_PUSHB     = 0xB0,
    TTF_PUSHB_ABC = 0xB7,
    TTF_FDEF      = 0x2C,
    TTF_IDEF      = 0x89,
    TTF_GPV       = 0x0C,
    TTF_CALL      = 0x2B,
    TTF_ENDF      = 0x2D,
    TTF_GETINFO   = 0x88,
    TTF_DUP       = 0x20,
    TTF_ROLL      = 0x8A,
    TTF_GTEQ      = 0x53,
    TTF_IF        = 0x58,
    TTF_ELSE      = 0x1B,
    TTF_EIF       = 0x59,
} TTF_Insruction;

typedef struct {
    const TTF_uint8* bytes;
    TTF_uint32       off;
} TTF_IStream;


/* ---------------- */
/* Helper functions */
/* ---------------- */
#define ttf__read_Offset16(data)       ttf__read_uint16(data)
#define ttf__read_Offset32(data)       ttf__read_uint32(data)
#define ttf__read_Version16Dot16(data) ttf__read_uint32(data)

static TTF_uint16 ttf__read_uint16(const TTF_uint8* data);
static TTF_uint32 ttf__read_uint32(const TTF_uint8* data);
static TTF_int16  ttf__read_int16 (const TTF_uint8* data);


/* ------------------------- */
/* Table directory functions */
/* ------------------------- */
static void table_dir__extract_table_info(TTF* font);


/* --------------------- */
/* Instruction functions */
/* --------------------- */
static void      ttf__istream_init       (TTF_IStream* stream, const TTF_uint8* bytes);
static TTF_uint8 ttf__istream_next       (TTF_IStream* stream);
static void      ttf__execute_ins        (TTF* font, TTF_IStream* stream, TTF_uint8 ins);
static void      ttf__PUSHB              (TTF* font, TTF_IStream* stream, TTF_uint8 ins);
static void      ttf__FDEF               (TTF* font, TTF_IStream* stream);
static void      ttf__IDEF               (TTF* font, TTF_IStream* stream);
static void      ttf__GPV                (TTF* font);
static void      ttf__CALL               (TTF* font);
static void      ttf__GETINFO            (TTF* font);
static void      ttf__DUP                (TTF* font);
static void      ttf__ROLL               (TTF* font);
static void      ttf__GTEQ               (TTF* font);
static void      ttf__IF                 (TTF* font, TTF_IStream* stream);
static TTF_uint8 ttf__jump_to_else_or_eif(TTF_IStream* stream);


/* --------------- */
/* Stack functions */
/* --------------- */
#define ttf__stack_push_F2Dot14(font, val) ttf__stack_push_int32(font, val)

static void       ttf__stack_push_uint32(TTF* font, TTF_uint32 val);
static void       ttf__stack_push_int32 (TTF* font, TTF_int32  val);
static TTF_uint32 ttf__stack_pop_uint32 (TTF* font);
static TTF_int32  ttf__stack_pop_int32  (TTF* font);


/* -------------- */
/* cmap functions */
/* -------------- */
static int        cmap__extract_encoding             (TTF* font);
static int        cmap__encoding_format_is_supported (TTF_uint16 format);
static TTF_uint32 cmap__get_char_glyph_index         (const TTF* font, TTF_uint32 c);
static TTF_uint16 cmap__get_char_glyph_index_format_4(const TTF_uint8* subtable, TTF_uint32 c);


/* -------------- */
/* fpgm functions */
/* -------------- */
static void fpgm__execute     (TTF* font);
static int  fpgm__ins_is_valid(TTF_uint8 ins);


/* -------------- */
/* glyf functions */
/* -------------- */
static void glyf__extract_glyph_coordinate_data          (const TTF* font, TTF_uint32 glyphIndex);
static void glyf__extract_simple_glyph_coordinate_data   (const TTF_uint8* data);
static void glyf__extract_composite_glyph_coordinate_data(const TTF_uint8* data);


/* -------------- */
/* loca functions */
/* -------------- */
static TTF_Offset32 loca__get_glyf_block_off(const TTF* font, TTF_uint32 glyphIndex);


/* -------------- */
/* prep functions */
/* -------------- */
static void prep__execute(TTF* font);


int ttf_init(TTF* font, const char* path) {
    memset(font, 0, sizeof(TTF));

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    
    fseek(f, 0, SEEK_END);
    font->size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    font->data = calloc(font->size, 1);
    if (font->data == NULL) {
        fclose(f);
        return 0;
    }

    fread(font->data, 1, font->size, f);
    fclose(f);

    // sfntVersion is 0x00010000 for fonts that contain TrueType outlines
    assert(ttf__read_uint32(font->data) == 0x00010000);

    table_dir__extract_table_info(font);

    {
        const TTF_uint8* maxp = font->data + font->maxp.off;

        TTF_uint32 stackFrameSize    = sizeof(TTF_Stack_Frame)    * ttf__read_uint16(maxp + 24);
        TTF_uint32 funcsSize         = sizeof(TTF_Func)           * ttf__read_uint16(maxp + 20);
        TTF_uint32 graphicsStateSize = sizeof(TTF_Graphics_State);

        font->mem = calloc(1, stackFrameSize + funcsSize + graphicsStateSize);
        if (font->mem == NULL) {
            free(font->data);
            return 0;
        }

        font->stack.frames  = (TTF_Stack_Frame*)   (font->mem);
        font->funcs         = (TTF_Func*)          (font->mem + stackFrameSize);
        font->graphicsState = (TTF_Graphics_State*)(font->mem + stackFrameSize + funcsSize);
    }

    font->graphicsState->xProjectionVector = 1 << 14;
    font->graphicsState->yProjectionVector = 0;

    if (!cmap__extract_encoding(font)) {
        ttf_free(font);
        return 0;
    }

    fpgm__execute(font);
    prep__execute(font);

    // TTF_uint32 glyphIndex = cmap__get_char_glyph_index(font, 'e');
    // printf("glyphIndex = %d\n", (int)glyphIndex);
    // glyf__extract_glyph_coordinate_data(font, glyphIndex);

    return 1;
}

void ttf_free(TTF* font) {
    if (font) {
        free(font->data);
        free(font->mem);
    }
}

/* ---------------- */
/* Helper functions */
/* ---------------- */
static TTF_uint16 ttf__read_uint16(const TTF_uint8* data) {
    return data[0] << 8 | data[1];
}

static TTF_uint32 ttf__read_uint32(const TTF_uint8* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static TTF_int16 ttf__read_int16(const TTF_uint8* data) {
    return data[0] << 8 | data[1];
}


/* ------------------------- */
/* Table directory functions */
/* ------------------------- */
static void table_dir__extract_table_info(TTF* font) {
    TTF_uint16 numTables = ttf__read_uint16(font->data + 4);

    for (TTF_uint16 i = 0; i < numTables; i++) {
        TTF_uint8* record = font->data + (12 + 16 * i);
        TTF_Table* table  = NULL;

        TTF_uint8 tag[4];
        memcpy(tag, record, 4);

        if (!font->cmap.exists && !memcmp(tag, "cmap", 4)) {
            table = &font->cmap;
        }
        else if (!font->fpgm.exists && !memcmp(tag, "fpgm", 4)) {
            table = &font->fpgm;
        }
        else if (!font->head.exists && !memcmp(tag, "glyf", 4)) {
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
            table->exists = 1;
            table->off    = ttf__read_Offset32(record + 8);
            table->size   = ttf__read_uint32(record + 12);
        }
    }
}


/* --------------------- */
/* Instruction functions */
/* --------------------- */
static void ttf__istream_init(TTF_IStream* stream, const TTF_uint8* bytes) {
    stream->bytes = bytes;
    stream->off   = 0;
}

static TTF_uint8 ttf__istream_next(TTF_IStream* stream) {
    return stream->bytes[stream->off++];
}

static void ttf__execute_ins(TTF* font, TTF_IStream* stream, TTF_uint8 ins) {
    switch (ins) {
        case TTF_FDEF:
            ttf__FDEF(font, stream);
            return;
        case TTF_IDEF:
            ttf__IDEF(font, stream);
            return;
        case TTF_GPV:
            ttf__GPV(font);
            return;
        case TTF_CALL:
            ttf__CALL(font);
            return;
        case TTF_GETINFO:
            ttf__GETINFO(font);
            return;
        case TTF_DUP:
            ttf__DUP(font);
            return;
        case TTF_ROLL:
            ttf__ROLL(font);
            return;
        case TTF_GTEQ:
            ttf__GTEQ(font);
            return;
        case TTF_IF:
            ttf__IF(font, stream);
            return;
    }

    if (ins >= TTF_PUSHB && ins <= TTF_PUSHB_ABC) {
        ttf__PUSHB(font, stream, ins);
        return;
    }

    TTF_PRINTF("Unknown instruction: %#x\n", ins);
    assert(0);
}

// static void ttf__NPUSHB(TTF* font, TTF_IStream* stream, TTF_uint8, ins) {

// }

static void ttf__PUSHB(TTF* font, TTF_IStream* stream, TTF_uint8 ins) {
    TTF_uint8 n = 1 + (ins & 0x7);
    TTF_PRINTF("PUSHB %d\n", n);

    do {
        TTF_uint8 byte = ttf__istream_next(stream);
        ttf__stack_push_uint32(font, byte);
    } while (--n);
}

static void ttf__FDEF(TTF* font, TTF_IStream* stream) {
    TTF_uint32 funcId = ttf__stack_pop_uint32(font);
    TTF_PRINTF("FDEF %#x\n", funcId);

    font->funcs[funcId].firstIns = stream->bytes + stream->off;
    while (ttf__istream_next(stream) != TTF_ENDF);
}

static void ttf__IDEF(TTF* font, TTF_IStream* stream) {
    // TODO
    assert(0);
}

static void ttf__GPV(TTF* font) {
    ttf__stack_push_F2Dot14(font, font->graphicsState->xProjectionVector);
    ttf__stack_push_F2Dot14(font, font->graphicsState->yProjectionVector);
}

static void ttf__CALL(TTF* font) {
    // Starting with the first instruction, a call to a function executes 
    // instructions until instruction 0x2D (ENDF) is reached.
    TTF_IStream stream;
    {
        TTF_uint32 funcId = ttf__stack_pop_uint32(font);
        ttf__istream_init(&stream, font->funcs[funcId].firstIns);
        TTF_PRINTF("CALL %#X\n", funcId);
    }

    while (1) {
        TTF_uint8 ins = ttf__istream_next(&stream);
        
        if (ins == TTF_ENDF) {
            break;
        }

        ttf__execute_ins(font, &stream, ins);
    };
    
    TTF_PRINT("\n");
}

static void ttf__GETINFO(TTF* font) {
    TTF_uint32 selector = ttf__stack_pop_uint32(font);
    TTF_PRINTF("GETINFO %d\n", selector);

    if (selector & 0x1) {
        // Scalar version number
        ttf__stack_push_uint32(font, 42);
    }
    else {
        // TODO
        assert(0);
    }
}

static void ttf__DUP(TTF* font) {
    TTF_PRINT("DUP\n");
    TTF_uint32 e = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e);
    ttf__stack_push_uint32(font, e);
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

static void ttf__GTEQ(TTF* font) {
    TTF_PRINT("GTEQ\n");
    TTF_uint32 e2 = ttf__stack_pop_uint32(font);
    TTF_uint32 e1 = ttf__stack_pop_uint32(font);
    ttf__stack_push_uint32(font, e1 >= e2 ? 1 : 0);
}

static void ttf__IF(TTF* font, TTF_IStream* stream) {
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

    while (1) {
        TTF_uint8 ins = ttf__istream_next(stream);

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

static TTF_uint8 ttf__jump_to_else_or_eif(TTF_IStream* stream) {
    TTF_uint32 numNested = 0;

    while (1) {
        TTF_uint8 ins = ttf__istream_next(stream);

        if (numNested == 0){
            if (ins == TTF_EIF || ins == TTF_ELSE) {
                return ins;
            }
        }
        else if (ins == TTF_EIF) {
            numNested--;
        }
        else if (ins == TTF_IF) {
            numNested++;
        }
    }

    assert(0);
    return 0;
}


/* --------------- */
/* Stack functions */
/* --------------- */
static void ttf__stack_push_uint32(TTF* font, TTF_uint32 val) {
    font->stack.frames[font->stack.numFrames++].uValue = val;
}

static void ttf__stack_push_int32(TTF* font, TTF_int32 val) {
    font->stack.frames[font->stack.numFrames++].sValue = val;
}

static TTF_uint32 ttf__stack_pop_uint32(TTF* font) {
    assert(font->stack.numFrames > 0);
    return font->stack.frames[--font->stack.numFrames].uValue;
}

static TTF_int32 ttf__stack_pop_int32(TTF* font) {
    assert(font->stack.numFrames > 0);
    return font->stack.frames[--font->stack.numFrames].sValue;
}


/* -------------- */
/* cmap functions */
/* -------------- */
static int cmap__extract_encoding(TTF* font) {
    TTF_uint16 numTables = ttf__read_uint16(font->data + font->cmap.off + 2);
    
    for (TTF_uint16 i = 0; i < numTables; i++) {
        const TTF_uint8* data = font->data + font->cmap.off + 4 + i * 8;

        TTF_uint16 platformID = ttf__read_uint16(data);
        TTF_uint16 encodingID = ttf__read_uint16(data + 2);
        int        foundValid = 0;

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
            font->encoding.off        = ttf__read_Offset32(data + 4);

            TTF_uint8* subtable = font->data + font->cmap.off + font->encoding.off;
            TTF_uint16 format   = ttf__read_uint16(subtable);
            if (cmap__encoding_format_is_supported(format)) {
                return 1;
            }
        }
    }

    return 0;
}

static int cmap__encoding_format_is_supported(TTF_uint16 format) {
    switch (format) {
        case 4:
        case 6:
        case 8:
        case 10:
        case 12:
        case 13:
        case 14:
            return 1;
    }
    return 0;
}

static TTF_uint32 cmap__get_char_glyph_index(const TTF* font, TTF_uint32 c) {
    const TTF_uint8* subtable = font->data + font->cmap.off + font->encoding.off;

    switch (ttf__read_uint16(subtable)) {
        case 4:
            return cmap__get_char_glyph_index_format_4(subtable, c);
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

static TTF_uint16 cmap__get_char_glyph_index_format_4(const TTF_uint8* subtable, TTF_uint32 c) {
    // TODO: Not sure how these values are supposed to be used in the binary search so it will be
    //       done without them.
    //
    // TTF_uint16 searchRange   = ttf__read_uint16(data + 8);
    // TTF_uint16 entrySelector = ttf__read_uint16(data + 10);
    // TTF_uint16 rangeShift    = ttf__read_uint16(data + 12);
    
    TTF_uint16 segCount = ttf__read_uint16(subtable + 6) >> 1;
    TTF_uint16 left     = 0;
    TTF_uint16 right    = segCount - 1;

    while (left <= right) {
        TTF_uint16 mid     = (left + right) / 2;
        TTF_uint16 endCode = ttf__read_uint16(subtable + 14 + 2 * mid);

        if (endCode >= c) {
            if (mid == 0 || ttf__read_uint16(subtable + 14 + 2 * (mid - 1)) < c) {
                TTF_uint32       off            = 16 + 2 * mid;
                const TTF_uint8* idRangeOffsets = subtable + 6 * segCount + off;
                TTF_uint16       idRangeOffset  = ttf__read_uint16(idRangeOffsets);
                TTF_uint16       startCode      = ttf__read_uint16(subtable + 2 * segCount + off);

                if (startCode > c) {
                    return 0;
                }
                
                if (idRangeOffset == 0) {
                    TTF_uint16 idDelta = ttf__read_int16(subtable + 4 * segCount + off);
                    return c + idDelta;
                }

                return ttf__read_uint16(idRangeOffset + 2 * (c - startCode) + idRangeOffsets);
            }
            right = mid - 1;
        }
        else {
            left = mid + 1;
        }
    }

    return 0;
}


/* -------------- */
/* fpgm functions */
/* -------------- */
static void fpgm__execute(TTF* font) {
    TTF_PRINT("-- Font Program --\n");

    TTF_IStream stream;
    ttf__istream_init(&stream, font->data + font->fpgm.off);
    
    while (stream.off < font->fpgm.size) {
        TTF_uint8 ins = ttf__istream_next(&stream);
        assert(fpgm__ins_is_valid(ins));
        ttf__execute_ins(font, &stream, ins);
    }
}

static int fpgm__ins_is_valid(TTF_uint8 ins) {
    return ins == TTF_PUSHB || ins == TTF_FDEF || ins == TTF_IDEF;
}


/* -------------- */
/* glyf functions */
/* -------------- */
static void glyf__extract_glyph_coordinate_data(const TTF* font, TTF_uint32 glyphIndex) {
    const TTF_uint8* data = font->data + 
                            font->glyf.off + 
                            loca__get_glyf_block_off(font, glyphIndex);

    if (ttf__read_int16(data) >= 0) {
        glyf__extract_simple_glyph_coordinate_data(data);
    }
    else {
        glyf__extract_composite_glyph_coordinate_data(data);
    }
}

static void glyf__extract_simple_glyph_coordinate_data(const TTF_uint8* data) {
    #define TTF_IS_REPEATED_COORD(coord, flags) \
        (!(flags & TTF_ ## coord ## _SHORT_VECTOR) && (flags & TTF_ ## coord ## _DUAL))

    // Note: When a coordinate is repeated, the absolute value is the same, so 
    //       the offset is 0.
    #define TTF_READ_COORD_OFF(coord, flags, coordData)                             \
        ((flags & TTF_ ## coord ## _SHORT_VECTOR) ?                                 \
         (flags & TTF_ ## coord ## _DUAL ? *coordData : -(*coordData)) :            \
         (flags & TTF_ ## coord ## _DUAL ? 0          : ttf__read_int16(coordData)))\

    TTF_uint32 numContours = ttf__read_int16(data);
    TTF_uint32 numPoints   = 1 + ttf__read_uint16(data + 8 + 2 * numContours);
    TTF_uint32 flagsSize   = 0;
    TTF_uint32 xDataSize   = 0;

    data += 10 + 2 * numContours;
    data += 2 + ttf__read_uint16(data);

    for (TTF_uint32 i = 0; i < numPoints;) {
        TTF_uint8  flags     = data[flagsSize];
        TTF_uint32 flagsReps = (flags & TTF_REPEAT_FLAG) ? data[flagsSize + 1] : 1;

        for (TTF_uint32 j = 0; j < flagsReps; j++) {
            if (!TTF_IS_REPEATED_COORD(X, flags)) {
                xDataSize += (flags & TTF_X_SHORT_VECTOR) ? 1 : 2;
            }
        }

        i += flagsReps;
        flagsSize++;
    }

    const TTF_uint8* xData = data + flagsSize;
    const TTF_uint8* yData = xData + xDataSize;

    for (TTF_uint32 i = 0; i < flagsSize; i++) {
        TTF_uint8  flags     = data[i];
        TTF_uint32 flagsReps = (flags & TTF_REPEAT_FLAG) ? data[i + 1] : 1;

        for (TTF_uint32 j = 0; j < flagsReps; j++) {
            TTF_int16 x = TTF_READ_COORD_OFF(X, flags, xData);
            TTF_int16 y = TTF_READ_COORD_OFF(Y, flags, yData);

            // printf("(%d, %d)\n", (int)x, (int)y);

            if (!TTF_IS_REPEATED_COORD(X, flags)) {
                xData += (flags & TTF_X_SHORT_VECTOR) ? 1 : 2;
            }

            if (!TTF_IS_REPEATED_COORD(Y, flags)) {
                yData += (flags & TTF_Y_SHORT_VECTOR) ? 1 : 2;
            }
        }
    }

    #undef TTF_IS_REPEATED_COORD
    #undef TTF_READ_COORD_OFF
}

static void glyf__extract_composite_glyph_coordinate_data(const TTF_uint8* data) {
    assert(0);
}


/* -------------- */
/* loca functions */
/* -------------- */
static TTF_Offset32 loca__get_glyf_block_off(const TTF* font, TTF_uint32 glyphIndex) {
    assert(glyphIndex);

    TTF_int16 version = ttf__read_int16(font->data + font->head.off + 50);

    if (version == 0) {
        // The offset divided by 2 is stored
        return 2 * ttf__read_Offset16(font->data + font->loca.off + (2 * glyphIndex));
    }

    assert(version == 1);
    return ttf__read_Offset32(font->data + font->loca.off + (4 * glyphIndex));
}


/* -------------- */
/* prep functions */
/* -------------- */
static void prep__execute(TTF* font) {
    TTF_PRINT("\n-- CV Program --\n");

    TTF_IStream stream;
    ttf__istream_init(&stream, font->data + font->prep.off);
    
    while (stream.off < font->prep.size) {
        TTF_uint8 ins = ttf__istream_next(&stream);
        ttf__execute_ins(font, &stream, ins);

        // TODO: temporary
        if (ins == TTF_CALL) {
            return;
        }
    }
}

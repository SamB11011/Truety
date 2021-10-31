#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "ttf.h"


enum {
    TTF_ON_CURVE_POINT = 0x01,
    TTF_X_SHORT_VECTOR = 0x02,
    TTF_Y_SHORT_VECTOR = 0x04,
    TTF_REPEAT_FLAG    = 0x08,
    TTF_X_DUAL         = 0x10, // X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR
    TTF_Y_DUAL         = 0x20, // Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR
    TTF_OVERLAP_SIMPLE = 0x40,
    TTF_RESERVED       = 0x80,
} TTF_Simple_Glyph_Flag;


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
static void fpgm__execute_instructions(TTF* font);


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
static void prep__execute_instructions(TTF* font);


int ttf_init(TTF* font, const char* path) {
    memset(font, 0, sizeof(TTF));

    FILE* f = fopen(path, "rb");
    assert(f);
    
    fseek(f, 0, SEEK_END);
    font->size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    font->data = calloc(font->size, 1);
    assert(font->data);
    fread(font->data, 1, font->size, f);

    fclose(f);

    // sfntVersion is 0x00010000 for fonts that contain TrueType outlines
    assert(ttf__read_uint32(font->data) == 0x00010000);

    table_dir__extract_table_info(font);
    
    if (!cmap__extract_encoding(font)) {
        free(font->data);
        return 0;
    }

    fpgm__execute_instructions(font);

    TTF_uint32 glyphIndex = cmap__get_char_glyph_index(font, 'e');
    printf("glyphIndex = %d\n", (int)glyphIndex);
    glyf__extract_glyph_coordinate_data(font, glyphIndex);

    return 1;
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
static void fpgm__execute_instructions(TTF* font) {
    for (TTF_uint32 i = 0; i < font->fpgm.size; i++) {

    }
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

            printf("(%d, %d)\n", (int)x, (int)y);

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
static void prep__execute_instructions(TTF* font) {

}

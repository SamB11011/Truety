#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "font.h"

/* ---------------- */
/* Helper functions */
/* ---------------- */
#define read_Version16Dot16(data) read_uint32(data)
#define read_Offset16(data)       read_uint16(data)
#define read_Offset32(data)       read_uint32(data)

static uint16 read_uint16(const uint8* data);
static uint32 read_uint32(const uint8* data);
static int16  read_int16 (const uint8* data);
static void   read_tag   (uint8 tag[4], const uint8* data);
static int    tag_equals (uint8 tag0[4], const char* tag1);


/* ------------------------- */
/* Table directory functions */
/* ------------------------- */
static void table_dir__extract_table_info(Font* font);


/* -------------- */
/* cmap functions */
/* -------------- */
static void   cmap__extract_encoding             (Font* font);
static uint32 cmap__get_char_glyph_index         (const Font* font, uint32 c);
static uint16 cmap__get_char_glyph_index_format_4(const Font* font, uint32 c);


/* -------------- */
/* cmap functions */
/* -------------- */
static void glyf__extract_glyph_coordinate_data          (const Font* font, uint32 glyphIndex);
static void glyf__extract_simple_glyph_coordinate_data   (const uint8* data);
static void glyf__extract_composite_glyph_coordinate_data(const uint8* data);


/* -------------- */
/* loca functions */
/* -------------- */
static Offset32 loca__get_glyf_block_off(const Font* font, uint32 glyphIndex);


/* -------------- */
/* maxp functions */
/* -------------- */
static uint16 maxp__get_num_glyphs(Font* font);


int font_init(Font* font, const char* path) {
    memset(font, 0, sizeof(Font));

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
    assert(read_uint32(font->data) == 0x00010000);

    table_dir__extract_table_info(font);
    cmap__extract_encoding(font);

    

    uint32 glyphIndex = cmap__get_char_glyph_index(font, 'A');
    glyf__extract_glyph_coordinate_data(font, glyphIndex);

    return 1;
}


/* ---------------- */
/* Helper functions */
/* ---------------- */
static uint16 read_uint16(const uint8* data) {
    return data[0] << 8 | data[1];
}

static uint32 read_uint32(const uint8* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static int16 read_int16(const uint8* data) {
    return data[0] << 8 | data[1];
}

static void read_tag(uint8 tag[4], const uint8* data) {
    memcpy(tag, data, 4);
}

static int tag_equals(uint8 tag0[4], const char* tag1) {
    return !memcmp(tag0, tag1, 4);
}


/* ------------------------- */
/* Table directory functions */
/* ------------------------- */
static void table_dir__extract_table_info(Font* font) {
    uint16 numTables = read_uint16(font->data + 4);

    for (uint16 i = 0; i < numTables; i++) {
        uint8* data = font->data + (12 + 16 * i);

        uint8 tag[4];
        read_tag(tag, data);

        Table* table = NULL;

        if (!font->cmap.exists && tag_equals(tag, "cmap")) {
            table = &font->cmap;
        }
        else if (!font->head.exists && tag_equals(tag, "glyf")) {
            table = &font->glyf;
        }
        else if (!font->head.exists && tag_equals(tag, "head")) {
            table = &font->head;
        }
        else if (!font->maxp.exists && tag_equals(tag, "maxp")) {
            table = &font->maxp;
        }
        else if (!font->loca.exists && tag_equals(tag, "loca")) {
            table = &font->loca;
        }

        if (table) {
            table->exists = 1;
            table->off    = read_Offset32(data + 8);
            table->size   = read_uint32(data + 12);
        }
    }

    assert(font->head.exists);
}


/* -------------- */
/* cmap functions */
/* -------------- */
static void cmap__extract_encoding(Font* font) {
    uint16 numTables = read_uint16(font->data + font->cmap.off + 2);
    
    for (uint16 i = 0; i < numTables; i++) {
        const uint8* data = font->data + font->cmap.off + 4 + i * 8;

        uint16 platformID = read_uint16(data);
        uint16 encodingID = read_uint16(data + 2);
        int    foundValid = 0;

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
            font->encoding.off        = read_Offset32(data + 4);
            return;
        }
    }

    assert(0);
}

static uint32 cmap__get_char_glyph_index(const Font* font, uint32 c) {
    const uint8* data = font->data + font->cmap.off + font->encoding.off;

    switch (read_uint16(data)) {
        case 4:
            return cmap__get_char_glyph_index_format_4(font, c);
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

static uint16 cmap__get_char_glyph_index_format_4(const Font* font, uint32 c) {
    // TODO: Not sure how these values are supposed to be used in the binary search so it will be
    //       done without them.
    //
    // uint16 searchRange   = read_uint16(data + 8);
    // uint16 entrySelector = read_uint16(data + 10);
    // uint16 rangeShift    = read_uint16(data + 12);

    const uint8* data     = font->data + font->cmap.off + font->encoding.off;
    uint16       segCount = read_uint16(data + 6) >> 1;
    uint16       left     = 0;
    uint16       right    = segCount - 1;

    while (left <= right) {
        uint16 mid     = (left + right) / 2;
        uint16 endCode = read_uint16(data + 14 + 2 * mid);

        if (endCode >= c) {
            if (mid == 0 || read_uint16(data + 14 + 2 * (mid - 1)) < c) {
                uint16       startCode      = read_uint16(data + 2 * segCount + 16 + 2 * mid);
                const uint8* idRangeOffsets = data + 6 * segCount + 16 + 2 * mid;
                uint16       idRangeOffset  = read_uint16(idRangeOffsets);

                if (startCode > c) {
                    return 0;
                }
                
                if (idRangeOffset == 0) {
                    uint16 idDelta = read_int16(data + 4 * segCount + 16 + 2 * mid);
                    return c + idDelta;
                }

                return read_uint16(idRangeOffset + 2 * (c - startCode) + idRangeOffsets);
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
/* cmap functions */
/* -------------- */
static void glyf__extract_glyph_coordinate_data(const Font* font, uint32 glyphIndex) {
    const uint8* data = font->data + 
                        font->glyf.off + 
                        loca__get_glyf_block_off(font, glyphIndex);

    if (read_int16(data) >= 0) {
        glyf__extract_simple_glyph_coordinate_data(data);
    }
    else {
        glyf__extract_composite_glyph_coordinate_data(data);
    }
}

static void glyf__extract_simple_glyph_coordinate_data(const uint8* data) {
    #define IS_REPEATED_COORD(flags, coord)                                 \
        (!(flags & coord ## _SHORT_VECTOR) &&                               \
         (flags & coord ## _IS_SAME_OR_POSITIVE_ ## coord ## _SHORT_VECTOR))

    enum {
        ON_CURVE_POINT                       = 0x01,
        X_SHORT_VECTOR                       = 0x02,
        Y_SHORT_VECTOR                       = 0x04,
        REPEAT_FLAG                          = 0x08,
        X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR = 0x10,
        Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR = 0x20,
        OVERLAP_SIMPLE                       = 0x40,
        RESERVED                             = 0x80,
    };

    uint32 numContours = read_int16(data);
    uint32 numPoints   = 1 + read_uint16(data + 8 + 2 * numContours);
    uint32 flagsSize   = 0;
    uint32 xCoordsSize = 0;
    uint32 yCoordsSize = 0;

    data += 10 + 2 * numContours;
    data += 2 + read_uint16(data);

    for (uint32 i = 0; i < numPoints;) {
        uint8  flags     = data[flagsSize];
        uint32 flagsReps = (flags & REPEAT_FLAG) ? data[flagsSize + 1] : 1;

        for (uint32 j = 0; j < flagsReps; j++) {
            if (!IS_REPEATED_COORD(flags, X)) {
                xCoordsSize += (flags & X_SHORT_VECTOR) ? 1 : 2;
            }

            if (!IS_REPEATED_COORD(flags, Y)) {
                yCoordsSize += (flags & Y_SHORT_VECTOR) ? 1 : 2;
            }
        }

        i += flagsReps;
        flagsSize++;
    }

    printf("flagsSize = %d, xCoordsSize = %d, yCoordsSize = %d\n", flagsSize, xCoordsSize, yCoordsSize);

    #undef IS_REPEATED_COORD
}

static void glyf__extract_composite_glyph_coordinate_data(const uint8* data) {
    assert(0);
}


/* -------------- */
/* loca functions */
/* -------------- */
static Offset32 loca__get_glyf_block_off(const Font* font, uint32 glyphIndex) {
    assert(glyphIndex);

    int16 version = read_int16(font->data + font->head.off + 50);

    if (version == 0) {
        // The offset divided by 2 is stored
        return 2 * read_Offset16(font->data + font->loca.off + (2 * glyphIndex));
    }

    assert(version == 1);
    return read_Offset32(font->data + font->loca.off + (4 * glyphIndex));
}


/* -------------- */
/* maxp functions */
/* -------------- */
static uint16 maxp__get_num_glyphs(Font* font) {
    return read_uint16(font->data + font->maxp.off);
}

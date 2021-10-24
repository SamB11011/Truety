#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "font.h"

#define read_Version16Dot16(data) read_uint32(data)
#define read_Offset16(data)       read_uint16(data)
#define read_Offset32(data)       read_uint32(data)

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


static void cmap__set_encoding(Font* font) {
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

int font_init(Font* font, const char* path) {
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

    uint16 numTables = read_uint16(font->data + 4);

    for (uint16 i = 0; i < numTables; i++) {
        uint8* data = font->data + (12 + 16 * i);

        uint8 tag[4];
        read_tag(tag, data);

        Table* table = NULL;

        if (!font->cmap.exists && tag_equals(tag, "cmap")) {
            table = &font->cmap;
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
            table->off    = read_uint32(data + 8);
            table->len    = read_uint32(data + 12);
        }
    }

    cmap__set_encoding(font);

    printf("%d\n", (int)cmap__get_char_glyph_index(font, 7890));

    return 1;
}

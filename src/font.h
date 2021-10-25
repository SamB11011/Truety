#ifndef FONT_H
#define FONT_H

#include <stdint.h>

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

typedef int8_t  int8;
typedef int16_t int16;
typedef int32_t int32;

typedef int16  FWORD;
typedef uint16 UFWORD;
typedef uint16 Offset16;
typedef uint32 Offset32;
typedef uint32 Version16Dot16;

typedef struct {
    int      exists;
    Offset32 off;
    uint32   size;
} Table;

typedef struct {
    uint16   platformID;
    uint16   encodingID;
    Offset32 off;
} Encoding;

typedef struct {
    uint8*   data;
    uint32   size;
    Table    cmap;
    Table    glyf;
    Table    head;
    Table    maxp;
    Table    loca;
    Encoding encoding;
} Font;

int font_init(Font* font, const char* path);

#endif

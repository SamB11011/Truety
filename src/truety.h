#ifndef TRUETY_H
#define TRUETY_H

#include <stdint.h>

#define TTY_TRUE  1
#define TTY_FALSE 0

typedef uint8_t  TTY_uint8;
typedef uint16_t TTY_uint16;
typedef uint32_t TTY_uint32;
typedef uint64_t TTY_uint64;

typedef int8_t  TTY_int8;
typedef int16_t TTY_int16;
typedef int32_t TTY_int32;
typedef int64_t TTY_int64;

typedef TTY_uint8  TTY_bool;
typedef TTY_int16  TTY_FWORD;
typedef TTY_uint16 TTY_UFWORD;
typedef TTY_uint16 TTY_Offset16;
typedef TTY_uint32 TTY_Offset32;
typedef TTY_uint32 TTY_Version16Dot16;
typedef TTY_int32  TTY_F2Dot14;
typedef TTY_int32  TTY_F10Dot22;
typedef TTY_int32  TTY_F16Dot16;
typedef TTY_int32  TTY_F26Dot6;

typedef enum {
    TTY_ON_CURVE_POINT ,
    TTY_OFF_CURVE_POINT,
} TTY_Point_Type;

typedef enum {
    TTY_UNTOUCHED = 0x0,
    TTY_TOUCH_X   = 0x1,
    TTY_TOUCH_Y   = 0x2,
} TTY_Touch_Flag;

typedef struct {
    TTY_bool     exists;
    TTY_Offset32 off;
    TTY_uint32   size;
} TTY_Table;

typedef struct {
    TTY_uint16   platformID;
    TTY_uint16   encodingID;
    TTY_uint16   format;
    TTY_Offset32 off;
} TTY_Encoding;

typedef struct {
    TTY_int32 x, y;
} TTY_V2,
  TTY_Fix_V2,
  TTY_F2Dot14_V2,
  TTY_F26Dot6_V2;

typedef union {
    TTY_int32  sValue;
    TTY_uint32 uValue;
} TTY_Stack_Frame;

typedef struct {
    TTY_Stack_Frame* frames;
    TTY_uint16       count;
    TTY_uint16       cap;
} TTY_Stack;

typedef struct {
    TTY_uint8* firstIns;
} TTY_Func;

typedef struct {
    TTY_Func*  funcs;
    TTY_uint16 count;
    TTY_uint16 cap;
} TTY_Func_Array;

typedef struct {
    TTY_uint8*      mem;
    TTY_V2*         org;
    TTY_F26Dot6_V2* orgScaled;
    TTY_F26Dot6_V2* cur;
    TTY_Touch_Flag* touchFlags;
    TTY_Point_Type* pointTypes;
    TTY_uint32      cap;
} TTY_Zone;

typedef struct {
    TTY_bool       autoFlip;
    TTY_F26Dot6    controlValueCutIn;
    TTY_uint32     deltaBase;
    TTY_uint32     deltaShift;
    TTY_F2Dot14_V2 dualProjVec;
    TTY_F2Dot14_V2 freedomVec;
    TTY_uint32     loop;
    TTY_F26Dot6    minDist;
    TTY_F2Dot14_V2 projVec;
    TTY_uint32     rp0;
    TTY_uint32     rp1;
    TTY_uint32     rp2;
    TTY_uint8      roundState;
    TTY_bool       scanControl;
    TTY_uint8      scanType;
    TTY_F26Dot6    singleWidthCutIn;
    TTY_F26Dot6    singleWidthValue;
    TTY_Touch_Flag touchFlags;
    TTY_Zone*      zp0;
    TTY_Zone*      zp1;
    TTY_Zone*      zp2;
} TTY_Graphics_State;

typedef struct {
    TTY_uint8*      mem;
    TTY_V2*         points;
    TTY_Point_Type* pointTypes;
} TTY_Unhinted;

typedef struct {
    /* Unlike zone 1, the twilight zone persists between glyph programs. 
       Therefore, it is stored here instead of TTY_Temp. */
    TTY_Zone     zone0;
    TTY_uint8*   mem;
    TTY_F26Dot6* cvt;
    TTY_int32*   storageArea;
    TTY_bool     isRotated;
    TTY_bool     isStretched;
    TTY_uint32   ppem;
    TTY_F10Dot22 scale;
} TTY_Instance;

typedef struct {
    TTY_uint8* pixels;
    TTY_uint16 w;
    TTY_uint16 h;
} TTY_Image;

typedef struct {
    TTY_uint32 idx;
    TTY_uint16 xAdvance;
    TTY_V2     offset;
    TTY_V2     bitmapPos;
    TTY_V2     size;
} TTY_Glyph;

/* Stores temporary variables that are used during execution of a glyph program
   and/ or glyph rendering. */
typedef struct {
    union {
        TTY_Zone     zone1;
        TTY_Unhinted unhinted;
    };

    TTY_Instance* instance;
    TTY_Glyph*    glyph;
    TTY_uint16    numContours;
    TTY_uint32    numPoints;
    TTY_uint8*    glyfBlock;
} TTY_Temp;

typedef struct {
    TTY_uint8*         data;
    TTY_uint32         size;
    TTY_bool           hasHinting;
    TTY_uint8*         insMem;
    TTY_Table          cmap;
    TTY_Table          cvt;
    TTY_Table          fpgm;
    TTY_Table          glyf;
    TTY_Table          head;
    TTY_Table          hhea;
    TTY_Table          hmtx;
    TTY_Table          loca;
    TTY_Table          maxp;
    TTY_Table          OS2;
    TTY_Table          prep;
    TTY_Table          vmtx;
    TTY_Encoding       encoding;
    TTY_int16          ascender;
    TTY_int16          descender;
    TTY_Stack          stack;
    TTY_Func_Array     funcArray;
    TTY_Graphics_State gState;
    TTY_Temp           temp;
} TTY;

TTY_bool tty_init(TTY* font, const char* path);

TTY_bool tty_instance_init(TTY* font, TTY_Instance* instance, TTY_uint32 ppem);

TTY_bool tty_image_init(TTY_Image* image, TTY_uint8* pixels, TTY_uint32 w, TTY_uint32 h);

void tty_glyph_init(TTY* font, TTY_Glyph* glyph, TTY_uint32 glyphIdx);

void tty_free(TTY* font);

void tty_free_instance(TTY* font, TTY_Instance* instance);

void tty_free_image(TTY_Image* image);

TTY_uint32 tty_get_glyph_index(TTY* font, TTY_uint32 cp);

TTY_uint16 tty__get_num_glyphs(TTY* font);

TTY_int32 tty_get_ascender(TTY* font, TTY_Instance* instance);

TTY_int32 tty_get_advance_width(TTY_Instance* instance, TTY_Glyph* glyph);

TTY_bool tty_render_glyph(TTY* font, TTY_Image* image, TTY_uint32 cp);

TTY_bool tty_render_glyph_to_existing_image(TTY*          font, 
                                            TTY_Instance* instance, 
                                            TTY_Image*    image, 
                                            TTY_Glyph*    glyph, 
                                            TTY_uint32    x, 
                                            TTY_uint32    y);

#endif

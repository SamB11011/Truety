#ifndef TTF_H
#define TTF_H

#include <stdint.h>

#define TTF_TRUE  1
#define TTF_FALSE 0

typedef uint8_t  TTF_uint8;
typedef uint16_t TTF_uint16;
typedef uint32_t TTF_uint32;
typedef uint64_t TTF_uint64;

typedef int8_t  TTF_int8;
typedef int16_t TTF_int16;
typedef int32_t TTF_int32;
typedef int64_t TTF_int64;

typedef TTF_uint8  TTF_bool;
typedef TTF_int16  TTF_FWORD;
typedef TTF_uint16 TTF_UFWORD;
typedef TTF_uint16 TTF_Offset16;
typedef TTF_uint32 TTF_Offset32;
typedef TTF_uint32 TTF_Version16Dot16;
typedef TTF_int32  TTF_F2Dot14;
typedef TTF_int32  TTF_F10Dot22;
typedef TTF_int32  TTF_F16Dot16;
typedef TTF_int32  TTF_F26Dot6;

typedef enum {
    TTF_ON_CURVE_POINT ,
    TTF_OFF_CURVE_POINT,
} TTF_Point_Type;

typedef enum {
    TTF_UNTOUCHED = 0x0,
    TTF_TOUCH_X   = 0x1,
    TTF_TOUCH_Y   = 0x2,
} TTF_Touch_Flag;

typedef struct {
    TTF_bool     exists;
    TTF_Offset32 off;
    TTF_uint32   size;
} TTF_Table;

typedef struct {
    TTF_uint16   platformID;
    TTF_uint16   encodingID;
    TTF_uint16   format;
    TTF_Offset32 off;
} TTF_Encoding;

typedef struct {
    TTF_int32 x, y;
} TTF_V2,
  TTF_Fix_V2,
  TTF_F2Dot14_V2,
  TTF_F26Dot6_V2;

typedef union {
    TTF_int32  sValue;
    TTF_uint32 uValue;
} TTF_Stack_Frame;

typedef struct {
    TTF_Stack_Frame* frames;
    TTF_uint16       count;
    TTF_uint16       cap;
} TTF_Stack;

typedef struct {
    TTF_uint8* firstIns;
} TTF_Func;

typedef struct {
    TTF_Func*  funcs;
    TTF_uint16 count;
    TTF_uint16 cap;
} TTF_Func_Array;

typedef struct {
    TTF_uint8*      mem;
    TTF_V2*         org;
    TTF_F26Dot6_V2* orgScaled;
    TTF_F26Dot6_V2* cur;
    TTF_Touch_Flag* curTouchFlags;
    TTF_Point_Type* pointTypes;
    TTF_uint32      count;
    TTF_uint32      cap;
} TTF_Zone;

typedef struct {
    TTF_bool       autoFlip;
    TTF_F26Dot6    controlValueCutIn;
    TTF_uint32     deltaBase;
    TTF_uint32     deltaShift;
    TTF_F2Dot14_V2 dualProjVec;
    TTF_F2Dot14_V2 freedomVec;
    TTF_uint32     loop;
    TTF_F26Dot6    minDist;
    TTF_F2Dot14_V2 projVec;
    TTF_uint32     rp0;
    TTF_uint32     rp1;
    TTF_uint32     rp2;
    TTF_uint8      roundState;
    TTF_bool       scanControl;
    TTF_F26Dot6    singleWidthCutIn;
    TTF_F26Dot6    singleWidthValue;
    TTF_Touch_Flag touchFlags;
    TTF_Zone*      zp0;
    TTF_Zone*      zp1;
    TTF_Zone*      zp2;
} TTF_Graphics_State;

typedef struct {
    TTF_uint8*      mem;
    TTF_V2*         points;
    TTF_Point_Type* pointTypes;
} TTF_Unhinted;

typedef struct {
    TTF_F26Dot6* cvt;
    TTF_bool     isRotated;
    TTF_bool     isStretched;
    TTF_uint32   ppem;
    TTF_F10Dot22 scale;
    /* TODO: Keep Graphics State default values here in case defaults are set
             by the CV program. */
} TTF_Instance;

typedef struct {
    TTF_uint8* pixels;
    TTF_uint16 w;
    TTF_uint16 h;
} TTF_Image;

typedef struct {
    TTF_uint32 idx;
    TTF_uint16 xAdvance;
    TTF_V2     bitmapPos;
    TTF_V2     offset;
    TTF_V2     size;
} TTF_Glyph;

typedef struct {
    union {
        TTF_Zone     zones[2];
        TTF_Unhinted unhinted;
    };

    TTF_Instance* instance;
    TTF_Glyph*    glyph;
    TTF_uint16    numContours;
    TTF_uint32    numPoints;
    TTF_uint8*    glyfBlock;
} TTF_Current;

typedef struct {
    TTF_uint8*         data;
    TTF_uint32         size;
    TTF_bool           hasHinting;
    TTF_uint8*         insMem;
    TTF_Table          cmap;
    TTF_Table          cvt;
    TTF_Table          fpgm;
    TTF_Table          glyf;
    TTF_Table          head;
    TTF_Table          hhea;
    TTF_Table          hmtx;
    TTF_Table          loca;
    TTF_Table          maxp;
    TTF_Table          OS2;
    TTF_Table          prep;
    TTF_Table          vmtx;
    TTF_Encoding       encoding;
    TTF_int16          ascender;
    TTF_int16          descender;
    TTF_Stack          stack;
    TTF_Func_Array     funcArray;
    TTF_Graphics_State gState;
    TTF_Current        cur;
} TTF;

TTF_bool ttf_init         (TTF* font, const char* path);
TTF_bool ttf_instance_init(TTF* font, TTF_Instance* instance, TTF_uint32 ppem);
TTF_bool ttf_image_init   (TTF_Image* image, TTF_uint8* pixels, TTF_uint32 w, TTF_uint32 h);
void     ttf_glyph_init   (TTF* font, TTF_Glyph* glyph, TTF_uint32 glyphIdx);

void ttf_free         (TTF* font);
void ttf_free_instance(TTF* font, TTF_Instance* instance);
void ttf_free_image   (TTF_Image* image);

TTF_uint32 ttf_get_glyph_index               (TTF* font, TTF_uint32 cp);
TTF_uint16 ttf__get_num_glyphs               (TTF* font);
TTF_bool   ttf_render_glyph                  (TTF* font, TTF_Image* image, TTF_uint32 cp);
TTF_bool   ttf_render_glyph_to_existing_image(TTF* font, TTF_Instance* instance, TTF_Image* image, TTF_Glyph* glyph, TTF_uint32 x, TTF_uint32 y);
TTF_int32  ttf_scale                         (TTF_Instance* instance, TTF_int32 value);

#endif

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
typedef TTY_int32  TTY_F2Dot30;
typedef TTY_int32  TTY_F10Dot22;
typedef TTY_int32  TTY_F16Dot16;
typedef TTY_int32  TTY_F26Dot6;

typedef TTY_uint8* TTY_Func;


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
    TTY_int32 x, y;
} TTY_V2,
  TTY_Fix_V2,
  TTY_F2Dot14_V2,
  TTY_F26Dot6_V2;

typedef struct {
    TTY_uint8* buffer;
    TTY_uint32 off;
} TTY_Ins_Stream;

typedef struct {
    TTY_int32* buffer;
    TTY_uint32 count;
    TTY_uint32 cap;
} TTY_Stack;

typedef struct {
    TTY_Func*  buffer;
    TTY_uint32 cap;
} TTY_Funcs;

typedef struct {
    TTY_F26Dot6* buffer;
    TTY_uint32   cap;
} TTY_CVT;

typedef struct {
    TTY_int32* buffer;
    TTY_uint32 cap;
} TTY_Storage_Area;

typedef struct {
    TTY_uint8*      mem;
    size_t          memSize;
    TTY_V2*         org;
    TTY_F26Dot6_V2* orgScaled;
    TTY_F26Dot6_V2* cur;
    TTY_Touch_Flag* touchFlags;
    TTY_Point_Type* pointTypes;
    TTY_uint32*     endPointIndices;
    TTY_uint32      numEndPoints;
    TTY_uint32      numOutlinePoints; /* Non-phantom points */
    TTY_uint32      numPoints;
} TTY_Zone;

typedef struct {
    TTY_bool       autoFlip;
    TTY_F26Dot6    controlValueCutIn;
    TTY_uint32     deltaBase;
    TTY_uint32     deltaShift;
    TTY_F2Dot14_V2 dualProjVec;
    TTY_F2Dot14_V2 freedomVec;
    TTY_uint8      gep0;
    TTY_uint8      gep1;
    TTY_uint8      gep2;
    TTY_uint32     loop;
    TTY_F26Dot6    minDist;
    TTY_F2Dot14_V2 projVec;
    TTY_uint8      roundState;
    TTY_uint32     rp0;
    TTY_uint32     rp1;
    TTY_uint32     rp2;
    TTY_bool       scanControl;
    TTY_uint8      scanType;
    TTY_F26Dot6    singleWidthCutIn;
    TTY_F26Dot6    singleWidthValue;
    TTY_Zone*      zp0;
    TTY_Zone*      zp1;
    TTY_Zone*      zp2;
    
    TTY_Touch_Flag activeTouchFlags; /* Helper variable */
    TTY_F2Dot30    projDotFree;      /* Helper variable */
} TTY_Graphics_State;

typedef struct {
    TTY_uint8*      mem;
    TTY_V2*         points;
    TTY_Point_Type* pointTypes;
    TTY_uint32*     endPointIndices;
    TTY_uint32      numEndPoints;
    TTY_uint32      numOutlinePoints;
    TTY_uint32      numPoints;
} TTY_Unhinted;

typedef struct {
    TTY_uint8*       mem;
    TTY_CVT          cvt;
    TTY_Storage_Area storage;
    TTY_Zone         zone0;
    TTY_bool         useHinting;
    TTY_bool         isRotated;
    TTY_bool         isStretched;
    TTY_uint32       ppem;
    TTY_F10Dot22     scale;
} TTY_Instance;

typedef struct {
    TTY_uint32 idx;
    TTY_V2     advance;
    TTY_V2     offset;
    TTY_V2     size;
} TTY_Glyph;

typedef struct {
    TTY_Glyph* glyph;
    TTY_uint8* glyfBlock;
    TTY_int16  numContours;

    union {
        TTY_Unhinted unhinted;
        TTY_Zone     zone1;
    };
} TTY_Glyph_Data;

struct TTY_Interp;

typedef struct {
    TTY_Ins_Stream  stream;
    TTY_Instance*   instance;
    TTY_Glyph_Data* glyphData;
    void (*execute_next_ins)(struct TTY_Interp*);
} TTY_Temp_Interp_Data;

typedef struct TTY_Interp {
    TTY_uint8*            mem;
    TTY_Stack             stack;
    TTY_Funcs             funcs;
    TTY_Graphics_State    gState;
    TTY_Temp_Interp_Data* temp;
} TTY_Interp;

typedef struct {
    TTY_uint8* pixels;
    TTY_uint16 w;
    TTY_uint16 h;
} TTY_Image;

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
    TTY_uint8*   data;
    TTY_uint32   size;
    TTY_Table    cmap;
    TTY_Table    cvt;
    TTY_Table    fpgm;
    TTY_Table    glyf;
    TTY_Table    head;
    TTY_Table    hhea;
    TTY_Table    hmtx;
    TTY_Table    loca;
    TTY_Table    maxp;
    TTY_Table    OS2;
    TTY_Table    prep;
    TTY_Table    vmtx;
    TTY_Encoding encoding;
    TTY_bool     hasHinting;
    TTY_uint16   upem;
    TTY_int16    ascender;
    TTY_int16    descender;
    TTY_int16    lineGap;
    TTY_Interp   interp;
} TTY;

TTY_bool tty_init(TTY* font, const char* path);

TTY_bool tty_instance_init(TTY* font, TTY_Instance* instance, TTY_uint32 ppem, TTY_bool useHinting);

void tty_glyph_init(TTY_Glyph* glyph, TTY_uint32 glyphIdx);

TTY_bool tty_image_init(TTY_Image* image, TTY_uint8* pixels, TTY_uint32 w, TTY_uint32 h);

void tty_free(TTY* font);

void tty_instance_free(TTY_Instance* instance);

void tty_image_free(TTY_Image* image);

TTY_uint32 tty_get_glyph_index(TTY* font, TTY_uint32 cp);

TTY_uint16 tty_get_num_glyphs(TTY* font);

TTY_int32 tty_get_ascender(TTY* font, TTY_Instance* instance);

TTY_int32 tty_get_descender(TTY* font, TTY_Instance* instance);

TTY_int32 tty_get_line_gap(TTY* font, TTY_Instance* instance);

TTY_int32 tty_get_new_line_offset(TTY* font, TTY_Instance* instance);

TTY_int32 tty_get_max_horizontal_extent(TTY* font, TTY_Instance* instance);

TTY_bool tty_render_glyph(TTY*          font,
                          TTY_Instance* instance,
                          TTY_Glyph*    glyph,
                          TTY_Image*    image);

TTY_bool tty_render_glyph_to_existing_image(TTY*          font, 
                                            TTY_Instance* instance, 
                                            TTY_Glyph*    glyph,
                                            TTY_Image*    image,
                                            TTY_uint32    x, 
                                            TTY_uint32    y);

#endif

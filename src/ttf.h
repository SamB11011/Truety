#ifndef TTF_H
#define TTF_H

#include <stdint.h>

typedef uint8_t  TTF_uint8;
typedef uint16_t TTF_uint16;
typedef uint32_t TTF_uint32;
typedef uint64_t TTF_uint64;

typedef int8_t  TTF_int8;
typedef int16_t TTF_int16;
typedef int32_t TTF_int32;
typedef int64_t TTF_int64;

typedef TTF_int16  TTF_FWORD;
typedef TTF_uint16 TTF_UFWORD;
typedef TTF_uint16 TTF_Offset16;
typedef TTF_uint32 TTF_Offset32;
typedef TTF_uint32 TTF_Version16Dot16;
typedef TTF_int32  TTF_F2Dot14;
typedef TTF_int32  TTF_F10Dot22;
typedef TTF_int32  TTF_F16Dot16;
typedef TTF_int32  TTF_F26Dot6;

typedef struct {
    int          exists;
    TTF_Offset32 off;
    TTF_uint32   size;
} TTF_Table;

typedef struct {
    TTF_uint16   platformID;
    TTF_uint16   encodingID;
    TTF_uint16   format;
    TTF_Offset32 off;
} TTF_Encoding;

typedef union {
    TTF_int32  sValue;
    TTF_uint32 uValue;
} TTF_Stack_Frame;

typedef struct {
    TTF_Stack_Frame* frames;
    TTF_uint16       numFrames;
} TTF_Stack;

typedef struct {
    TTF_uint8* firstIns;
} TTF_Func;

typedef struct {
    TTF_F2Dot14 xProjectionVector;
    TTF_F2Dot14 yProjectionVector;
} TTF_Graphics_State;

typedef struct {
    TTF_F26Dot6*        cvt;
    TTF_Graphics_State* graphicsState;
    TTF_uint32          ppem;
    TTF_F10Dot22        scale;
} TTF_Instance;

typedef struct {
    TTF_uint8* pixels;
    TTF_uint16 w;
    TTF_uint16 h;
    TTF_uint16 stride;
} TTF_Image;

typedef struct {
    TTF_uint8*          data;
    TTF_uint32          size;
    TTF_uint8*          insMem;
    TTF_Table           cmap;
    TTF_Table           cvt;
    TTF_Table           fpgm;
    TTF_Table           glyf;
    TTF_Table           head;
    TTF_Table           maxp;
    TTF_Table           loca;
    TTF_Table           prep;
    TTF_Encoding        encoding;
    TTF_Stack           stack;
    TTF_Func*           funcs;
    TTF_Graphics_State* graphicsState;
    TTF_Instance*       instance;
} TTF;


int ttf_init         (TTF* font, const char* path);
int ttf_instance_init(TTF* font, TTF_Instance* instance, TTF_uint32 ppem);
int ttf_image_init   (TTF_Image* image, TTF_uint8* pixels, TTF_uint32 w, TTF_uint32 h, TTF_uint32 stride);

void ttf_free         (TTF* font);
void ttf_free_instance(TTF* font, TTF_Instance* instance);
void ttf_free_image   (TTF_Image* image);

void ttf_set_current_instance(TTF* font, TTF_Instance* instance);

int ttf_render_glyph                  (TTF* font, TTF_Image* image, TTF_uint32 cp);
int ttf_render_glyph_to_existing_image(TTF* font, TTF_Image* image, TTF_uint32 cp, TTF_uint32 x, TTF_uint32 y);

#endif

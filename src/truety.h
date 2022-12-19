#ifndef TRUETY_H
#define TRUETY_H

#include <stdint.h>

#define TTY_TRUE  1
#define TTY_FALSE 0


struct TTY_Program_Context;
struct TTY_Zone;

typedef uint8_t  TTY_Bool;
typedef uint8_t  TTY_U8;
typedef uint16_t TTY_U16;
typedef uint32_t TTY_U32;
typedef uint64_t TTY_U64;

typedef int8_t  TTY_S8;
typedef int16_t TTY_S16;
typedef int32_t TTY_S32;
typedef int64_t TTY_S64;

typedef TTY_S32 TTY_F2Dot14;
typedef TTY_S32 TTY_F2Dot30;
typedef TTY_S32 TTY_F10Dot22;
typedef TTY_S32 TTY_F16Dot16;
typedef TTY_S32 TTY_F26Dot6;

typedef void (*TTY_Move_Point_Func)(struct TTY_Program_Context*, struct TTY_Zone*, TTY_U32, TTY_F26Dot6);


typedef enum {
    TTY_ERROR_NONE                       ,
    TTY_ERROR_FAILED_TO_READ_FILE        ,
    TTY_ERROR_FILE_IS_NOT_TTF            ,
    TTY_ERROR_FILE_IS_CORRUPTED          ,
    TTY_ERROR_UNSUPPORTED_FEATURE        , /* TODO: documentation can explain why a function returns this */
    TTY_ERROR_OUT_OF_MEMORY              ,
    TTY_ERROR_UNKNOWN_INSTRUCTION        , /* TODO: This will be deprecated once all instructions are implemented */
    TTY_ERROR_GLYPH_DOES_NOT_FIT_IN_IMAGE,
} TTY_Error;

typedef enum {
    TTY_INSTANCE_DEFAULT                = 0,
    TTY_INSTANCE_NO_HINTING             = 1,
    TTY_INSTANCE_SUBPIXEL_RENDERING_RGB = 2, /* TODO: implement subpixel rendering */
} TTY_Instance_Flag;

typedef struct {
    TTY_S32  x, y;
} TTY_V2,
  TTY_F2Dot14_V2,
  TTY_F26Dot6_V2;

typedef struct {
    TTY_U32  x, y;
} TTY_U32_V2;

typedef struct {
    TTY_F26Dot6_V2  p0;
    TTY_F26Dot6_V2  p1; /* Control point */
    TTY_F26Dot6_V2  p2;
} TTY_Curve;

typedef struct {
    TTY_Curve*  buff;
    TTY_U32     cap;
    TTY_U32     count;
} TTY_Curves;

typedef struct {
    TTY_U8**  insPtrs;
    TTY_U32*  sizes;
    TTY_U16   cap;
    TTY_U16   count;
} TTY_Funcs;

typedef struct {
    TTY_F26Dot6*  buff;
    TTY_S16       cap;
    TTY_S16       count;
} TTY_CVT;

typedef struct  {
    TTY_S32*  buff;
    TTY_U32   cap;
    TTY_U32   count;
} TTY_Storage_Area;

typedef struct {
    TTY_U32*  buff;
    TTY_U16   cap;
    TTY_U16   count;
} TTY_Interp_Stack;

/* org, pointTypes, endPointIndices, numOutlinePoints, numEndPoints, and 
   maxEndPoints are only used by zone1 */
typedef struct TTY_Zone {
    TTY_V2*          org;       /* Unscaled glyph points at their original positions  */
    TTY_F26Dot6_V2*  orgScaled; /* Scaled glyph points at their original positions    */
    TTY_F26Dot6_V2*  cur;       /* Scaled glyph points at their hinted positions      */
    TTY_U8*          pointTypes;
    TTY_U8*          touchFlags;
    TTY_U16*         endPointIndices;
    TTY_U32          numPoints;
    TTY_U32          numOutlinePoints; /* The number of points excluding phantom points */
    TTY_U32          maxPoints;
    TTY_U16          numEndPoints;
    TTY_U16          maxEndPoints;
} TTY_Zone;

typedef struct {
    TTY_Move_Point_Func  move_point;
    TTY_Zone*            zp0;
    TTY_Zone*            zp1;
    TTY_Zone*            zp2;
    TTY_F2Dot30          projDotFree;
    TTY_F26Dot6          minDist;
    TTY_F26Dot6          controlValueCutIn;
    TTY_F26Dot6          singleWidthCutIn;
    TTY_F26Dot6          singleWidthValue;
    TTY_F2Dot14_V2       dualProjVec;
    TTY_F2Dot14_V2       freedomVec;
    TTY_F2Dot14_V2       projVec;
    TTY_U32              deltaBase;
    TTY_U32              deltaShift;
    TTY_U32              loop;
    TTY_U32              rp0;
    TTY_U32              rp1;
    TTY_U32              rp2;
    TTY_U8               gep0;
    TTY_U8               gep1;
    TTY_U8               gep2;
    TTY_U8               roundState;
    TTY_U8               scanType;
    TTY_Bool             autoFlip;
    TTY_Bool             scanControl;
} TTY_Graphics_State;

/* Glyph points/ curves are stored in zone1 even if the font doesn't have 
   hinting or hinting is disabled */
typedef struct {
    TTY_U8*             mem;
    TTY_Curves          curves;
    TTY_Zone            zone1;
    TTY_Interp_Stack    stack;
    TTY_Funcs           funcs;
    TTY_Graphics_State  gs;
} TTY_Font_Hinting_Data;

typedef struct {
    TTY_U32   off;
    TTY_U32   size;
    TTY_Bool  exists;
} TTY_Table;

/*
 * Supported platforms, encodings, and formats:
 *     Platform    Encodings    Formats
 *     0           3, 4, 6      4 or 6, 10 or 12, 13   
 *     3           1, 10        4, 12
 *
 * Format UTF Encodings:
 *     Format 4:  UTF-16
 *     Format 6:  UTF-16
 *     Format 10: UTF-32
 *     Format 12: UTF-32
 *     Format 13: UTF-32
 *
 * Note: All functions with a 'codePoint' aregument expect it to be UTF-8.
 *       The function will then convert the code point to the required encoding
 *       as needed.
 *
 * TODO: Handle all supported platforms, encodings, and formats
 *       Currently, only platform 0, encoding 3, format 4 is handled
 */
typedef struct {
    TTY_U32   off;
    TTY_U16   platformId;
    TTY_U16   encodingId;
    TTY_U16   format;
    TTY_U8    utf; /* This equals 16 if UTF-16 or 32 if UTF-32 (None of the supported formats are UTF-8) */
} TTY_Encoding;

typedef struct {
    TTY_Font_Hinting_Data  hint;
    TTY_U8*                fileData;
    TTY_S32                fileSize;
    TTY_Table              cmap;
    TTY_Table              cvt;
    TTY_Table              fpgm;
    TTY_Table              glyf;
    TTY_Table              head;
    TTY_Table              hhea;
    TTY_Table              hmtx;
    TTY_Table              loca;
    TTY_Table              maxp;
    TTY_Table              OS2;
    TTY_Table              prep;
    TTY_Table              vmtx;
    TTY_Encoding           encoding;
    TTY_U32                numGlyphs;
    TTY_U32                startingEdgeCap;
    TTY_U16                upem;
    TTY_S16                ascender;
    TTY_S16                descender;
    TTY_S16                lineGap;
    TTY_S16                maxHoriExtent;
    TTY_Bool               hasHinting;
} TTY_Font;

/* Each instance needs its own zone0 data since the data must persist for the 
   lifetime of the instance. */
typedef struct {
    TTY_U8*           mem;
    TTY_CVT           cvt;
    TTY_Storage_Area  storage;
    TTY_Zone          zone0;
} TTY_Instance_Hinting_Data;

typedef struct {
    TTY_Instance_Hinting_Data  hint;
    TTY_U32                    ppem;
    TTY_S32                    ascender;
    TTY_S32                    descender;
    TTY_S32                    lineGap;
    TTY_V2                     maxGlyphSize;
    TTY_F10Dot22               scale;
    TTY_Bool                   useHinting;
    TTY_Bool                   useSubpixelRendering; /* TODO: Implement subpixel rendering */
    TTY_Bool                   isRotated;            /* TODO: Implement rotation */
    TTY_Bool                   isStretched;          /* TODO: Implement stretching */
} TTY_Instance;

/* advance, offset, and size are not calculated until the glyph is rendered */
typedef struct {
    TTY_U8*  glyfBlock;
    TTY_U32  idx;
    TTY_V2   advance;
    TTY_V2   offset;
    TTY_V2   size;
    TTY_S16  numContours; /* Equals -1 if the glyph is a composite glyph */
} TTY_Glyph;

typedef struct {
    TTY_U8*     pixels;
    TTY_U32_V2  size;
    TTY_U32     numChannels;
} TTY_Image;

typedef struct {
    TTY_Glyph   glyph;
    TTY_U32_V2  atlasPos;
} TTY_Atlas_Cache_Entry;

typedef struct TTY_Atlas_Cache_Node {
    TTY_U32                       codePoint;
    TTY_Atlas_Cache_Entry         entry;
    struct TTY_Atlas_Cache_Node*  lruPrev;
    struct TTY_Atlas_Cache_Node*  lruNext;
    struct TTY_Atlas_Cache_Node*  next;
} TTY_Atlas_Cache_Node;

typedef struct {
    TTY_U8*                mem;
    TTY_Atlas_Cache_Node*  nodes;
    TTY_Atlas_Cache_Node** chainHeads;
    TTY_Atlas_Cache_Node*  lruHead;
    TTY_Atlas_Cache_Node*  lruTail;
    TTY_Image              atlas;
    TTY_U32_V2             slotSize;
    TTY_U32_V2             nextAtlasPos;
    TTY_U32                numGlyphs;
    TTY_U32                maxGlyphs;
} TTY_Atlas_Cache;


/* 
 * Creates a `TTY_Font` using the TTF file specified by `path`.
 *
 * Returns one of the following:
 *     TTY_ERROR_NONE                - The font was successfully loaded.
 *     TTY_ERROR_OUT_OF_MEMORY       - Not enough memory could be allocated to load the font.
 *     TTY_ERROR_FAILED_TO_READ_FILE - The file contents could not be read.
 *     TTY_ERROR_FILE_IS_NOT_TTF     - The file does not contain a TTF file signature.
 *     TTY_ERROR_FILE_IS_CORRUPTED   - The file content differs from what is expected.
 *     TTY_ERROR_UNSUPPORTED_FEATURE - The file uses an encoding that is not Unicode.
 *     TTY_ERROR_UNKNOWN_INSTRUCTION - The font has hinting and the font program has an instruction that is not yet handled.
 */
TTY_Error tty_font_init(TTY_Font* font, const char* path);

void tty_font_free(TTY_Font* font);

/*
 * Creates a `TTY_Instance` which is an instance of a 'TTY_Font'. Each 
 * `TTY_Instance` corresponds to exactly one font and exactly one size (ppem).
 * Each `TTY_Font` can have any number of instances.
 *
 * Returns one of the following:
 *     TTY_ERROR_NONE                - The font was successfully loaded.
 *     TTY_ERROR_OUT_OF_MEMORY       - Not enough memory could be allocated to create an instance of the font.
 *     TTY_ERROR_UNKNOWN_INSTRUCTION - The instance uses hinting and the CV program has an instruction that is not yet handled.
 */
TTY_Error tty_instance_init(TTY_Font* font, TTY_Instance* instance, TTY_U32 ppem, TTY_U32 flags);


/*
 * Returns one of the following:
 *     TTY_ERROR_NONE                - The font was successfully loaded.
 *     TTY_ERROR_UNKNOWN_INSTRUCTION - The instance uses hinting and the CV program has an instruction that is not yet handled.
 */
TTY_Error tty_instance_resize(TTY_Font* font, TTY_Instance* instance, TTY_U32 ppem);

void tty_instance_free(TTY_Instance* instance);


/* 
 * Returns one of the following:
 *     TTY_ERROR_NONE                - The glyph index was successfully retrieved.
 *     TTY_ERROR_UNSUPPORTED_FEATURE - The font's encoding format is not handled yet.
 */
TTY_Error tty_get_glyph_index(TTY_Font* font, TTY_U32 codePoint, TTY_U32* idx);

/*
 * Returns one of the following:
 *     TTY_ERROR_NONE - The glyph was successfully loaded.
 */
TTY_Error tty_glyph_init(TTY_Font* font, TTY_Glyph* glyph, TTY_U32 idx);


/*
 * Returns one of the following:
 *     TTY_ERROR_NONE          - The image was successfully created.
 *     TTY_ERROR_OUT_OF_MEMORY - If `pixels` is NULL and `w * h` bytes could not be allocated.
 */
TTY_Error tty_image_init(TTY_Image* image, TTY_U8* pixels, TTY_U32 w, TTY_U32 h, TTY_U32 numChannels);

void tty_image_free(TTY_Image* image);


/* 
 * Returns one of the following:
 *    TTY_ERROR_NONE                - The glyph was rendered successfully.
 *    TTY_ERROR_OUT_OF_MEMORY       - Not enough memory could be allocated to render the glyph.
 *    TTY_ERROR_UNSUPPORTED_FEATURE - The glyph is a composite glyph that uses point matching.
 *    TTY_ERROR_UNKNOWN_INSTRUCTION - The instance uses hinting and the glyph program has an instruction that is not yet handled.
 */
TTY_Error tty_render_glyph(TTY_Font* font, TTY_Instance* instance, TTY_Glyph* glyph, TTY_Image* image);

/* 
 * Returns one of the following:
 *    TTY_ERROR_NONE                        - The glyph was rendered successfully.
 *    TTY_ERROR_OUT_OF_MEMORY               - Not enough memory could be allocated to render the glyph.
 *    TTY_ERROR_UNSUPPORTED_FEATURE         - The glyph is a composite glyph that uses point matching.
 *    TTY_ERROR_UNKNOWN_INSTRUCTION         - The instance uses hinting and the glyph program has an instruction that is not yet handled.
 *    TTY_ERROR_GLYPH_DOES_NOT_FIT_IN_IMAGE - The provided image is not large enough to contain the rasterized glyph.
 */
TTY_Error tty_render_glyph_to_existing_image(TTY_Font* font, TTY_Instance* instance, TTY_Glyph* glyph, TTY_Image* image, TTY_U32 x, TTY_U32 y);

/*
 * Returns one of the following:
 *     TTY_ERROR_NONE          - The cache was successfully created.
 *     TTY_ERROR_OUT_OF_MEMORY - Not enough memory could be allocated for the cache.
 */
TTY_Error tty_atlas_cache_init(TTY_Instance* instance, TTY_Atlas_Cache* cache, TTY_U32 w, TTY_U32 h);

void tty_atlas_cache_free(TTY_Atlas_Cache* cache);

/*
 * Returns TTY_ERROR_NONE on success. If the entry was not already cached, then
 * this function may return any error produced by `tty_render_glyph_to_existing_image`.
 */
TTY_Error tty_atlas_cache_get_entry(TTY_Font* font, TTY_Instance* instance, TTY_Atlas_Cache* cache, TTY_Atlas_Cache_Entry* entry, TTY_U32 codePoint);

/* Note: This does not update the cache */
TTY_Bool tty_atlas_cache_contains(TTY_Atlas_Cache* cache, TTY_U32 codePoint);

TTY_Bool tty_atlas_cache_is_full(TTY_Atlas_Cache* cache);


#endif

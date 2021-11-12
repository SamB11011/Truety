#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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


/* Inverse of 26.6 fixed point scaling factor */
#define TTF_F26DOT6_SF_INV 64

typedef enum {
    GLYF_ON_CURVE_POINT = 0x01,
    GLYF_X_SHORT_VECTOR = 0x02,
    GLYF_Y_SHORT_VECTOR = 0x04,
    GLYF_REPEAT_FLAG    = 0x08,
    GLYF_X_DUAL         = 0x10, // X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR
    GLYF_Y_DUAL         = 0x20, // Y_IS_SAME_OR_POSITIVE_Y_SHORT_VECTOR
    GLYF_OVERLAP_SIMPLE = 0x40,
    GLYF_RESERVED       = 0x80,
} Glyf_Simple_Glyph_Flags;

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
    TTF_PUSHW     = 0xB8,
    TTF_PUSHW_ABC = 0xBF,
    TTF_MUL       = 0x63,
} TTF_Instruction;

typedef struct {
    const TTF_uint8* bytes;
    TTF_uint32       off;
} TTF_IStream;

typedef struct {
    float x, y;
} TTF_Point;

typedef struct {
    TTF_Point p0;
    TTF_Point p1;
    TTF_Point ctrl;
} TTF_Curve;

typedef struct {
    TTF_Curve* curve;
    float      xIntersect;
} TTF_Active_Curve;

typedef struct {
    TTF_uint8* flagData;
    TTF_uint8* xData;
    TTF_uint8* yData;
    TTF_Point  absPos;
    TTF_uint8  flagsReps;
} Glyf_Simple_Glyph;


/* ---------------- */
/* Helper functions */
/* ---------------- */
#define ttf__get_Offset16(data)       ttf__get_uint16(data)
#define ttf__get_Offset32(data)       ttf__get_uint32(data)
#define ttf__get_Version16Dot16(data) ttf__get_uint32(data)

static TTF_uint16 ttf__get_uint16                       (const TTF_uint8* data);
static TTF_uint32 ttf__get_uint32                       (const TTF_uint8* data);
static TTF_int16  ttf__get_int16                        (const TTF_uint8* data);
static float      ttf__maxf                             (float a, float b);
static float      ttf__minf                             (float a, float b);
static float      ttf__linear_interp                    (float p0, float p1, float t);
static float      ttf__get_curve_max_y                  (const TTF_Curve* curve);
static float      ttf__get_curve_min_y                  (const TTF_Curve* curve);
static void       ttf__get_min_and_max                  (float* min, float* max, float a, float b);
static TTF_uint8  ttf__get_curve_scanline_x_intersection(const TTF_Curve* curve, float* x0, float* x1, float scanline);


/* -------------- */
/* List functions */
/* -------------- */
static TTF_Node* ttf__list_alloc_node      (TTF_List* list);
static void*     ttf__list_insert          (TTF_List* list);
static void*     ttf__list_insert_before   (TTF_List* list, TTF_Node* node);
static void      ttf__list_remove          (TTF_List* list, TTF_Node* node);
static void*     ttf__list_get_node_val    (const TTF_List* list, const TTF_Node* node);
static void*     ttf__list_get_value_buffer(const TTF_List* list);
static void      ttf__list_remove_all      (TTF_List* list);


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
static void      ttf__PUSHW              (TTF* font, TTF_IStream* stream, TTF_uint8 ins);
static void      ttf__MUL                (TTF* font);
static TTF_uint8 ttf__jump_to_else_or_eif(TTF_IStream* stream);


/* --------------- */
/* Stack functions */
/* --------------- */
#define ttf__stack_push_F2Dot14(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_push_F26Dot6(font, val) ttf__stack_push_int32(font, val)
#define ttf__stack_pop_F2Dot14(font)       ttf__stack_pop_int32(font)
#define ttf__stack_pop_F26Dot6(font)       ttf__stack_pop_int32(font)

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
static TTF_uint8* glyf__get_glyph_data_block          (const TTF* font, TTF_uint32 glyphIndex);
static void       glyf__extract_glyph_curves          (TTF* font, TTF_uint32 glyphIndex);
static void       glyf__extract_simple_glyph_curves   (TTF* font, TTF_uint8* glyphData);
static void       glyf__simple_glyph_init             (TTF_uint8* glyphData, Glyf_Simple_Glyph* glyph);
static void       glyf__insert_simple_glyph_curve     (TTF_List* curves, const TTF_Curve* curve);
static void       glyf__extract_composite_glyph_curves(TTF* font, TTF_uint8* glyphData);
static int        glyf__get_next_simple_glyph_point   (Glyf_Simple_Glyph* glyph, TTF_Point* point);
static int        glyf__peek_next_simple_glyph_point  (const Glyf_Simple_Glyph* glyph, TTF_Point* point);
static int        glyf__compare_simple_glyph_curves   (const void* a, const void* b);


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
    if (ttf__get_uint32(font->data) != 0x00010000) {
        return 0;
    }

    table_dir__extract_table_info(font);

    {
        const TTF_uint8* maxp      = font->data + font->maxp.off;
        size_t           maxCurves = ttf__get_uint16(maxp + 6);

        size_t stackFrameSize    =  sizeof(TTF_Stack_Frame) * ttf__get_uint16(maxp + 24);
        size_t funcsSize         =  sizeof(TTF_Func) * ttf__get_uint16(maxp + 20);
        size_t curvesSize        = (sizeof(TTF_Curve) + sizeof(TTF_Node)) * maxCurves;
        size_t activeCurvesSize  = (sizeof(TTF_Active_Curve) + sizeof(TTF_Node)) * maxCurves;
        size_t graphicsStateSize =  sizeof(TTF_Graphics_State);
        size_t totalSize         = stackFrameSize   + 
                                   funcsSize        + 
                                   curvesSize       + 
                                   activeCurvesSize + 
                                   graphicsStateSize;

        font->mem = calloc(1, totalSize);
        if (font->mem == NULL) {
            free(font->data);
            return 0;
        }

        size_t off = 0;

        font->stack.frames     = (TTF_Stack_Frame*)(font->mem);
        font->funcs            = (TTF_Func*)(font->mem + (off += stackFrameSize));
        font->curves.mem       = (font->mem + (off += funcsSize));
        font->activeCurves.mem = (font->mem + (off += curvesSize));
        font->graphicsState    = (TTF_Graphics_State*)(font->mem + (off += activeCurvesSize));

        font->curves.cap     = maxCurves;
        font->curves.valSize = sizeof(TTF_Curve);

        font->activeCurves.cap     = maxCurves;
        font->activeCurves.valSize = sizeof(TTF_Active_Curve);
    }

    font->graphicsState->xProjectionVector = 1 << 14;
    font->graphicsState->yProjectionVector = 0;

    if (!cmap__extract_encoding(font)) {
        ttf_free(font);
        return 0;
    }

    // fpgm__execute(font);
    // prep__execute(font);

    // TTF_uint32 glyphIndex = cmap__get_char_glyph_index(font, 'B');
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

// TODO: remove
static int count = 1;
static void print_curve(const TTF_Curve* curve) {
    printf("%2d) p0 = (%.4f, %.4f), ", count, curve->p0.x, curve->p0.y);
    printf("p1 = (%.4f, %.4f), ", curve->p1.x, curve->p1.y);
    printf("ctrl = (%.4f, %.4f)\n", curve->ctrl.x, curve->ctrl.y);
    // float ymin = ttf__minf(ttf__minf(curve->p0.y, curve->p1.y), curve->ctrl.y);
    // printf("%d) %f\n", count, ymin);
    count++;
}

void ttf_render_glyph(TTF* font, TTF_uint32 c, TTF_Glyph_Image* image) {
    TTF_uint32 glyphIndex = cmap__get_char_glyph_index(font, c);
    glyf__extract_glyph_curves(font, glyphIndex);

    TTF_uint8* glyphData = glyf__get_glyph_data_block(font, glyphIndex);
    TTF_Point  min       = { ttf__get_int16(glyphData + 2), ttf__get_int16(glyphData + 4) };
    float      xOffset   = min.x < 0.0f ? fabs(min.x) : 0.0f;
    float      yOffset   = min.y < 0.0f ? fabs(min.y) : 0.0f;
    float      height    = fabs(min.y) + fabs(ttf__get_int16(glyphData + 8));
    float      sf        = (float)image->ppem / ttf__get_uint16(font->data + font->head.off + 18);

    {
        #define TTF_X_TO_PIXELS(xVal) sf * (xVal + xOffset)
        #define TTF_Y_TO_PIXELS(yVal) sf * (-(yVal + yOffset) + height)

        TTF_Node* node = font->curves.head;
        
        while (node != NULL) {
            TTF_Curve* curve = ttf__list_get_node_val(&font->curves, node);
            
            curve->p0.x = TTF_X_TO_PIXELS(curve->p0.x);
            curve->p0.y = TTF_Y_TO_PIXELS(curve->p0.y);
            
            curve->p1.x = TTF_X_TO_PIXELS(curve->p1.x);
            curve->p1.y = TTF_Y_TO_PIXELS(curve->p1.y);

            curve->ctrl.x = TTF_X_TO_PIXELS(curve->ctrl.x);
            curve->ctrl.y = TTF_Y_TO_PIXELS(curve->ctrl.y);

            node = node->next;
        }

        #undef TTF_X_TO_PIXELS
        #undef TTF_Y_TO_PIXELS
    }

    for (float y = 0.25f; y <= 1.0f; y += 0.25f) {
        print_curve(ttf__list_get_node_val(&font->curves, font->curves.head));
        TTF_Node* startNode = font->curves.head;

        for (float scanline = y; scanline < image->h; scanline++) {
            if (scanline > height) {
                break;
            }

            TTF_Node* node = font->activeCurves.head;

            while (node != NULL) {
                TTF_Node*         nextNode    = node->next;
                TTF_Active_Curve* activeCurve = ttf__list_get_node_val(&font->activeCurves, node);
                
                if (ttf__get_curve_max_y(activeCurve->curve) <= scanline) {
                    // The bottommost point of the curve is above the scanline so the curve is no 
                    // longer active.
                    ttf__list_remove(&font->activeCurves, node);
                }
                else {
                    // Update the x position where scanline intersects the curve
                    float x0, x1;

                    TTF_uint8 numIntersections = 
                        ttf__get_curve_scanline_x_intersection(activeCurve->curve, &x0, &x1, scanline);

                    // TODO: Handle cases where the scanline intersects the curve twice
                    assert(numIntersections == 1);
                    activeCurve->xIntersect = x0;
                }

                node = nextNode;
            }

            node = startNode;

            while (node != NULL) {
                TTF_Node*  nextNode = node->next;
                TTF_Curve* curve    = ttf__list_get_node_val(&font->curves, node);

                if (ttf__get_curve_min_y(curve) <= scanline) {
                    if (ttf__get_curve_max_y(curve) > scanline) {
                        TTF_Node* activeNode = font->activeCurves.head;

                        float x0, x1;

                        TTF_uint8 numIntersections = 
                            ttf__get_curve_scanline_x_intersection(curve, &x0, &x1, scanline);

                        if (numIntersections == 0) {
                            // The curve is parallel with the x-axis or only the curve's control point 
                            // is above the scanline
                            continue;
                        }

                        // TODO: handle cases where the scanline intersects the curve twice
                        assert(numIntersections == 1);

                        while (activeNode != NULL) {
                            TTF_Active_Curve* activeCurve = 
                                ttf__list_get_node_val(&font->activeCurves, activeNode);

                            if (activeCurve->xIntersect >= x0) {
                                break;
                            }

                            activeNode = activeNode->next;
                        }

                        TTF_Active_Curve* activeCurve = 
                            activeNode == NULL ?
                            ttf__list_insert(&font->activeCurves) :
                            ttf__list_insert_before(&font->activeCurves, activeNode);

                        activeCurve->curve      = curve;
                        activeCurve->xIntersect = x0;
                        // ttf__list_remove(&font->curves, node);
                        startNode = nextNode;
                    }
                    else {
                        startNode = nextNode;
                        // ttf__list_remove(&font->curves, node);
                    }
                }

                node = nextNode;
            }

            if (font->activeCurves.head == NULL) {
                continue;
            }

            node = font->activeCurves.head;
            TTF_int32 windingNumber = 0;

            float last  = ((TTF_Active_Curve*)ttf__list_get_node_val(&font->activeCurves, font->activeCurves.tail))->xIntersect;
            float x     = ((TTF_Active_Curve*)ttf__list_get_node_val(&font->activeCurves, node))->xIntersect;
            float xPrev = floorf(fabs(x));
            if (xPrev == x) {
                float intersect = ((TTF_Active_Curve*)ttf__list_get_node_val(&font->activeCurves, node->next))->xIntersect;
                x = intersect < x + 1.0f ? intersect : x + 1.0f;
            }

            TTF_Active_Curve* activeCurve = ttf__list_get_node_val(&font->activeCurves, node);

            while (xPrev < last) {
                float alpha;
                if (windingNumber != 0) {
                    // Filled
                    alpha = 255.0f * (x - xPrev);
                }
                else if (x != xPrev + 1) {
                    // Not filled
                    alpha = 255.0f * (xPrev + 1 - x);
                }
                else {
                    alpha = 0.0f;
                }

                TTF_uint32 xPix = xPrev;
                TTF_uint32 yPix = floorf(scanline);
                assert(xPix + yPix * image->stride < image->w * image->h);
                image->pixels[xPix + yPix * image->stride] += 0.25f * alpha;

                if (x == activeCurve->xIntersect) {
                    if (node->next == NULL) {
                        break;
                    }
                    windingNumber += activeCurve->curve->p1.y - activeCurve->curve->p0.y < 0.0f ? 1 : -1;
                    node          = node->next;
                    activeCurve   = ttf__list_get_node_val(&font->activeCurves, node);
                }

                xPrev = ceilf(x);
                x = activeCurve->xIntersect < xPrev + 1.0f ? activeCurve->xIntersect : xPrev + 1.0f;
            }
        }

        ttf__list_remove_all(&font->activeCurves);
    }

    // TTF_Node* node = font->curves.head;
    // while (node != NULL) {
    //     TTF_Curve* curve = ttf__list_get_node_val(&font->curves, node);
    //     // float ymax = ttf__maxf(ttf__maxf(curve->p0.y, curve->p1.y), curve->ctrl.y);
    //     // printf("%f\n", ymax);
    //     print_curve(curve);
    //     node = node->next;
    // }

    ttf__list_remove_all(&font->curves);
}

/* ---------------- */
/* Helper functions */
/* ---------------- */
static TTF_uint16 ttf__get_uint16(const TTF_uint8* data) {
    return data[0] << 8 | data[1];
}

static TTF_uint32 ttf__get_uint32(const TTF_uint8* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static TTF_int16 ttf__get_int16(const TTF_uint8* data) {
    return data[0] << 8 | data[1];
}

static float ttf__maxf(float a, float b) {
    return a > b ? a : b;
}

static float ttf__minf(float a, float b) {
    return a < b ? a : b;
}

static float ttf__linear_interp(float p0, float p1, float t) {
    return p0 + t * (p1 - p0);
}

static float ttf__quad_bezier(float p0, float p1, float ctrl, float t) {
    float t1 = 1.0f - t;
    return t1 * t1 * p0 + 2.0f * t1 * t * ctrl + t * t * p1;
}

static float ttf__get_curve_max_y(const TTF_Curve* curve) {
    return ttf__maxf(ttf__maxf(curve->p0.y, curve->p1.y), curve->ctrl.y);
}

static float ttf__get_curve_min_y(const TTF_Curve* curve) {
    return ttf__minf(ttf__minf(curve->p0.y, curve->p1.y), curve->ctrl.y);
}

static void ttf__get_min_and_max(float* min, float* max, float a, float b) {
    if (a < b) {
        *min = a;
        *max = b;
    }
    else {
        *min = b;
        *max = a;
    }
}

static TTF_uint8 ttf__get_curve_scanline_x_intersection(const TTF_Curve* curve, float* x0, float* x1, float scanline) {
    if (curve->ctrl.x == curve->p1.x) {
        if (curve->ctrl.y == curve->p1.y) {
            // The curve is a straight line
            float yMin, yMax;
            ttf__get_min_and_max(&yMin, &yMax, curve->p0.y, curve->p1.y);
            if (scanline < yMin || scanline > yMax) {
                return 0;
            }
            
            float m = (curve->p1.y - curve->p0.y) / (curve->p1.x - curve->p0.x);
            *x0 = (scanline - curve->p0.y) / m + curve->p0.x;
            return 1;
        }
    }

    if (curve->p0.y + curve->p1.y == 2.0f * curve->ctrl.y) {
        if (curve->ctrl.y == curve->p1.y) {
            return 0;
        }

        float t = 
            (scanline - 2.0f * curve->ctrl.y + curve->p1.y) / 
            (2.0f * (curve->p1.y - curve->ctrl.y));

        assert(t >= 0.0f && t <= 1.0f);

        *x0 = ttf__quad_bezier(curve->p0.x, curve->p1.x, curve->ctrl.x, t);
        return 1;
    }

    float rat = 
        -2.0f * scanline * curve->ctrl.y + 
        curve->p0.y * (scanline - curve->p1.y) + 
        scanline * curve->p1.y + curve->ctrl.y * curve->ctrl.y;
    
    if (rat < 0.0f) {
        return 0;
    }

    float denom = curve->p0.y - 2.0f * curve->ctrl.y + curve->p1.y;
    assert(denom != 0.0f);

    float sqrtRat = sqrtf(rat);
    float t0      = -(sqrtRat - curve->p0.y + curve->ctrl.y) / denom;
    float t1      =  (sqrtRat + curve->p0.y - curve->ctrl.y) / denom;
    
    TTF_uint8 count = 0;

    if (t0 >= 0.0f && t0 <= 1.0f) {
        *x0 = ttf__quad_bezier(curve->p0.x, curve->p1.x, curve->ctrl.x, t0);
        count++;
    }

    if (t1 >= 0.0f && t1 <= 1.0f) {
        float x = ttf__quad_bezier(curve->p0.x, curve->p1.x, curve->ctrl.x, t1);
        if (count == 0) {
            *x0 = x;
        }
        else {
            *x1 = x;
        }
        count++;
    }

    return count;
}


/* -------------- */
/* List functions */
/* -------------- */
static TTF_Node* ttf__list_alloc_node(TTF_List* list) {
    TTF_Node* node = NULL;
    
    if (list->reuse == NULL) {
        node = (TTF_Node*)list->mem + list->count;
    }
    else {
        node        = list->reuse;
        list->reuse = list->reuse->next;
    }
   
    node->next = NULL;
    node->prev = NULL;
    list->count++;
    return node;
}

static void* ttf__list_insert(TTF_List* list) {
    TTF_Node* node = ttf__list_alloc_node(list);
    
    if (list->head == NULL) {
        list->head = node;
    }
    else {
        node->prev = list->tail;
        list->tail->next = node;
    }
    list->tail = node;
    
    return ttf__list_get_node_val(list, node);
}

static void* ttf__list_insert_before(TTF_List* list, TTF_Node* node) {
    TTF_Node* newNode = ttf__list_alloc_node(list);

    if (node == list->head) {
        list->head = newNode;
    }
    else {
        node->prev->next = newNode;
        newNode->prev    = node->prev;
    }

    node->prev    = newNode;
    newNode->next = node;

    return ttf__list_get_node_val(list, newNode);
}

static void ttf__list_remove(TTF_List* list, TTF_Node* node) {
    assert(list->count > 0);
    assert(node != NULL);

    if (node != list->head) {
        node->prev->next = node->next;
    }
    else {
        list->head = list->head->next;
    }

    if (node != list->tail) {
        node->next->prev = node->prev;
    }
    else {
        list->tail = list->tail->prev;
    }

    node->next  = list->reuse;
    list->reuse = node;
    list->count--;
}

static void ttf__list_remove_all(TTF_List* list) {
    list->count = 0;
    list->head  = NULL;
    list->tail  = NULL;
    list->reuse = NULL;
}

static void* ttf__list_get_node_val(const TTF_List* list, const TTF_Node* node) {
    size_t idx = node - (TTF_Node*)(list->mem);
    size_t off = sizeof(TTF_Node) * list->cap + list->valSize * idx;
    return list->mem + off;
}

static void* ttf__list_get_value_buffer(const TTF_List* list) {
    return list->mem + sizeof(TTF_Node) * list->cap;
}


/* ------------------------- */
/* Table directory functions */
/* ------------------------- */
static void table_dir__extract_table_info(TTF* font) {
    TTF_uint16 numTables = ttf__get_uint16(font->data + 4);

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
            table->off    = ttf__get_Offset32(record + 8);
            table->size   = ttf__get_uint32(record + 12);
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
        case TTF_MUL:
            ttf__MUL(font);
            return;
    }

    if (ins >= TTF_PUSHB && ins <= TTF_PUSHB_ABC) {
        ttf__PUSHB(font, stream, ins);
        return;
    }
    else if (ins >= TTF_PUSHW && ins <= TTF_PUSHW_ABC) {
        ttf__PUSHW(font, stream, ins);
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

    // TODO: can select multiple pieces of information

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

static void ttf__PUSHW(TTF* font, TTF_IStream* stream, TTF_uint8 ins) {
    TTF_uint32 n = 1 + (ins & 0x07);
    TTF_PRINTF("PUSHW %d\n", n);

    do {
        TTF_uint8 ms  = ttf__istream_next(stream);
        TTF_uint8 ls  = ttf__istream_next(stream);
        TTF_int16 val = (TTF_int16)((ms << 8) | ls);
        TTF_PRINTF("\t%d\n", val);
        ttf__stack_push_int32(font, val);
    } while (--n);
}

static void ttf__MUL(TTF* font) {
    TTF_PRINT("MUL\n");
    TTF_F26Dot6 n1 = ttf__stack_pop_F26Dot6(font);
    TTF_F26Dot6 n2 = ttf__stack_pop_F26Dot6(font);
    ttf__stack_push_F26Dot6(font, (n1 * n2) / TTF_F26DOT6_SF_INV);
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
    TTF_uint16 numTables = ttf__get_uint16(font->data + font->cmap.off + 2);
    
    for (TTF_uint16 i = 0; i < numTables; i++) {
        const TTF_uint8* data = font->data + font->cmap.off + 4 + i * 8;

        TTF_uint16 platformID = ttf__get_uint16(data);
        TTF_uint16 encodingID = ttf__get_uint16(data + 2);
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
            font->encoding.off        = ttf__get_Offset32(data + 4);

            TTF_uint8* subtable = font->data + font->cmap.off + font->encoding.off;
            TTF_uint16 format   = ttf__get_uint16(subtable);
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

    switch (ttf__get_uint16(subtable)) {
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
    // TTF_uint16 searchRange   = ttf__get_uint16(data + 8);
    // TTF_uint16 entrySelector = ttf__get_uint16(data + 10);
    // TTF_uint16 rangeShift    = ttf__get_uint16(data + 12);
    #define CMAP_GET_END_CODE(index) ttf__get_uint16(subtable + 14 + 2 * (index))
    
    TTF_uint16 segCount = ttf__get_uint16(subtable + 6) >> 1;
    TTF_uint16 left     = 0;
    TTF_uint16 right    = segCount - 1;

    while (left <= right) {
        TTF_uint16 mid     = (left + right) / 2;
        TTF_uint16 endCode = CMAP_GET_END_CODE(mid);

        if (endCode >= c) {
            if (mid == 0 || CMAP_GET_END_CODE(mid - 1) < c) {
                TTF_uint32       off            = 16 + 2 * mid;
                const TTF_uint8* idRangeOffsets = subtable + 6 * segCount + off;
                TTF_uint16       idRangeOffset  = ttf__get_uint16(idRangeOffsets);
                TTF_uint16       startCode      = ttf__get_uint16(subtable + 2 * segCount + off);

                if (startCode > c) {
                    return 0;
                }
                
                if (idRangeOffset == 0) {
                    TTF_uint16 idDelta = ttf__get_int16(subtable + 4 * segCount + off);
                    return c + idDelta;
                }

                return ttf__get_uint16(idRangeOffset + 2 * (c - startCode) + idRangeOffsets);
            }
            right = mid - 1;
        }
        else {
            left = mid + 1;
        }
    }

    return 0;

    #undef CMAP_GET_END_CODE
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
#define GLYF_IS_REPEATED_COORD(coord, flags) \
    (!(flags & GLYF_ ## coord ## _SHORT_VECTOR) && (flags & GLYF_ ## coord ## _DUAL))

#define GLYF_READ_COORD_OFF(coord, flags, coordData)                            \
    ((flags & GLYF_ ## coord ## _SHORT_VECTOR) ?                                \
     (flags & GLYF_ ## coord ## _DUAL ? *coordData : -(*coordData)) :           \
     (flags & GLYF_ ## coord ## _DUAL ? 0          : ttf__get_int16(coordData)))\

static TTF_uint8* glyf__get_glyph_data_block(const TTF* font, TTF_uint32 glyphIndex) {
    return font->data + font->glyf.off + loca__get_glyf_block_off(font, glyphIndex);
}

static void glyf__extract_glyph_curves(TTF* font, TTF_uint32 glyphIndex) {
    TTF_uint8* glyphData = glyf__get_glyph_data_block(font, glyphIndex);

    if (ttf__get_int16(glyphData) >= 0) {
        glyf__extract_simple_glyph_curves(font, glyphData);
    }
    else {
        glyf__extract_composite_glyph_curves(font, glyphData);
    }
}

static void glyf__extract_simple_glyph_curves(TTF* font, TTF_uint8* glyphData) {
    Glyf_Simple_Glyph glyph;
    glyf__simple_glyph_init(glyphData, &glyph);

    TTF_int16  numContours   = ttf__get_int16(glyphData);
    TTF_uint16 startPointIdx = 0;

    for (TTF_uint32 i = 0; i < numContours; i++) {
        TTF_Point startPoint;
        glyf__get_next_simple_glyph_point(&glyph, &startPoint);

        TTF_Point  nextP0      = startPoint;
        TTF_uint16 endPointIdx = ttf__get_uint16(glyphData + 10 + 2 * i);

        for (TTF_uint16 j = startPointIdx + 1; j <= endPointIdx; j++) {
            int insertFinalCurve = 0;

            TTF_Curve curve;
            curve.p0 = nextP0;

            if (glyf__get_next_simple_glyph_point(&glyph, &curve.ctrl)) {
                curve.p1         = curve.ctrl;
                insertFinalCurve = j == endPointIdx;
            }
            else if (j == endPointIdx) {
                curve.p1 = startPoint;
            }
            else {
                TTF_Point point;

                if (glyf__peek_next_simple_glyph_point(&glyph, &point)) {
                    curve.p1         = point;
                    insertFinalCurve = ++j == endPointIdx;
                    glyf__get_next_simple_glyph_point(&glyph, &point); // Commit the peek
                }
                else { // Implied on-curve point
                    curve.p1.x = ttf__linear_interp(point.x, curve.ctrl.x, 0.5f);
                    curve.p1.y = ttf__linear_interp(point.y, curve.ctrl.y, 0.5f);
                }
            }

            glyf__insert_simple_glyph_curve(&font->curves, &curve);

            if (insertFinalCurve) {
                curve.p0   = curve.p1;
                curve.p1   = startPoint;
                curve.ctrl = startPoint;
                glyf__insert_simple_glyph_curve(&font->curves, &curve);
            }

            nextP0 = curve.p1;
        }

        startPointIdx = endPointIdx + 1;
    }
}

static void glyf__simple_glyph_init(TTF_uint8* glyphData, Glyf_Simple_Glyph* glyph) {
    TTF_int16  numContours = ttf__get_int16(glyphData);
    TTF_uint32 numPoints   = 1 + ttf__get_uint16(glyphData + 8 + 2 * numContours);
    TTF_uint32 flagsSize   = 0;
    TTF_uint32 xDataSize   = 0;

    memset(glyph, 0, sizeof(Glyf_Simple_Glyph));
    glyph->flagData = glyphData + (10 + 2 * numContours);
    glyph->flagData += 2 + ttf__get_uint16(glyph->flagData);

    for (TTF_uint32 i = 0; i < numPoints;) {
        TTF_uint8 flags       = glyph->flagData[flagsSize];
        TTF_uint8 flagsReps   = (flags & GLYF_REPEAT_FLAG) ? glyph->flagData[flagsSize + 1] : 1;

        TTF_uint8 xSize = 0;
        if (!GLYF_IS_REPEATED_COORD(X, flags)) {
            xSize = (flags & GLYF_X_SHORT_VECTOR) ? 1 : 2;
        }

        flagsSize += (flagsReps == 1 ? 1 : 2);
        i         += flagsReps;

        while (flagsReps > 0) {
            xDataSize += xSize;
            flagsReps--;
        }
    }

    glyph->xData = glyph->flagData + flagsSize;
    glyph->yData = glyph->xData + xDataSize;
}

static void glyf__insert_simple_glyph_curve(TTF_List* curves, const TTF_Curve* curve) {
    TTF_Node* node = curves->head;
    float     yMax = ttf__get_curve_max_y(curve);

    while (node != NULL) {
        TTF_Curve* currentCurve = ttf__list_get_node_val(curves, node);
        
        if (ttf__get_curve_max_y(currentCurve) <= yMax) {
            TTF_Curve* newCurve = ttf__list_insert_before(curves, node);
            *newCurve = *curve;
            return;
        }

        node = node->next;
    }

    TTF_Curve* newCurve = ttf__list_insert(curves);
    *newCurve = *curve;
}

static void glyf__extract_composite_glyph_curves(TTF* font, TTF_uint8* glyphData) {
    // TODO
    assert(0);
}

static int glyf__get_next_simple_glyph_point(Glyf_Simple_Glyph* glyph, TTF_Point* point) {
    TTF_uint8 flags = *glyph->flagData;

    point->x = glyph->absPos.x + GLYF_READ_COORD_OFF(X, flags, glyph->xData);
    point->y = glyph->absPos.y + GLYF_READ_COORD_OFF(Y, flags, glyph->yData);

    if (glyph->flagsReps > 0) {
        glyph->flagsReps--;
        
        if (glyph->flagsReps == 0) {
            glyph->flagData += 2;
        }
    }
    else if (glyph->flagsReps == 0) {
        if (flags & GLYF_REPEAT_FLAG) {
            glyph->flagsReps = glyph->flagData[1];
        }
        else {
            glyph->flagData++;
        }
    }

    if (!GLYF_IS_REPEATED_COORD(X, flags)) {
        glyph->xData += (flags & GLYF_X_SHORT_VECTOR) ? 1 : 2;
    }

    if (!GLYF_IS_REPEATED_COORD(Y, flags)) {
        glyph->yData += (flags & GLYF_Y_SHORT_VECTOR) ? 1 : 2;
    }

    glyph->absPos = *point;

    return flags & GLYF_ON_CURVE_POINT;
}

static int glyf__peek_next_simple_glyph_point(const Glyf_Simple_Glyph* glyph, TTF_Point* point) {
    TTF_uint8 flags = *glyph->flagData;

    point->x = glyph->absPos.x + GLYF_READ_COORD_OFF(X, flags, glyph->xData);
    point->y = glyph->absPos.y + GLYF_READ_COORD_OFF(Y, flags, glyph->yData);
    
    return flags & GLYF_ON_CURVE_POINT;
}

static int glyf__compare_simple_glyph_curves(const void* a, const void* b) {
    return ttf__get_curve_max_y(a) > ttf__get_curve_max_y(b) ? -1 : 1;
}


/* -------------- */
/* loca functions */
/* -------------- */
static TTF_Offset32 loca__get_glyf_block_off(const TTF* font, TTF_uint32 glyphIndex) {
    TTF_int16 version = ttf__get_int16(font->data + font->head.off + 50);

    if (version == 0) {
        // The offset divided by 2 is stored
        return 2 * ttf__get_Offset16(font->data + font->loca.off + (2 * glyphIndex));
    }

    assert(version == 1);
    return ttf__get_Offset32(font->data + font->loca.off + (4 * glyphIndex));
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

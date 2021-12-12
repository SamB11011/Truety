#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "ttf.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main() {
    TTF font;
    if (!ttf_init(&font, "./resources/fonts/Roboto-Regular.ttf")) {
        fprintf(stderr, "Failed to create TTF.\n");
        return 1;
    }

    TTF_Instance instance;
    if (!ttf_instance_init(&font, &instance, 15)) {
        fprintf(stderr, "Failed to create TTF_Instance.\n");
        return 1;
    }

    TTF_Image image;
    if (!ttf_image_init(&image, NULL, 100, 100)) {
        fprintf(stderr, "Failed to create TTF_Image.\n");
        return 1;
    }

    TTF_int32 x = 0;
    TTF_int32 y = 30 + ttf_scale(&instance, font.ascender) + labs(ttf_scale(&instance, font.descender));

    // TODO: make glyph xAdvance to pixel units

    for (int cp = 'A'; cp <= 'B'; cp++) {
        TTF_Glyph glyph;
        ttf_glyph_init(&font, &glyph, ttf_get_glyph_index(&font, cp));

        TTF_V2 pos = { glyph.offset.x + x, y - glyph.offset.y };
        if (!ttf_render_glyph_to_existing_image(&font, &instance, &image, &glyph, pos.x, pos.y)) {
            fprintf(stderr, "Failed to render glyph.\n");
            return 1;
        }

        x += ttf_scale(&instance, glyph.xAdvance);
        fprintf(stderr, "bitmap_top = %d\n", glyph.offset.y);
        fprintf(stderr, "bitmap_left = %d\n", glyph.offset.x);
        fprintf(stderr, "xAdvance = %d\n\n", ttf_scale(&instance, glyph.xAdvance));
    }

    stbi_write_png("./resources/output.png", image.w, image.h, 1, image.pixels, image.w);

    ttf_free_image(&image);
    ttf_free_instance(&font, &instance);
    ttf_free(&font);

    printf("Done\n");

    return 0;
}

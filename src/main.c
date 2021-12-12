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

    TTF_Glyph glyph;
    ttf_glyph_init(&font, &glyph, ttf_get_glyph_index(&font, 'B'));

    if (!ttf_render_glyph_to_existing_image(&font, &instance, &image, &glyph, 50, 50)) {
        fprintf(stderr, "Failed to render glyph.\n");
        return 1;
    }

    stbi_write_png("./resources/output.png", image.w, image.h, 1, image.pixels, image.stride);

    ttf_free_image(&image);
    ttf_free_instance(&font, &instance);
    ttf_free(&font);

    return 0;
}

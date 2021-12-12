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
    if (!ttf_instance_init(&font, &instance, 11)) {
        fprintf(stderr, "Failed to create TTF_Instance.\n");
        return 1;
    }

    TTF_Image image;
    if (!ttf_image_init(&image, NULL, 100, 100)) {
        fprintf(stderr, "Failed to create TTF_Image.\n");
        return 1;
    }

    for (int cp = 'A'; cp <= 'Z'; cp++) {
        TTF_Glyph glyph;
        ttf_glyph_init(&font, &glyph, ttf_get_glyph_index(&font, cp));

        if (!ttf_render_glyph_to_existing_image(&font, &instance, &image, &glyph, 30, 30)) {
            fprintf(stderr, "Failed to render glyph.\n");
            return 1;
        }

        memset(image.pixels, 0, image.w * image.h);
    }

    // stbi_write_png("./resources/output.png", image.w, image.h, 1, image.pixels, image.w);

    ttf_free_image(&image);
    ttf_free_instance(&font, &instance);
    ttf_free(&font);

    return 0;
}

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "truety.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "./external/stb_image_write.h"

#define IMAGE_PATH "./output_image.png"

int main() {
    {
    TTY_Font font;
    if (tty_font_init(&font, "./fonts/Roboto-Regular.ttf")) {
        goto failure;
    }

    TTY_Instance instance;
    if (tty_instance_init(&font, &instance, 18, TTY_INSTANCE_DEFAULT)) {
        goto failure;
    }

    TTY_Image image;
    if (tty_image_init(&image, NULL, 512, 512)) {
        goto failure;
    }

    TTY_U32 x = 0, y = 0;

    for (char c = ' '; c <= '~'; c++) {
        printf("Rendering %c at (%d, %d)\n", (char)c, (int)x, (int)y);

        TTY_U32 glyphIdx;
        if (tty_get_glyph_index(&font, c, &glyphIdx)) {
            goto failure;
        }
        
        TTY_Glyph glyph;
        if (tty_glyph_init(&font, &glyph, glyphIdx)) {
            goto failure;
        }
        
        if (tty_render_glyph_to_existing_image(&font, &instance, &glyph, &image, x, y)) {
            goto failure;
        }
        
        if (glyph.size.x != 0) {
            x += instance.maxGlyphSize.x;
            if (x + instance.maxGlyphSize.x > image.size.x) {
                x = 0;
                y += instance.maxGlyphSize.y;
            }
        }
    }

    stbi_write_png(IMAGE_PATH, image.size.x, image.size.y, 1, image.pixels, image.size.x);
    printf("Result saved as %s\n", IMAGE_PATH);

    tty_image_free(&image);
    tty_instance_free(&instance);
    tty_font_free(&font);
    }
    return 0;

failure:
    fprintf(stderr, "An error occurred");
    return 1;
}

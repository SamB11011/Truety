#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "truety.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main() {
    TTY font;
    if (!tty_init(&font, "./resources/fonts/LiberationSans-Regular.ttf")) {
        fprintf(stderr, "Failed to load font\n");
        return 1;
    }

    TTY_Instance instance;
    if (!tty_instance_init(&font, &instance, 16)) {
        fprintf(stderr, "Failed to create an instance\n");
        return 1;
    }

    for (int cp = ' '; cp <= '~'; cp++) {
        if (cp == ';' || cp == ':') {
            // TODO: Handle composite glyphs
            continue;
        }

        printf("Rendering %c\n", cp);

        TTY_Glyph glyph;
        tty_glyph_init(&font, &glyph, tty_get_glyph_index(&font, cp));

        TTY_Image image;
        if (!tty_render_glyph(&font, &instance, &image, &glyph)) {
            fprintf(stderr, "Failed to render glyph\n");
            return 1;
        }

        tty_free_image(&image);
    }

    // stbi_write_png("./resources/output.png", image.w, image.h, 1, image.pixels, image.w);

    tty_free_instance(&font, &instance);
    tty_free(&font);

    printf("Done\n");

    return 0;
}

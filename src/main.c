#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "truety.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static void test_font(const char* path) {
    printf("%s\n", path);

    TTY font;
    if (!tty_init(&font, path)) {
        fprintf(stderr, "Failed to load font\n");
        exit(1);
    }

    for (int ppem = 14; ppem <= 70; ppem++) {
        printf("ppem = %d\n", ppem);

        TTY_Instance instance;
        if (!tty_instance_init(&font, &instance, ppem, TTY_TRUE)) {
            fprintf(stderr, "Failed to create an instance\n");
            exit(1);
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
            if (!tty_render_glyph(&font, &instance, &glyph, &image)) {
                fprintf(stderr, "Failed to render glyph\n");
                exit(1);
            }

            tty_image_free(&image);
        }

        tty_instance_free(&instance);
    }

    tty_free(&font);
}

static void save_glyph(const char* path, int cp, int ppem) {
    printf("%s\n", path);

    TTY font;
    if (!tty_init(&font, path)) {
        fprintf(stderr, "Failed to load font\n");
        exit(1);
    }

    printf("ppem = %d\n", ppem);

    TTY_Instance instance;
    if (!tty_instance_init(&font, &instance, ppem, TTY_TRUE)) {
        fprintf(stderr, "Failed to create an instance\n");
        exit(1);
    }

    printf("Rendering %c\n", cp);

    TTY_Glyph glyph;
    tty_glyph_init(&font, &glyph, tty_get_glyph_index(&font, cp));

    TTY_Image image;
    if (!tty_render_glyph(&font, &instance, &glyph, &image)) {
        fprintf(stderr, "Failed to render glyph\n");
        exit(1);
    }

    stbi_write_png("./resources/output.png", image.w, image.h, 1, image.pixels, image.w);

    tty_image_free(&image);
    tty_instance_free(&instance);
    tty_free(&font);
}

int main() {
    // test_font("./resources/fonts/Roboto-Regular.ttf");
    // test_font("./resources/fonts/LiberationSans-Regular.ttf");
    // test_font("./resources/fonts/BakbakOne-Regular.ttf");
    save_glyph("./resources/fonts/Roboto-Regular.ttf", 'g', 25);
    printf("Done\n");
    return 0;
}

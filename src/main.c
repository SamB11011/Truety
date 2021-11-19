#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "ttf.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main() {
    TTF font;
    ttf_init(&font, "./resources/fonts/Roboto-Regular.ttf");

    TTF_Glyph_Image image;
    image.w      = 100;
    image.h      = 100;
    image.stride = 100;
    image.ppem   = 60;
    image.pixels = calloc(image.w * image.h, 1);
    assert(image.pixels);
    
    for (int i = 0; i < 10000; i++) {
        for (int c = 'A'; c <= 'Z'; c++) {
            ttf_render_glyph(&font, c, &image);
        }
    }

    printf("DONE\n");
    
    // stbi_write_png("./output.png", image.w, image.h, 1, image.pixels, image.stride);
    return 0;
}

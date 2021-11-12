#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "ttf.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main() {
    const size_t SIZE = 13;
    TTF_uint8* pixels = malloc(SIZE * SIZE);
    assert(pixels);

    TTF font;
    ttf_init(&font, "./resources/fonts/Roboto-Regular.ttf");

    TTF_Glyph_Image image;
    image.w      = 100;
    image.h      = 100;
    image.stride = 100;
    image.ppem   = 75;
    image.pixels = calloc(image.w * image.h, 1);
    assert(image.pixels);

    // double average = 0;

    #define COUNT 1
    //Average = 0.000046 seconds

    for (int i = 0; i < COUNT; i++) {
        // clock_t begin = clock();

        for (int c = 'a'; c <= 'a'; c++) {
            ttf_render_glyph(&font, c, &image);
        }

        // average += (double)(clock() - begin) / CLOCKS_PER_SEC;
    }

    // average /= COUNT;
    // printf("Average = %f seconds\n", average);
    
    stbi_write_png("./output_2.png", image.w, image.h, 1, image.pixels, image.stride);
    return 0;
}

// 449
// 582
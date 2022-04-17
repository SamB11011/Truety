#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "truety.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "./external/stb_image_write.h"

#define IMAGE_PATH "./output_image.png"

int main() {
    TTY_Font font;
    if (tty_font_init(&font, "./fonts/Roboto-Regular.ttf")) {
        goto failure;
    }

    TTY_Instance instance;
    if (tty_instance_init(&font, &instance, 18, TTY_INSTANCE_DEFAULT)) {
        goto failure;
    }

    TTY_Atlas_Cache cache;
    if (tty_atlas_cache_init(&instance, &cache, 128, 128)) {
        goto failure;
    }

    for (char c = ' '; c <= '~'; c++) {
        if (tty_atlas_cache_is_full(&cache)) {
            printf("The cache is full, replacing %c with %c\n", cache.lruTail->codePoint, c);
        }
        else {
            printf("Adding %c to the cache\n", c);
        }

        TTY_Atlas_Cache_Entry entry;
        if (tty_atlas_cache_get_entry(&font, &instance, &cache, &entry, c)) {
            goto failure;
        }
    }

    stbi_write_png(IMAGE_PATH, cache.atlas.size.x, cache.atlas.size.y, 1, cache.atlas.pixels, cache.atlas.size.x);
    printf("Result saved as %s\n", IMAGE_PATH);

    tty_atlas_cache_free(&cache);
    tty_instance_free(&instance);
    tty_font_free(&font);

    return 0;

failure:
    fprintf(stderr, "An error occurred");
    return 1;
}

/* ----------------------------------------------------------------------- */
/* This demo shows Truety's texture atlas caching functionality            */
/*                                                                         */
/* NOTE: This demo is incomplete and doesn't demonstrate the functionality */
/*       very well yet                                                     */
/* ----------------------------------------------------------------------- */

#include <stdio.h>
#include "truety.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

#define PPEM      30
#define FONT_PATH "./external/Roboto-Regular.ttf"

int main() {
    TTY_Font font;
    if (!tty_init(&font, FONT_PATH)) {
        fprintf(stderr, "Failed to load the font\n");
        exit(1);
    }
    
    TTY_Instance instance;
    if (!tty_instance_init(&font, &instance, PPEM, TTY_INSTANCE_DEFAULT)) {
        fprintf(stderr, "Failed to create an instance of the font\n");
        exit(1);
    }
    
    TTY_Atlas_Cache cache;
    if (!tty_atlas_cache_init(&instance, &cache, 256, 256)) {
        fprintf(stderr, "Failed to create a cache for the font\n");
        exit(1);
    }

    // Gets a cache entry. If there is no cache entry, one will be created and
    // then returned.
    for (TTY_uint32 cp = ' '; cp <= '~'; cp++) {
        TTY_Cache_Entry entry;
        TTY_bool        wasCached;
        if (!tty_get_atlas_cache_entry(&font, &instance, &cache, &entry, &wasCached, cp)) {
            fprintf(stderr, "Failed to get cache entry\n");
            exit(1);
        }
    }
    
    stbi_write_png(
        "./output.png", cache.atlas.size.x, cache.atlas.size.y, 1, cache.atlas.pixels, 
        cache.atlas.size.x);
    
    tty_atlas_cache_free(&cache);
    tty_instance_free(&instance);
    tty_free(&font);
    
    printf("Finished successfully\n");
    return 0;
}

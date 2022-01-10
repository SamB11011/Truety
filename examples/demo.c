/* ------------------------------------------------------------------ */
/* This demo shows how to generate a glyph texture atlas using Truety */
/* ------------------------------------------------------------------ */

#include <stdio.h>
#include "truety.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

/* Pixels Per EM */
#define PPEM 18

/* The size of the texture atlas */
#define IMAGE_W 256
#define IMAGE_H 256

/* The range of characters that will go in the atlas */
#define FIRST_CHAR ' '
#define LAST_CHAR  '~'

/* The path to the font file */
#define FONT_PATH "./external/Roboto-Regular.ttf"

int main() {
    // Load the font
    TTY font;
    if (!tty_init(&font, FONT_PATH)) {
        fprintf(stderr, "Failed to load the font\n");
        exit(1);
    }
    
    // Create an instance of the font
    // An instance corresponds to a single font at a single size
    TTY_Instance instance;
    if (!tty_instance_init(&font, &instance, PPEM, TTY_INSTANCE_DEFAULT)) {
        fprintf(stderr, "Failed to create an instance of the font\n");
        exit(1);
    }
    
    // Create an image, this is what glyphs will be rendered to
    TTY_Image image;
    if (!tty_image_init(&image, NULL, IMAGE_W, IMAGE_H)) {
        fprintf(stderr, "Failed to create an image\n");
        exit(1);
    }
    
    // The position of the current glyph in the texture atlas
    TTY_uint32 x = 0;
    TTY_uint32 y = 0;
    
    // Create a texture atlas containing all characters in the specified range
    for (int c = FIRST_CHAR; c <= LAST_CHAR; c++) {
        // Map the character to its corresponding glyph index
        TTY_uint32 glyphIdx = tty_get_glyph_index(&font, c);
        
        // Create a glyph object
        TTY_Glyph glyph;
        tty_glyph_init(&glyph, glyphIdx);
        
        // Render the glyph into the texture atlas
        if (!tty_render_glyph_to_existing_image(&font, &instance, &glyph, &image, x, y)) {
            fprintf(stderr, "Failed to render glyph\n");
            exit(1);
        }
        
        // If the glyph isn't empty, increase the position for the next glyph
        // (The space character will produce an emtpy glyph, for example)
        if (glyph.size.x != 0) {
            x += instance.maxGlyphSize.x;
            if (x + instance.maxGlyphSize.x > image.w) {
                x = 0;
                y += instance.maxGlyphSize.y;
            }
        }
    }
    
    // Save the texture atlas to an image named "atlas.png"
    stbi_write_png("./atlas.png", image.w, image.h, 1, image.pixels, image.w);
    
    // Clean up
    tty_image_free(&image);
    tty_instance_free(&instance);
    tty_free(&font);
    
    printf("Finished successfully\n");
    return 0;
}

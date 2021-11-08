#include <stdlib.h>
#include <assert.h>
#include "ttf.h"

// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include "stb_image_write.h"

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
	image.ppem   = 50;
	image.pixels = malloc(image.w * image.h);
	assert(image.pixels);
	ttf_render_glyph(&font, 'E', &image);

	// stbi_write_png("./output.png", SIZE, SIZE, 1, SIZE, pixels);
	return 0;
}

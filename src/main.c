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
	ttf_render_glyph(&font, 'B', NULL);

	// stbi_write_png("./output.png", SIZE, SIZE, 1, SIZE, pixels);
	return 0;
}

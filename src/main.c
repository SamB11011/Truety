#include "ttf.h"

int main() {
	TTF font;
	ttf_init(&font, "./resources/fonts/Roboto-Regular.ttf");
	return 0;
}

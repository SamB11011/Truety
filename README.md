# Truety
A small TrueType font rendering library.

![](./images/demo_image.png)
# Features
- Hinting
  - The bytecode interpreter (hinter) is designed to match the results produced by FreeType's V40 Interpreter (with backward compatibility enabled).
- Small and easy to use
  - Consists of a single header file and a single source file.
  - No dependencies (besides the C standard library).
  - Should compile with any C99 compiler.
- Supports TrueType (.ttf) files and OpenType (.otf) files that contain TrueType outlines.
# Limitations
- Truety is still under development and won't work for ALL supported fonts until the following tasks are completed
  - Finish implementing bytecode instructions. Approximately 75% of instructions are implemented.
  - Handle all supported *cmap* table encodings and formats.
  - Use the *vmtx* table for vertical metrics when applicable.
- Limited error checking
  - As of right now, this library does not adequately validate the integrity of font files. For this reason, you should only use it with font files you trust.
- Unicode is the only supported character encoding.
# Planned Features
- Robust error handling
- Glyph rotation and stretching
- Underlining, subscript and superscript
- Subpixel rendering
- Kerning
  - Provided by the *kern* table
- Support for OpenType text layout features
  - Provided by the *GSUB*, *GPOS*, *BASE*, *JSTF*, and *GDEF* tables
- Support for OpenType variable fonts
- Support for TrueType Collection (.ttc) files
# Acknowledgements
[FreeType](https://freetype.org/) and [stb_truetype](https://github.com/nothings/stb/blob/master/stb_truetype.h) are two other font rendering libraries that served as great references when building Truety. Thank you very much.

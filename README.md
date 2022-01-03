# Truety
Truety is a lightweight TrueType font rendering library.
# Features
- Hinting
  - The bytecode interpreter (hinter) is designed to match the results produced by FreeType's V40 Interpreter (with backward compatibility enabled).
- Small and easy to use
  - Consists of a single header file and a single source file.
  - No dependencies (besides the C standard library).
  - Should compile with any C99 compiler.
- Supports TrueType (.ttf) files and OpenType (.otf) files that contain TrueType outlines.
# Limitations
- Limited error checking
  - As of right now, this library does not adequately validate the integrity of font files. For this reason, you should only use it with font files you trust.
- Not all bytecode instructions are implemented yet
  - Approximately 75% of instructions are implemented. Finishing this is at the top of the to-do list.
- Unicode is the only supported character encoding.
# Planned Features
- Robust error handling
- Glyph rotation and stretching
- Subpixel rendering
- Kerning
  - Provided by the *kern* table
- Support for OpenType text layout features
  - Provided by the *GSUB*, *GPOS*, *BASE*, *JSTF*, and *GDEF* tables
- Support for OpenType variable fonts
- Support for TrueType Collection (.ttc) files
# Acknowledgements

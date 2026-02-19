#include "text.h"
#include <stdlib.h>
#include <string.h>

// A very minimal 8x8 font for ASCII 32-127 (just a few characters for demo)
// In a real app, you'd use a font atlas or FreeType.
static const uint8_t font8x8_basic[128][8] = {
    ['H'] = {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    ['e'] = {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},
    ['l'] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1C, 0x00},
    ['o'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},
    ['W'] = {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    ['r'] = {0x00, 0x00, 0x36, 0x3C, 0x30, 0x30, 0x78, 0x00},
    ['d'] = {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00},
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ... add more if needed, but these are enough for "Hello World"
};

void text_generate_texture(const char* text, uint8_t** data, int* width, int* height) {
    int len = strlen(text);
    *width = len * 8;
    *height = 8;
    *data = calloc((*width) * (*height), 1);

    for (int i = 0; i < len; i++) {
        char c = text[i];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8_basic[(int)c][row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1 << col)) {
                    (*data)[row * (*width) + (i * 8 + col)] = 255;
                }
            }
        }
    }
}

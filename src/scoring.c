#include "scoring.h"
#include "gt/feature/text/text.h"
#include <stdio.h>
#include <stdint.h>
#include "gen/assets/font.h"
#include "gt/banking.h"
#include "gen/bank_nums.h"

extern const unsigned char decimal_conversion_table[100]; // defined in text.c

void fast_uint_to_string_v2(unsigned int num, char* buffer) {
    unsigned char upper = 0;
    unsigned char lower;
    
    // Still need loops for the 100s/1000s since the table only goes to 99
    buffer[0] = '0';
    while (num >= 1000) { num -= 1000; buffer[0]++; }
    
    buffer[1] = '0';
    while (num >= 100) { num -= 100; buffer[1]++; }
    
    // Now 'num' is between 0 and 99.
    // Use the GameTank's built-in table to get the last two digits instantly!
    push_rom_bank();
    change_rom_bank(BANK_PROG0);
    lower = decimal_conversion_table[(unsigned char)num];
    pop_rom_bank();

    buffer[2] = (lower >> 4) + '0';   // Tens place
    buffer[3] = (lower & 0x0F) + '0'; // Ones place
    buffer[4] = '\0';
}

void print_scores(int num, char xpos) {
    char buffer[5]; // Allocate actual memory for "0000\0"
    text_color = TEXT_COLOR_WHITE;
    text_cursor_y = 12;
    text_cursor_x = xpos;

    // Cap the score based on your digit count
    #if SCORING_DIGITS == 3
        if (num > 999) num = 999;
        fast_uint_to_string_v2(num, buffer);
        text_print_string(buffer + 1); // Start at index 1 to ignore the 1000s digit
    #else
        if (num > 9999) num = 9999;
        fast_uint_to_string_v2(num, buffer);
        text_print_string(buffer);
    #endif
}
void scoring_init()
{
    allocate_sprite(&ASSET__font__breakoutFont_bmp_load_list);
    text_init();
}

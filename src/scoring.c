#include "scoring.h"
#include "gt/feature/text/text.h"
#include <stdio.h>
#include <stdint.h>
#include "gen/assets/font.h"

void fast_uint_to_string(unsigned int num, char* buffer) {
    // For a 4-digit score
    buffer[0] = '0';
    while (num >= 1000) { num -= 1000; buffer[0]++; }
    
    buffer[1] = '0';
    while (num >= 100) { num -= 100; buffer[1]++; }
    
    buffer[2] = '0';
    while (num >= 10) { num -= 10; buffer[2]++; }
    
    buffer[3] = '0' + (char)num;
    buffer[4] = '\0';
}

void print_scores(int num, char xpos)
{
    char buffer = "";
    //text_init();
    text_color = TEXT_COLOR_WHITE;
    text_cursor_y = 11;
    text_cursor_x = xpos;//5
    #if SCORING_DIGITS==3
        if (num > 999) text_print_string("999");
        else
        {
            if (num < 10) text_print_string("00");
            else if (num < 100) text_print_string("0");
            //else if (num < 1000) text_print_string("0");
            uint_to_string(num,buffer);
            text_print_string(buffer);
        }
    #else //if SCORING_DIGITS==4
        if (num > 9999) text_print_string("9999");
        else
        {
            fast_uint_to_string(num,buffer);
            text_print_string(buffer);
        }
    #endif
}
void scoring_init()
{
    //text_load_font();
    allocate_sprite(&ASSET__font__breakoutFont_bmp_load_list);
    text_init();
}

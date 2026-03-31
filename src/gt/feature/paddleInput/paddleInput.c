#include "../../input.h"
#include "paddleInput.h"
#include "../../../gen/modules_enabled.h"
#include "../../gametank.h"

#ifdef ENABLE_MODULE_PADDLEINPUT

#pragma optimize (push, off)
void update_paddle_inputs(){
    char inputsA, inputsB;
    inputsA = *gamepad_2;
    inputsA = *gamepad_1;
    inputsB = *gamepad_1;

    player1_old_buttons = player1_buttons;
    player1_buttons = ~((((int) inputsB) << 8) | inputsA);
    player1_buttons &= INPUT_MASK_PADDLE_ALL_KEYS;
    player1_new_buttons = player1_buttons & ~player1_old_buttons;

    inputsA = *gamepad_2;
    inputsB = *gamepad_2;
    player2_old_buttons = player2_buttons;
    player2_buttons = ~((((int) inputsB) << 8) | inputsA);
    player2_buttons &= INPUT_MASK_PADDLE_ALL_KEYS;
    player2_new_buttons = player2_buttons & ~player2_old_buttons;
}
#pragma optimize (pop)

#endif
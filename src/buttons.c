#include "main.h"

void button_update(int pressed, int duration, struct button_state *state)
{
    if (pressed == state->pressed) {
        state->duration += duration;
    } else {
        state->pressed = pressed;
        state->duration = duration;
    }
}

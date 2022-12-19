/*
 * Open Surge Engine
 * mobile_gamepad.h - virtual gamepad for mobile devices
 * Copyright (C) 2008-2022  Alexandre Martins <alemartf@gmail.com>
 * http://opensurge2d.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MOBILEGAMEPAD_H
#define _MOBILEGAMEPAD_H

#include <stdint.h>

enum {
    MOBILEGAMEPAD_DPAD_CENTER = 0,
    MOBILEGAMEPAD_DPAD_RIGHT  = 1,
    MOBILEGAMEPAD_DPAD_UP     = 1 << 1,
    MOBILEGAMEPAD_DPAD_LEFT   = 1 << 2,
    MOBILEGAMEPAD_DPAD_DOWN   = 1 << 3
};

enum {
    MOBILEGAMEPAD_BUTTON_NONE   = 0,
    MOBILEGAMEPAD_BUTTON_ACTION = 1,
    MOBILEGAMEPAD_BUTTON_BACK   = 1 << 1
};

/* state of the mobile gamepad */
typedef struct mobilegamepad_state_t mobilegamepad_state_t;
struct mobilegamepad_state_t {

    /* D-Pad flags */
    uint8_t dpad;

    /* button flags */
    uint8_t buttons;

};

/* public API */
void mobilegamepad_init();
void mobilegamepad_release();
void mobilegamepad_update();
void mobilegamepad_render();

void mobilegamepad_get_state(mobilegamepad_state_t* state);

void mobilegamepad_fadein();
void mobilegamepad_fadeout();

#endif
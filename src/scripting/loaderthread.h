/*
 * Open Surge Engine
 * loaderthread.h - scripting system: SurgeScript loader thread
 * Copyright (C) 2008-2023  Alexandre Martins <alemartf@gmail.com>
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

#ifndef _LOADERTHREAD_H
#define _LOADERTHREAD_H

#include <allegro5/allegro.h>

ALLEGRO_THREAD* surgescriptloaderthread_create(int argc, const char** argv);
ALLEGRO_THREAD* surgescriptloaderthread_destroy(ALLEGRO_THREAD* thread);

#endif
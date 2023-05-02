/*
 * Open Surge Engine
 * renderqueue.c - render queue
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

#include <allegro5/allegro.h>
#include <surgescript.h>
#include <string.h>
#include <math.h>
#include "renderqueue.h"
#include "particle.h"
#include "player.h"
#include "brick.h"
#include "actor.h"
#include "background.h"
#include "legacy/item.h"
#include "legacy/enemy.h"
#include "../core/logfile.h"
#include "../core/util.h"
#include "../core/stringutil.h"
#include "../core/video.h"
#include "../core/image.h"
#include "../scenes/level.h"
#include "../scripting/scripting.h"



/* the types of renderables */
enum {
    TYPE_PLAYER,

    TYPE_BRICK,
    TYPE_BRICK_MASK,
    TYPE_BRICK_DEBUG,
    TYPE_BRICK_PATH,
    TYPE_PARTICLE,

    TYPE_SSOBJECT,
    TYPE_SSOBJECT_GIZMO,
    TYPE_SSOBJECT_DEBUG,

    TYPE_BACKGROUND,
    TYPE_FOREGROUND,
    TYPE_WATER,

    TYPE_ITEM, /* legacy item */
    TYPE_OBJECT /* legacy object */
};

/* a renderable */
typedef union renderable_t renderable_t;
union renderable_t {
    player_t* player;
    brick_t* brick;
    item_t* item;
    object_t* object; /* legacy object */
    surgescript_object_t* ssobject;
    bgtheme_t* theme;
    void* dummy;
};

/* a vtable used for rendering different types of entities */
typedef struct renderable_vtable_t renderable_vtable_t;
struct renderable_vtable_t {
    void (*render)(renderable_t,v2d_t);
    float (*zindex)(renderable_t);
    int (*ypos)(renderable_t);
    const char* (*path)(renderable_t, char*, size_t);
    int (*type)(renderable_t);
    bool (*is_translucent)(renderable_t);
};

/* an entry of the render queue */
typedef struct renderqueue_entry_t renderqueue_entry_t;
struct renderqueue_entry_t {

    renderable_t renderable;
    const renderable_vtable_t* vtable;

    int group_index; /* a helper for deferred rendering; see my commentary about it below */
    int zorder;

    struct {
        float zindex;
        int type;
        int ypos;
        bool is_translucent;
    } cached;

#if defined(__GNUC__)
} __attribute__((aligned));
#else
};
#endif

/* vtables */
static float zindex_particles(renderable_t r);
static float zindex_player(renderable_t r);
static float zindex_item(renderable_t r);
static float zindex_object(renderable_t r);
static float zindex_brick(renderable_t r);
static float zindex_brick_mask(renderable_t r);
static float zindex_brick_debug(renderable_t r);
static float zindex_brick_path(renderable_t r);
static float zindex_ssobject(renderable_t r);
static float zindex_ssobject_gizmo(renderable_t r);
static float zindex_ssobject_debug(renderable_t r);
static float zindex_background(renderable_t r);
static float zindex_foreground(renderable_t r);
static float zindex_water(renderable_t r);

static void render_particles(renderable_t r, v2d_t camera_position);
static void render_player(renderable_t r, v2d_t camera_position);
static void render_item(renderable_t r, v2d_t camera_position);
static void render_object(renderable_t r, v2d_t camera_position);
static void render_brick(renderable_t r, v2d_t camera_position);
static void render_brick_mask(renderable_t r, v2d_t camera_position);
static void render_brick_debug(renderable_t r, v2d_t camera_position);
static void render_brick_path(renderable_t r, v2d_t camera_position);
static void render_ssobject(renderable_t r, v2d_t camera_position);
static void render_ssobject_gizmo(renderable_t r, v2d_t camera_position);
static void render_ssobject_debug(renderable_t r, v2d_t camera_position);
static void render_background(renderable_t r, v2d_t camera_position);
static void render_foreground(renderable_t r, v2d_t camera_position);
static void render_water(renderable_t r, v2d_t camera_position);

static int ypos_particles(renderable_t r);
static int ypos_player(renderable_t r);
static int ypos_item(renderable_t r);
static int ypos_object(renderable_t r);
static int ypos_brick(renderable_t r);
static int ypos_brick_mask(renderable_t r);
static int ypos_brick_debug(renderable_t r);
static int ypos_brick_path(renderable_t r);
static int ypos_ssobject(renderable_t r);
static int ypos_ssobject_gizmo(renderable_t r);
static int ypos_ssobject_debug(renderable_t r);
static int ypos_background(renderable_t r);
static int ypos_foreground(renderable_t r);
static int ypos_water(renderable_t r);

static const char* path_particles(renderable_t r, char* dest, size_t dest_size);
static const char* path_player(renderable_t r, char* dest, size_t dest_size);
static const char* path_item(renderable_t r, char* dest, size_t dest_size);
static const char* path_object(renderable_t r, char* dest, size_t dest_size);
static const char* path_brick(renderable_t r, char* dest, size_t dest_size);
static const char* path_brick_mask(renderable_t r, char* dest, size_t dest_size);
static const char* path_brick_debug(renderable_t r, char* dest, size_t dest_size);
static const char* path_brick_path(renderable_t r, char* dest, size_t dest_size);
static const char* path_ssobject(renderable_t r, char* dest, size_t dest_size);
static const char* path_ssobject_gizmo(renderable_t r, char* dest, size_t dest_size);
static const char* path_ssobject_debug(renderable_t r, char* dest, size_t dest_size);
static const char* path_background(renderable_t r, char* dest, size_t dest_size);
static const char* path_foreground(renderable_t r, char* dest, size_t dest_size);
static const char* path_water(renderable_t r, char* dest, size_t dest_size);

static int type_particles(renderable_t r);
static int type_player(renderable_t r);
static int type_item(renderable_t r);
static int type_object(renderable_t r);
static int type_brick(renderable_t r);
static int type_brick_mask(renderable_t r);
static int type_brick_debug(renderable_t r);
static int type_brick_path(renderable_t r);
static int type_ssobject(renderable_t r);
static int type_ssobject_gizmo(renderable_t r);
static int type_ssobject_debug(renderable_t r);
static int type_background(renderable_t r);
static int type_foreground(renderable_t r);
static int type_water(renderable_t r);

static bool is_translucent_particles(renderable_t r);
static bool is_translucent_player(renderable_t r);
static bool is_translucent_item(renderable_t r);
static bool is_translucent_object(renderable_t r);
static bool is_translucent_brick(renderable_t r);
static bool is_translucent_brick_mask(renderable_t r);
static bool is_translucent_brick_debug(renderable_t r);
static bool is_translucent_brick_path(renderable_t r);
static bool is_translucent_ssobject(renderable_t r);
static bool is_translucent_ssobject_gizmo(renderable_t r);
static bool is_translucent_ssobject_debug(renderable_t r);
static bool is_translucent_background(renderable_t r);
static bool is_translucent_foreground(renderable_t r);
static bool is_translucent_water(renderable_t r);

static const renderable_vtable_t VTABLE[] = {
    [TYPE_BRICK] = {
        .zindex = zindex_brick,
        .render = render_brick,
        .ypos = ypos_brick,
        .path = path_brick,
        .type = type_brick,
        .is_translucent = is_translucent_brick
    },

    [TYPE_BRICK_MASK] = {
        .zindex = zindex_brick_mask,
        .render = render_brick_mask,
        .ypos = ypos_brick_mask,
        .path = path_brick_mask,
        .type = type_brick_mask,
        .is_translucent = is_translucent_brick_mask
    },

    [TYPE_BRICK_DEBUG] = {
        .zindex = zindex_brick_debug,
        .render = render_brick_debug,
        .ypos = ypos_brick_debug,
        .path = path_brick_debug,
        .type = type_brick_debug,
        .is_translucent = is_translucent_brick_debug
    },

    [TYPE_BRICK_PATH] = {
        .zindex = zindex_brick_path,
        .render = render_brick_path,
        .ypos = ypos_brick_path,
        .path = path_brick_path,
        .type = type_brick_path,
        .is_translucent = is_translucent_brick_path
    },

    [TYPE_ITEM] = {
        .zindex = zindex_item,
        .render = render_item,
        .ypos = ypos_item,
        .path = path_item,
        .type = type_item,
        .is_translucent = is_translucent_item
    },

    [TYPE_OBJECT] = {
        .zindex = zindex_object,
        .render = render_object,
        .ypos = ypos_object,
        .path = path_object,
        .type = type_object,
        .is_translucent = is_translucent_object
    },

    [TYPE_PLAYER] = {
        .zindex = zindex_player,
        .render = render_player,
        .ypos = ypos_player,
        .path = path_player,
        .type = type_player,
        .is_translucent = is_translucent_player
    },

    [TYPE_PARTICLE] = {
        .zindex = zindex_particles,
        .render = render_particles,
        .ypos = ypos_particles,
        .path = path_particles,
        .type = type_particles,
        .is_translucent = is_translucent_particles
    },

    [TYPE_SSOBJECT] = {
        .zindex = zindex_ssobject,
        .render = render_ssobject,
        .ypos = ypos_ssobject,
        .path = path_ssobject,
        .type = type_ssobject,
        .is_translucent = is_translucent_ssobject
    },

    [TYPE_SSOBJECT_DEBUG] = {
        .zindex = zindex_ssobject_debug,
        .render = render_ssobject_debug,
        .ypos = ypos_ssobject_debug,
        .path = path_ssobject_debug,
        .type = type_ssobject_debug,
        .is_translucent = is_translucent_ssobject_debug
    },

    [TYPE_SSOBJECT_GIZMO] = {
        .zindex = zindex_ssobject_gizmo,
        .render = render_ssobject_gizmo,
        .ypos = ypos_ssobject_gizmo,
        .path = path_ssobject_gizmo,
        .type = type_ssobject_gizmo,
        .is_translucent = is_translucent_ssobject_gizmo
    },

    [TYPE_BACKGROUND] = {
        .zindex = zindex_background,
        .render = render_background,
        .ypos = ypos_background,
        .path = path_background,
        .type = type_background,
        .is_translucent = is_translucent_background
    },

    [TYPE_FOREGROUND] = {
        .zindex = zindex_foreground,
        .render = render_foreground,
        .ypos = ypos_foreground,
        .path = path_foreground,
        .type = type_foreground,
        .is_translucent = is_translucent_foreground
    },

    [TYPE_WATER] = {
        .zindex = zindex_water,
        .render = render_water,
        .ypos = ypos_water,
        .path = path_water,
        .type = type_water,
        .is_translucent = is_translucent_water
    }
};

/* shaders */
static const char vs_glsl[] = ""
    "#define a_position " ALLEGRO_SHADER_VAR_POS "\n"
    "#define a_color " ALLEGRO_SHADER_VAR_COLOR "\n"
    "#define a_texcoord " ALLEGRO_SHADER_VAR_TEXCOORD "\n"
    "#define projview " ALLEGRO_SHADER_VAR_PROJVIEW_MATRIX "\n"
    "#define use_texmatrix " ALLEGRO_SHADER_VAR_USE_TEX_MATRIX "\n"
    "#define texmatrix " ALLEGRO_SHADER_VAR_TEX_MATRIX "\n"
    "\n"
    "attribute vec4 a_position;\n"
    "attribute vec4 a_color;\n"
    "attribute vec2 a_texcoord;\n"
    "\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "\n"
    "uniform mat4 projview;\n"
    "uniform mat4 texmatrix;\n"
    "uniform bool use_texmatrix;\n"
    "\n"
    "void main()\n"
    "{\n"
    "   mat4 m = use_texmatrix ? texmatrix : mat4(1.0);\n"
    "   vec4 uv = m * vec4(a_texcoord, 0.0, 1.0);\n"

    "   v_texcoord = uv.xy;\n"
    "   v_color = a_color;\n"

    "   gl_Position = projview * a_position;\n"
    "}\n"
"";

static const char fs_glsl_without_alpha_testing[] = ""
    "#ifdef GL_ES\n"
    "precision lowp float;\n"
    "#endif\n"
    "\n"
    "#define use_tex " ALLEGRO_SHADER_VAR_USE_TEX "\n"
    "#define tex " ALLEGRO_SHADER_VAR_TEX "\n"
    "\n"
    "uniform sampler2D tex;\n"
    "uniform bool use_tex;\n"
    "varying vec4 v_color;\n" /* tint */
    "varying vec2 v_texcoord;\n"
    "\n"
    "void main()\n"
    "{\n"
    "   vec4 p = use_tex ? texture2D(tex, v_texcoord) : vec4(1.0);\n"
    "   gl_FragColor = v_color * p;\n"
    "}\n"
"";

static const char fs_glsl_with_alpha_testing[] = ""
    "#ifdef GL_ES\n"
    "precision lowp float;\n"
    "#endif\n"
    "\n"
    "#define use_tex " ALLEGRO_SHADER_VAR_USE_TEX "\n"
    "#define tex " ALLEGRO_SHADER_VAR_TEX "\n"
    "\n"
    "uniform sampler2D tex;\n"
    "uniform bool use_tex;\n"
    "varying vec4 v_color;\n" /* tint */
    "varying vec2 v_texcoord;\n"
    "\n"
    "void main()\n"
    "{\n"
    "   vec4 p = use_tex ? texture2D(tex, v_texcoord) : vec4(1.0);\n"

        /* alpha test: discard the fragment (and don't write to the depth buffer) if alpha is zero */
    "   if(p.a == 0.0)\n"
    "       discard;\n"

    "   gl_FragColor = v_color * p;\n"

#if 0
        /* inspect the depth map */
    "   float depth = gl_FragCoord.z * 0.5 + 0.5;" /* map [-1,1] to [0,1] */
    "   gl_FragColor = vec4(vec3(1.0 - depth), 1.0);\n"
#endif

    "}\n"
"";

/* testing */
#define use_depth_buffer 0
/*#define use_depth_buffer true*/
/*#define use_depth_buffer false*/

/* render queue stats reporting */
#define want_report()             0
#define REPORT(...)               do { if(want_report()) video_showmessage(__VA_ARGS__); } while(0)
#define REPORT_BEGIN()            video_clearmessages()
#define REPORT_END()              REPORT("")

/* utilities */
#define ZINDEX_OFFSET(n)          (0.000001f * (float)(n)) /* ZINDEX_OFFSET(1) is the mininum zindex offset */
#define ZINDEX_LARGE              99999.0f /* will be displayed in front of others */
#define INITIAL_BUFFER_CAPACITY   256
#define LOG(...)                  logfile_message("Render queue - " __VA_ARGS__)
static int cmp_fun(const void* i, const void* j);
static int cmp_zbuf_fun(const void* i, const void* j);
static inline float brick_zindex_offset(const brick_t *brick);
static void enqueue(const renderqueue_entry_t* entry);
static const char* random_path(char prefix);

/* internal data */
static ALLEGRO_SHADER* shader = NULL;
static renderqueue_entry_t* buffer = NULL; /* storage */
static int* sorted_indices = NULL; /* a permutation of 0, 1, ... n-1, where n = buffer_size */
static renderqueue_entry_t** sorted_buffer = NULL; /* sorted indirection to buffer[] */
static int buffer_size = 0;
static int buffer_capacity = 0;
static v2d_t camera;



/*

OPTIMIZATION: DEFERRED DRAWING
------------------------------

According to the Allegro 5 manual (see the link below), deferred bitmap drawing
"allows for efficient drawing of many bitmaps that share a parent bitmap, such
as sub-bitmaps from a tilesheet or simply identical bitmaps".

In order to optimize the rendering process, we'll group the entries of the
render queue that share the same or a parent bitmap and enable deferred bitmap
drawing whenever we see fit.

Each of the n entries of the render queue is associated with a group_index
defined as follows:

group_index[n-1] = 1

group_index[i] = 1 + group_index[i+1], if path[i] == path[i+1]
                 1 otherwise                               for all 0 <= i < n-1

where path[i] is the path of the image file (if any) of the i-th entry of the
*sorted* render queue. If the paths are the same, then the bitmaps of the two
entries are either the same or one is a descendant of the other, thanks to our
resource manager (see src/core/image.h and src/core/resourcemanager.h). Simply
put, if the paths are the same, we will group the entries.

Let's also define the special off-bounds value group_index[-1] = 1. This is
implemented as a circular array, i.e., group_index[-1] = group_index[n-1] = 1.

It turns out that group_index[] is a piecewise monotonic decreasing sequence:
each piece corresponds to a group.

The optimization is implemented as follows:

for each j = 0 .. n-1,

    1. enable deferred drawing if group_index[j] > group_index[j-1]

    2. draw the j-th entry of the render queue

    3. disable deferred drawing if it's enabled and if group_index[j] == 1

since group_index[n-1] = 1, then deferred drawing will be disabled at the end
of the loop. We must disable deferred drawing because "no drawing is guaranteed
to take place until you disable the hold".

Read the documentation for al_hold_bitmap_drawing() at:
https://liballeg.org/a5docs/trunk/graphics.html#deferred-drawing

*/
#define USE_DEFERRED_DRAWING 1


/* ----- public interface -----*/



/*
 * renderqueue_init()
 * Initializes the render queue
 */
void renderqueue_init()
{
    LOG("initializing...");

    /* initialize the camera */
    camera = v2d_new(0, 0);

    /* allocate buffers */
    buffer_size = 0;
    buffer_capacity = INITIAL_BUFFER_CAPACITY;
    buffer = mallocx(buffer_capacity * sizeof(*buffer));
    sorted_buffer = mallocx(buffer_capacity * sizeof(*sorted_buffer));
    sorted_indices = mallocx(buffer_capacity * sizeof(*sorted_indices));

    /* setup the shader of the renderqueue */
    const char* fs_glsl = use_depth_buffer ? fs_glsl_with_alpha_testing : fs_glsl_without_alpha_testing;
    LOG("will %s alpha testing", use_depth_buffer ? "perform" : "not perform");

    shader = al_create_shader(ALLEGRO_SHADER_GLSL);
    if(!al_attach_shader_source(shader, ALLEGRO_VERTEX_SHADER, vs_glsl)) {
        LOG("Can't compile the vertex shader. %s", al_get_shader_log(shader));
        al_destroy_shader(shader);
        shader = NULL;
    }
    else if(!al_attach_shader_source(shader, ALLEGRO_PIXEL_SHADER, fs_glsl)) {
        LOG("Can't compile the fragment shader. %s", al_get_shader_log(shader));
        al_destroy_shader(shader);
        shader = NULL;
    }
    else if(!al_build_shader(shader)) {
        LOG("Can't build the shader. %s", al_get_shader_log(shader));
        al_destroy_shader(shader);
        shader = NULL;
    }

    /* done! */
    LOG("initialized!");
}

/*
 * renderqueue_release()
 * Deinitializes the render queue
 */
void renderqueue_release()
{
    LOG("releasing...");

    if(shader != NULL) {
        al_destroy_shader(shader);
        shader = NULL;
    }

    free(sorted_indices);
    sorted_indices = NULL;

    free(sorted_buffer);
    sorted_buffer = NULL;

    free(buffer);
    buffer = NULL;

    buffer_capacity = 0;
    buffer_size = 0;

    LOG("released!");
}




/*
 * renderqueue_begin()
 * Starts a new rendering process
 */
void renderqueue_begin(v2d_t camera_position)
{
    camera = camera_position;
    buffer_size = 0;
}

/*
 * renderqueue_end()
 * Finishes an existing rendering process (will render everything in the queue)
 */
void renderqueue_end()
{
    int draw_calls = 0;

    /* skip if the buffer is empty */
    if(buffer_size == 0)
        return;

    /* start reporting */
    REPORT_BEGIN();
    REPORT("Render queue stats");
    REPORT("------------------");
    REPORT("Depth test: % 3s", use_depth_buffer ? "yes" : "no");

    /* use the shader of the render queue */
    if(shader != NULL)
        al_use_shader(shader);

#if USE_DEFERRED_DRAWING

    ALLEGRO_TRANSFORM ztransform;
    static char path[2][1024];
    int translucent_start = buffer_size;

    if(use_depth_buffer) {

        /* enable the depth test */
        al_set_render_state(ALLEGRO_DEPTH_FUNCTION, ALLEGRO_RENDER_LESS_EQUAL);
        al_set_render_state(ALLEGRO_WRITE_MASK, ALLEGRO_MASK_DEPTH | ALLEGRO_MASK_RGBA); /* write to the framebuffer and to the depth buffer */
        al_set_render_state(ALLEGRO_DEPTH_TEST, 1);

        /* clear the depth buffer */
        /*al_clear_to_color(al_map_rgba_f(0, 0, 0, 0));*/ /* should we clear? */
        al_clear_depth_buffer(1);

        /* initialize the z-transform */
        al_identity_transform(&ztransform);

        /* set the z-order of each entry */
        for(int i = 0; i < buffer_size; i++)
            sorted_buffer[i]->zorder = i;

        /* sort by source image for batching
           no need of stable sorting here */
        qsort(sorted_buffer, buffer_size, sizeof(*sorted_buffer), cmp_zbuf_fun);

        /* after sorting, partition the buffer into opaque and translucent objects */
        for(int i = buffer_size - 1; i >= 0; i--) {
            if(sorted_buffer[i]->vtable->is_translucent(sorted_buffer[i]->renderable))
                translucent_start = i;
            else
                break;
        }

    }

    /* fill the group_index[] array */
    sorted_buffer[buffer_size - 1]->group_index = 1;
    for(int i = buffer_size - 2; i >= 0; i--) {
        sorted_buffer[i]->vtable->path(sorted_buffer[i]->renderable, path[0], sizeof(path[0]));
        sorted_buffer[i+1]->vtable->path(sorted_buffer[i+1]->renderable, path[1], sizeof(path[1]));

        if(0 == strcmp(path[0], path[1]))
            sorted_buffer[i]->group_index = 1 + sorted_buffer[i+1]->group_index;
        else
            sorted_buffer[i]->group_index = 1;
    }

    /* render the entries */
    bool held = false;
    for(int j = 0; j < buffer_size; j++) {

        int curr = sorted_buffer[j]->group_index;
        int prev = sorted_buffer[(j + (buffer_size - 1)) % buffer_size]->group_index;

        /* enable deferred drawing */
        if(curr > prev) {
            held = true;
            image_hold_drawing(true);
        }

        /* reporting */
        if(curr >= prev) {
            ++draw_calls;
            if(want_report()) {
                char c = (curr == prev) ? '+' : ' '; /* curr == prev only if group_index == 1 */
                sorted_buffer[j]->vtable->path(sorted_buffer[j]->renderable, path[0], sizeof(path[0]));
                REPORT("Batch size:%c%3d %s", c, sorted_buffer[j]->group_index, path[0]);
            }
        }

        /* set the z-coordinate */
        if(use_depth_buffer) {
            /* we've rendered all opaque objects. Now we're going to render
               the translucent ones. Let's disable depth writes and render
               back-to-front. */
            if(j == translucent_start)
                al_set_render_state(ALLEGRO_WRITE_MASK, ALLEGRO_MASK_RGBA);

            /* set z to a value in [0,1] according to the z-order of the entry */
            float z = 1.0f - (float)sorted_buffer[j]->zorder / (float)(buffer_size - 1);

            /* map z from [0,1] to [-1,1], the range of the default
               orthographic projection set by Allegro */
            z = 2.0f * z - 1.0f;

            /* change the transform */
            ztransform.m[3][2] = z;
            al_use_transform(&ztransform);
        }

        /* render the j-th entry */
        sorted_buffer[j]->vtable->render(sorted_buffer[j]->renderable, camera);

        /* disable deferred drawing */
        if(held && sorted_buffer[j]->group_index == 1) {
            image_hold_drawing(false);
            held = false;
        }

    }

    if(use_depth_buffer) {

        /* reset the z-transform */
        al_identity_transform(&ztransform);
        al_use_transform(&ztransform);

        /* disable the depth test */
        al_set_render_state(ALLEGRO_DEPTH_TEST, 0);

    }

#else

    /* render the entries without deferred drawing */
    for(int j = 0; j < buffer_size; j++) {
        sorted_buffer[j]->vtable->render(sorted_buffer[j]->renderable, camera);
        ++draw_calls; /* will be equal to buffer_size */
    }

    REPORT("No batching!");
    (void)cmp_zbuf_fun;

#endif

    /* end of report */
    float savings = 1.0f - (float)draw_calls / (float)buffer_size;
    REPORT("Total     :=%3d", buffer_size);
    REPORT("Draw calls: %3d <saved %.2f%%>", draw_calls, 100.0f * savings);
    REPORT_END();

    /* go back to the default shader set by Allegro */
    if(shader != NULL)
        al_use_shader(NULL);

    /* clean up */
    buffer_size = 0;
}




/*
 * renderqueue_enqueue_brick()
 * Enqueues a brick
 */
void renderqueue_enqueue_brick(brick_t *brick)
{
    renderqueue_entry_t entry = {
        .renderable.brick = brick,
        .vtable = &VTABLE[TYPE_BRICK]
    };

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_brick_mask()
 * Enqueues a brick mask
 */
void renderqueue_enqueue_brick_mask(brick_t *brick)
{
    renderqueue_entry_t entry = {
        .renderable.brick = brick,
        .vtable = &VTABLE[TYPE_BRICK_MASK]
    };

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_brick_debug()
 * Enqueues a brick (editor)
 */
void renderqueue_enqueue_brick_debug(brick_t *brick)
{
    renderqueue_entry_t entry = {
        .renderable.brick = brick,
        .vtable = &VTABLE[TYPE_BRICK_DEBUG]
    };

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_brick_path()
 * Enqueues the path of a moving brick (editor)
 */
void renderqueue_enqueue_brick_path(brick_t *brick)
{
    renderqueue_entry_t entry = {
        .renderable.brick = brick,
        .vtable = &VTABLE[TYPE_BRICK_PATH]
    };

    enqueue(&entry);
}



/*
 * renderqueue_enqueue_item()
 * Enqueues a legacy item
 */
void renderqueue_enqueue_item(item_t *item)
{
    renderqueue_entry_t entry = {
        .renderable.item = item,
        .vtable = &VTABLE[TYPE_ITEM]
    };

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_object()
 * Enqueues a legacy object
 */
void renderqueue_enqueue_object(object_t *object)
{
    renderqueue_entry_t entry = {
        .renderable.object = object,
        .vtable = &VTABLE[TYPE_OBJECT]
    };

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_player()
 * Enqueues a player
 */
void renderqueue_enqueue_player(player_t *player)
{
    renderqueue_entry_t entry = {
        .renderable.player = player,
        .vtable = &VTABLE[TYPE_PLAYER]
    };

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_particles()
 * Enqueues the level particles
 */
void renderqueue_enqueue_particles()
{
    renderqueue_entry_t entry = {
        .renderable.dummy = NULL,
        .vtable = &VTABLE[TYPE_PARTICLE]
    };

    /* skip if there are no particles to render */
    if(particle_is_empty())
        return;

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_ssobject()
 * Enqueues a SurgeScript object
 */
void renderqueue_enqueue_ssobject(surgescript_object_t* object)
{
    renderqueue_entry_t entry = {
        .renderable.ssobject = object,
        .vtable = &VTABLE[TYPE_SSOBJECT]
    };

    /* skip if the object is not a renderable */
    if(!surgescript_object_has_tag(object, "renderable"))
        return;

    /* don't enqueue invisible renderables */
    if(surgescript_object_has_function(object, "get_visible")) {
        surgescript_var_t* ret = surgescript_var_create();
        surgescript_object_call_function(object, "get_visible", NULL, 0, ret);
        bool is_visible = surgescript_var_get_bool(ret);
        surgescript_var_destroy(ret);

        if(!is_visible)
            return;
    }

#if 0
    /* clip out the renderables */
    if(surgescript_object_has_function(object, "__canBeClippedOut")) {
        surgescript_var_t* ret = surgescript_var_create();
        surgescript_object_call_function(object, "__canBeClippedOut", NULL, 0, ret);
        bool can_be_clipped_out = surgescript_var_get_bool(ret);
        surgescript_var_destroy(ret);

        if(can_be_clipped_out)
            return;
    }
#endif

    /* enqueue */
    enqueue(&entry);
}

/*
 * renderqueue_enqueue_ssobject_debug()
 * Enqueues a SurgeScript object (editor)
 */
void renderqueue_enqueue_ssobject_debug(surgescript_object_t* object)
{
    renderqueue_entry_t entry = {
        .renderable.ssobject = object,
        .vtable = &VTABLE[TYPE_SSOBJECT_DEBUG]
    };

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_ssobject_gizmo()
 * Enqueues a SurgeScript object gizmo (editor)
 */
void renderqueue_enqueue_ssobject_gizmo(surgescript_object_t* object)
{
    renderqueue_entry_t entry = {
        .renderable.ssobject = object,
        .vtable = &VTABLE[TYPE_SSOBJECT_GIZMO]
    };

    /* skip if the object is not a gizmo */
    if(!surgescript_object_has_tag(object, "gizmo"))
        return;

    /* enqueue */
    enqueue(&entry);
}

/*
 * renderqueue_enqueue_background()
 * Enqueues the background
 */
void renderqueue_enqueue_background(bgtheme_t* background)
{
    renderqueue_entry_t entry = {
        .renderable.theme = background,
        .vtable = &VTABLE[TYPE_BACKGROUND]
    };

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_foreground()
 * Enqueues the foreground
 */
void renderqueue_enqueue_foreground(bgtheme_t* foreground)
{
    renderqueue_entry_t entry = {
        .renderable.theme = foreground,
        .vtable = &VTABLE[TYPE_FOREGROUND]
    };

    enqueue(&entry);
}

/*
 * renderqueue_enqueue_water()
 * Enqueues the water
 */
void renderqueue_enqueue_water()
{
    renderqueue_entry_t entry = {
        .renderable.dummy = NULL,
        .vtable = &VTABLE[TYPE_WATER]
    };

    /* clip out */
    int y = level_waterlevel() - ((int)camera.y - VIDEO_SCREEN_H / 2);
    if(y >= VIDEO_SCREEN_H)
        return;

    enqueue(&entry);
}





/* ----- private utilities ----- */

/* enqueues an entry */
void enqueue(const renderqueue_entry_t* entry)
{
    /* grow the buffer if necessary */
    if(buffer_size == buffer_capacity) {
        buffer_capacity *= 2;
        buffer = reallocx(buffer, buffer_capacity * sizeof(*buffer));
        sorted_buffer = reallocx(sorted_buffer, buffer_capacity * sizeof(*sorted_buffer));
        sorted_indices = reallocx(sorted_indices, buffer_capacity * sizeof(*sorted_indices));

        /* sorted_buffer[] is invalidated because we have realloc'd buffer[] */
        for(int i = 0; i < buffer_size; i++)
            sorted_buffer[i] = &buffer[sorted_indices[i]];
    }

    /* add the entry to the buffer */
    renderqueue_entry_t* e = &buffer[buffer_size];
    memcpy(e, entry, sizeof(*entry));
    sorted_buffer[buffer_size] = e;
    sorted_indices[buffer_size] = buffer_size;

    /* cache the values of the new entry for purposes of comparison to other entries */
    e->cached.zindex = e->vtable->zindex(e->renderable);
    e->cached.type = e->vtable->type(e->renderable);
    e->cached.ypos = e->vtable->ypos(e->renderable);
    e->cached.is_translucent = e->vtable->is_translucent(e->renderable);

    /*
       keep the buffer sorted using insertion sort, which is
       stable, works well with nearly sorted(?) input and is
       good enough for small arrays
       
       at the time of this writing, we can expect arrays of
       length 100 - 200, approximately, to be sorted on each frame

       is it worthwhile to replace this simple and compact
       implementation for merge_sort(), which relies on memcpy()?
       idea for future work: adaptive approach.

       we want stable sorting in order to preserve the z-order of
       elements that are deemed "equal" by the comparison function

       maybe we can also use heapsort and keep an additional index
       for stability.
    */
    int j = buffer_size - 1;
    while(j >= 0 && cmp_fun(e, sorted_buffer[j]) < 0) {
        sorted_buffer[j+1] = sorted_buffer[j];
        sorted_indices[j+1] = sorted_indices[j];
        --j;
    }
    sorted_buffer[j+1] = e;
    sorted_indices[j+1] = buffer_size;

    /* done! */
    buffer_size++;
}

/* compares two entries of the render queue */
int cmp_fun(const void* i, const void* j)
{
    const renderqueue_entry_t* a = (const renderqueue_entry_t*)i;
    const renderqueue_entry_t* b = (const renderqueue_entry_t*)j;

    /* zindex check */
    float za = a->cached.zindex;
    float zb = b->cached.zindex;

    /* approximately same z-index? */
    if(fabs(za - zb) * 10.0f < ZINDEX_OFFSET(1)) {
        /* sort by type */
        int ta = a->cached.type;
        int tb = b->cached.type;

        if(ta == tb) {
            /* sort by ypos */
            return a->cached.ypos - b->cached.ypos;
        }
        else {
            /* render the players in front of the other entries if all else is equal */
            return (ta == TYPE_PLAYER) - (tb == TYPE_PLAYER);
        }
    }

    /* sort back-to-front */
    return (za > zb) - (za < zb);
}

/* sort the render queue while taking the depth buffer into consideration */
int cmp_zbuf_fun(const void* i, const void* j)
{
    const renderqueue_entry_t* a = *((const renderqueue_entry_t**)i);
    const renderqueue_entry_t* b = *((const renderqueue_entry_t**)j);

    /* put opaque objects first */
    int la = (int)a->cached.is_translucent;
    int lb = (int)b->cached.is_translucent;
    if(la != lb)
        return la - lb;

    /* read paths - TODO not optimal... */
    static char path[2][1024];
    int texcmp = strcmp(
        a->vtable->path(a->renderable, path[0], sizeof(path[0])),
        b->vtable->path(b->renderable, path[1], sizeof(path[1]))
    );

    /* sort by texture first, for optimal batching */
    if(texcmp != 0)
        return texcmp;

    /* if the entries share the same texture, sort
       front-to-back, so that the depth testing can
       discard pixels.

       if both entries are translucent, then sort
       back-to-front. We'll render them separately. */
    float za = a->cached.zindex;
    float zb = b->cached.zindex;
    int dz = (za > zb) - (za < zb);

    if(dz == 0)
        return a->zorder - b->zorder; /* keep relative z-order */
    else if(!la)
        return -dz; /* front-to-back */
    else
        return dz; /* back-to-front */
}

/* compute a tiny zindex offset for a brick depending on its type, layer and behavior */
float brick_zindex_offset(const brick_t *brick)
{
    float s = 0.0f;

    /* a hackish solution... */
    switch(brick_type(brick)) {
        case BRK_PASSABLE:  s -= ZINDEX_OFFSET(20); break;
        case BRK_CLOUD:     s -= ZINDEX_OFFSET(10); break;
        case BRK_SOLID:     break;
    }

    switch(brick_layer(brick)) {
        case BRL_YELLOW:    s -= ZINDEX_OFFSET(50); break;
        case BRL_GREEN:     s += ZINDEX_OFFSET(50); break; /* |layer offset| > max |type offset| */
        case BRL_DEFAULT:   break;
    }

    /* static bricks should appear behind moving bricks
       if they share the same zindex, type and layer */
    if(brick_behavior(brick) == BRB_DEFAULT)
        s -= ZINDEX_OFFSET(1);
    
    /* done */
    return s;
}

/* generates a random string */
const char* random_path(char prefix)
{
    static char path[8], table[] = {
        [0x0] = '0', [0x1] = '1', [0x2] = '2', [0x3] = '3',
        [0x4] = '4', [0x5] = '5', [0x6] = '6', [0x7] = '7',
        [0x8] = '8', [0x9] = '9', [0xa] = 'a', [0xb] = 'b',
        [0xc] = 'c', [0xd] = 'd', [0xe] = 'e', [0xf] = 'f'
    };

    int x = random(65536);

    path[0] = '<';
    path[1] = prefix;
    path[2] = table[(x >> 12) & 0xf];
    path[3] = table[(x >> 8) & 0xf];
    path[4] = table[(x >> 4) & 0xf];
    path[5] = table[x & 0xf];
    path[6] = '>';
    path[7] = '\0';

    return path;
}




/* ----- private strategies ----- */

int type_particles(renderable_t r) { return TYPE_PARTICLE; }
int type_player(renderable_t r) { return TYPE_PLAYER; }
int type_item(renderable_t r) { return TYPE_ITEM; }
int type_object(renderable_t r) { return TYPE_OBJECT; }
int type_brick(renderable_t r) { return TYPE_BRICK; }
int type_brick_mask(renderable_t r) { return TYPE_BRICK_MASK; }
int type_brick_debug(renderable_t r) { return TYPE_BRICK_DEBUG; }
int type_brick_path(renderable_t r) { return TYPE_BRICK_PATH; }
int type_ssobject(renderable_t r) { return TYPE_SSOBJECT; }
int type_ssobject_debug(renderable_t r) { return TYPE_SSOBJECT_DEBUG; }
int type_ssobject_gizmo(renderable_t r) { return TYPE_SSOBJECT_GIZMO; }
int type_background(renderable_t r) { return TYPE_BACKGROUND; }
int type_foreground(renderable_t r) { return TYPE_FOREGROUND; }
int type_water(renderable_t r) { return TYPE_WATER; }

float zindex_particles(renderable_t r) { return 1.0f; }
float zindex_player(renderable_t r) { return player_is_dying(r.player) ? (1.0f - ZINDEX_OFFSET(1)) : 0.5f; }
float zindex_item(renderable_t r) { return 0.5f - (r.item->bring_to_back ? ZINDEX_OFFSET(1) : 0.0f); }
float zindex_object(renderable_t r) { return r.object->zindex; }
float zindex_brick(renderable_t r) { return brick_zindex(r.brick) + brick_zindex_offset(r.brick); }
float zindex_brick_mask(renderable_t r) { return ZINDEX_LARGE + brick_zindex_offset(r.brick); }
float zindex_brick_debug(renderable_t r) { return zindex_brick(r); }
float zindex_brick_path(renderable_t r) { return zindex_brick_mask(r) + 1.0f; }
float zindex_ssobject(renderable_t r) { return scripting_util_object_zindex(r.ssobject); }
float zindex_ssobject_debug(renderable_t r) { return scripting_util_object_zindex(r.ssobject); } /* TODO: check children */
float zindex_ssobject_gizmo(renderable_t r) { return ZINDEX_LARGE; }
float zindex_background(renderable_t r) { return 0.0f; }
float zindex_foreground(renderable_t r) { return 1.0f; }
float zindex_water(renderable_t r) { return 1.0f; }

int ypos_particles(renderable_t r) { return 0; }
int ypos_player(renderable_t r) { return 0; } /*(int)(r.player->actor->position.y);*/
int ypos_item(renderable_t r) { return (int)(r.item->actor->position.y); }
int ypos_object(renderable_t r) { return (int)(r.object->actor->position.y); }
int ypos_brick(renderable_t r) { return brick_position(r.brick).y; }
int ypos_brick_mask(renderable_t r) { return ypos_brick(r); }
int ypos_brick_debug(renderable_t r) { return ypos_brick(r); }
int ypos_brick_path(renderable_t r) { return ypos_brick(r); }
int ypos_ssobject(renderable_t r) { return 0; } /* TODO (not needed?) */
int ypos_ssobject_debug(renderable_t r) { return ypos_ssobject(r); }
int ypos_ssobject_gizmo(renderable_t r) { return ypos_ssobject(r); }
int ypos_background(renderable_t r) { return 0; } /* preserve relative indexes */
int ypos_foreground(renderable_t r) { return 0; } /* preserve relative indexes */
int ypos_water(renderable_t r) { return 0; } /* not needed */

bool is_translucent_particles(renderable_t r) { return false; }
bool is_translucent_player(renderable_t r) { return false; }
bool is_translucent_item(renderable_t r) { return false; }
bool is_translucent_object(renderable_t r) { return false; }
bool is_translucent_brick(renderable_t r) { return false; }
bool is_translucent_brick_mask(renderable_t r) { return false; }
bool is_translucent_brick_debug(renderable_t r) { return false; }
bool is_translucent_brick_path(renderable_t r) { return false; }
bool is_translucent_background(renderable_t r) { return false; }
bool is_translucent_foreground(renderable_t r) { return false; }

bool is_translucent_water(renderable_t r) { return true; }
bool is_translucent_ssobject_gizmo(renderable_t r) { return false; }
bool is_translucent_ssobject_debug(renderable_t r) { return is_translucent_ssobject(r); }
bool is_translucent_ssobject(renderable_t r)
{
    if(surgescript_object_has_function(r.ssobject, "get_alpha")) {
        surgescript_var_t* ret = surgescript_var_create();
        surgescript_object_call_function(r.ssobject, "get_alpha", NULL, 0, ret);
        double alpha = surgescript_var_get_number(ret);
        surgescript_var_destroy(ret);

        return alpha < 1.0;
    }

    return false;
}

const char* path_particles(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, "<particles>", dest_size); }
const char* path_player(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, image_filepath(actor_image(r.player->actor)), dest_size); }
const char* path_item(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, "<legacy-item>", dest_size); }
const char* path_object(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, "<legacy-object>", dest_size); }
const char* path_brick(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, image_filepath(brick_image(r.brick)), dest_size); }
const char* path_brick_mask(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, random_path('M'), dest_size); }
const char* path_brick_debug(renderable_t r, char* dest, size_t dest_size) { return path_brick(r, dest, dest_size); }
const char* path_brick_path(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, random_path('P'), dest_size); }
const char* path_background(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, "<background>", dest_size); }
const char* path_foreground(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, "<foreground>", dest_size); }
const char* path_water(renderable_t r, char* dest, size_t dest_size) { return str_cpy(dest, "<water>", dest_size); }

const char* path_ssobject(renderable_t r, char* dest, size_t dest_size)
{
    if(surgescript_object_has_function(r.ssobject, "get_filepathOfRenderable")) {
        surgescript_var_t* ret = surgescript_var_create();

        surgescript_object_call_function(r.ssobject, "get_filepathOfRenderable", NULL, 0, ret);
        str_cpy(dest, surgescript_var_fast_get_string(ret), dest_size);

        surgescript_var_destroy(ret);
        return dest;
    }

    /*str_cpy(dest, surgescript_object_name(r.ssobject), dest_size);*/
    return str_cpy(dest, random_path('S'), dest_size);
}

const char* path_ssobject_debug(renderable_t r, char* dest, size_t dest_size)
{
    /* this routine is based on render_ssobject_debug() */
    const char* name = surgescript_object_name(r.ssobject);
    const animation_t* anim = sprite_animation_exists(name, 0) ? sprite_get_animation(name, 0) : sprite_get_animation(NULL, 0);
    const image_t* img = sprite_get_image(anim, 0);

    return str_cpy(dest, image_filepath(img), dest_size);
}

const char* path_ssobject_gizmo(renderable_t r, char* dest, size_t dest_size)
{
    return str_cpy(dest, random_path('G'), dest_size);
}




/* --- private rendering routines --- */

void render_particles(renderable_t r, v2d_t camera_position)
{
    particle_render(camera_position);
}

void render_player(renderable_t r, v2d_t camera_position)
{
    player_render(r.player, camera_position);
}

void render_item(renderable_t r, v2d_t camera_position)
{
    item_render(r.item, camera_position);
}

void render_object(renderable_t r, v2d_t camera_position)
{
    enemy_render(r.object, camera_position);
}

void render_brick(renderable_t r, v2d_t camera_position)
{
    brick_render(r.brick, camera_position);
}

void render_brick_mask(renderable_t r, v2d_t camera_position)
{
    brick_render_mask(r.brick, camera_position);
}

void render_brick_debug(renderable_t r, v2d_t camera_position)
{
    brick_render_debug(r.brick, camera_position);
}

void render_brick_path(renderable_t r, v2d_t camera_position)
{
    brick_render_path(r.brick, camera_position);
}

void render_ssobject(renderable_t r, v2d_t camera_position)
{
    surgescript_object_call_function(r.ssobject, "onRender", NULL, 0, NULL);
}

void render_ssobject_gizmo(renderable_t r, v2d_t camera_position)
{
    surgescript_object_call_function(r.ssobject, "onRenderGizmos", NULL, 0, NULL);
}

void render_ssobject_debug(renderable_t r, v2d_t camera_position)
{
    /* in render_ssobject_debug(), we don't call the "onRender"
       method of the SurgeScript object, so we don't provoke
       any changes within its state or data */
    const char* name = surgescript_object_name(r.ssobject);
    const animation_t* anim = sprite_animation_exists(name, 0) ? sprite_get_animation(name, 0) : sprite_get_animation(NULL, 0);
    const image_t* img = sprite_get_image(anim, 0);
    v2d_t hot_spot = anim->hot_spot;
    v2d_t position = scripting_util_world_position(r.ssobject);

    if(level_inside_screen(position.x - hot_spot.x, position.y - hot_spot.y, image_width(img), image_height(img))) {
        v2d_t half_screen = v2d_multiply(video_get_screen_size(), 0.5f);
        v2d_t topleft = v2d_subtract(camera_position, half_screen);

        image_draw(img, position.x - hot_spot.x - topleft.x, position.y - hot_spot.y - topleft.y, IF_NONE);
    }
}

void render_background(renderable_t r, v2d_t camera_position)
{
    background_render_bg(r.theme, camera_position);
}

void render_foreground(renderable_t r, v2d_t camera_position)
{
    background_render_fg(r.theme, camera_position);
}

void render_water(renderable_t r, v2d_t camera_position)
{
    /* convert the waterlevel from world space to screen space */
    int y = level_waterlevel() - ((int)camera_position.y - VIDEO_SCREEN_H / 2);

    /* clip out */
    if(y >= VIDEO_SCREEN_H)
        return;

    /* adjust y */
    y = max(0, y);

    /*

    Let's adjust the color of the water by pre-multiplying the alpha value

    "By default Allegro uses pre-multiplied alpha for transparent blending of
    bitmaps and primitives (see al_load_bitmap_flags for a discussion of that
    feature). This means that if you want to tint a bitmap or primitive to be
    transparent you need to multiply the color components by the alpha
    components when you pass them to this function."

    Source: Allegro manual at
    https://liballeg.org/a5docs/trunk/graphics.html#al_premul_rgba

    */
    uint8_t red, green, blue, alpha;
    color_t color = level_watercolor();
    color_unmap(color, &red, &green, &blue, &alpha);
    color = color_premul_rgba(red, green, blue, alpha);

    /* render the water */
    image_rectfill(0, y, VIDEO_SCREEN_W, VIDEO_SCREEN_H, color);
}

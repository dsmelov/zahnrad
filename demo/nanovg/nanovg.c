/*
    Copyright (c) 2016 Micha Mettke

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1.  The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software
        in a product, an acknowledgment in the product documentation would be
        appreciated but is not required.
    2.  Altered source versions must be plainly marked as such, and must not be
        misrepresented as being the original software.
    3.  This notice may not be removed or altered from any source distribution.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
    #include <windows.h>
#endif

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <SDL2/SDL.h>

#define NANOVG_GLES3_IMPLEMENTATION
#include "dep/nanovg.h"
#include "dep/nanovg_gl.h"
#include "dep/nanovg_gl_utils.h"
#include "dep/nanovg.c"

/* macros */
#include "../../zahnrad.h"
#include "../demo.c"

static void
die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs("\n", stderr);
    exit(EXIT_FAILURE);
}

static size_t
font_get_width(zr_handle handle, float height, const char *text, size_t len)
{
    size_t width;
    float bounds[4];
    NVGcontext *ctx = (NVGcontext*)handle.ptr;
    nvgFontSize(ctx, (float)height);
    nvgTextBounds(ctx, 0, 0, text, &text[len], bounds);
    width = (size_t)(bounds[2] - bounds[0]);
    return width;
}

static void
draw(NVGcontext *nvg, struct zr_context *ctx, int width, int height)
{
    const struct zr_command *cmd;
    glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_TEXTURE_2D);

    nvgBeginFrame(nvg, width, height, ((float)width/(float)height));
    zr_foreach(cmd, ctx) {
        switch (cmd->type) {
        case ZR_COMMAND_NOP: break;
        case ZR_COMMAND_SCISSOR: {
            const struct zr_command_scissor *s = zr_command(scissor, cmd);
            nvgScissor(nvg, s->x, s->y, s->w, s->h);
        } break;
        case ZR_COMMAND_LINE: {
            const struct zr_command_line *l = zr_command(line, cmd);
            nvgBeginPath(nvg);
            nvgMoveTo(nvg, l->begin.x, l->begin.y);
            nvgLineTo(nvg, l->end.x, l->end.y);
            nvgStrokeColor(nvg, nvgRGBA(l->color.r, l->color.g, l->color.b, l->color.a));
            nvgStroke(nvg);
        } break;
        case ZR_COMMAND_CURVE: {
            const struct zr_command_curve *q = zr_command(curve, cmd);
            nvgBeginPath(nvg);
            nvgMoveTo(nvg, q->begin.x, q->begin.y);
            nvgBezierTo(nvg, q->ctrl[0].x, q->ctrl[0].y, q->ctrl[1].x, q->ctrl[1].y, q->end.x, q->end.y);
            nvgStrokeColor(nvg, nvgRGBA(q->color.r, q->color.g, q->color.b, q->color.a));
            nvgStroke(nvg);
        } break;
        case ZR_COMMAND_RECT: {
            const struct zr_command_rect *r = zr_command(rect, cmd);
            nvgBeginPath(nvg);
            nvgRoundedRect(nvg, r->x, r->y, r->w, r->h, r->rounding);
            nvgFillColor(nvg, nvgRGBA(r->color.r, r->color.g, r->color.b, r->color.a));
            nvgFill(nvg);
        } break;
        case ZR_COMMAND_CIRCLE: {
            const struct zr_command_circle *c = zr_command(circle, cmd);
            nvgBeginPath(nvg);
            nvgCircle(nvg, c->x + (c->w/2.0f), c->y + c->w/2.0f, c->w/2.0f);
            nvgFillColor(nvg, nvgRGBA(c->color.r, c->color.g, c->color.b, c->color.a));
            nvgFill(nvg);
        } break;
        case ZR_COMMAND_TRIANGLE: {
            const struct zr_command_triangle *t = zr_command(triangle, cmd);
            nvgBeginPath(nvg);
            nvgMoveTo(nvg, t->a.x, t->a.y);
            nvgLineTo(nvg, t->b.x, t->b.y);
            nvgLineTo(nvg, t->c.x, t->c.y);
            nvgLineTo(nvg, t->a.x, t->a.y);
            nvgFillColor(nvg, nvgRGBA(t->color.r, t->color.g, t->color.b, t->color.a));
            nvgFill(nvg);
        } break;
        case ZR_COMMAND_TEXT: {
            const struct zr_command_text *t = zr_command(text, cmd);
            nvgBeginPath(nvg);
            nvgRoundedRect(nvg, t->x, t->y, t->w, t->h, 0);
            nvgFillColor(nvg, nvgRGBA(t->background.r, t->background.g,
                t->background.b, t->background.a));
            nvgFill(nvg);

            nvgBeginPath(nvg);
            nvgFillColor(nvg, nvgRGBA(t->foreground.r, t->foreground.g,
                t->foreground.b, t->foreground.a));
            nvgFontSize(nvg, (float)t->height);
            nvgTextAlign(nvg, NVG_ALIGN_MIDDLE);
            nvgText(nvg, t->x, t->y + t->h * 0.5f, t->string, &t->string[t->length]);
            nvgFill(nvg);
        } break;
        case ZR_COMMAND_IMAGE: {
            const struct zr_command_image *i = zr_command(image, cmd);
            NVGpaint imgpaint;
            imgpaint = nvgImagePattern(nvg, i->x, i->y, i->w, i->h, 0, i->img.handle.id, 1.0f);
            nvgBeginPath(nvg);
            nvgRoundedRect(nvg, i->x, i->y, i->w, i->h, 0);
            nvgFillPaint(nvg, imgpaint);
            nvgFill(nvg);
        } break;
        case ZR_COMMAND_ARC:
        default: break;
        }
    }
    zr_clear(ctx);

    nvgResetScissor(nvg);
    nvgEndFrame(nvg);
    glPopAttrib();
}

static void
input_key(struct zr_context *ctx, SDL_Event *evt, int down)
{
    const Uint8* state = SDL_GetKeyboardState(NULL);
    SDL_Keycode sym = evt->key.keysym.sym;
    if (sym == SDLK_RSHIFT || sym == SDLK_LSHIFT)
        zr_input_key(ctx, ZR_KEY_SHIFT, down);
    else if (sym == SDLK_DELETE)
        zr_input_key(ctx, ZR_KEY_DEL, down);
    else if (sym == SDLK_RETURN)
        zr_input_key(ctx, ZR_KEY_ENTER, down);
    else if (sym == SDLK_TAB)
        zr_input_key(ctx, ZR_KEY_TAB, down);
    else if (sym == SDLK_BACKSPACE)
        zr_input_key(ctx, ZR_KEY_BACKSPACE, down);
    else if (sym == SDLK_LEFT)
        zr_input_key(ctx, ZR_KEY_LEFT, down);
    else if (sym == SDLK_RIGHT)
        zr_input_key(ctx, ZR_KEY_RIGHT, down);
    else if (sym == SDLK_c)
        zr_input_key(ctx, ZR_KEY_COPY, down && state[SDL_SCANCODE_LCTRL]);
    else if (sym == SDLK_v)
        zr_input_key(ctx, ZR_KEY_PASTE, down && state[SDL_SCANCODE_LCTRL]);
    else if (sym == SDLK_x)
        zr_input_key(ctx, ZR_KEY_CUT, down && state[SDL_SCANCODE_LCTRL]);
}

static void
input_motion(struct zr_context *ctx, SDL_Event *evt)
{
    const int x = evt->motion.x;
    const int y = evt->motion.y;
    zr_input_motion(ctx, x, y);
}

static void
input_button(struct zr_context *ctx, SDL_Event *evt, int down)
{
    const int x = evt->button.x;
    const int y = evt->button.y;
    if (evt->button.button == SDL_BUTTON_LEFT)
        zr_input_button(ctx, ZR_BUTTON_LEFT, x, y, down);
    if (evt->button.button == SDL_BUTTON_RIGHT)
        zr_input_button(ctx, ZR_BUTTON_RIGHT, x, y, down);
}

static void
input_text(struct zr_context *ctx, SDL_Event *evt)
{
    zr_glyph glyph;
    memcpy(glyph, evt->text.text, ZR_UTF_SIZE);
    zr_input_glyph(ctx, glyph);
}

static void
resize(SDL_Event *evt)
{
    if (evt->window.event != SDL_WINDOWEVENT_RESIZED) return;
    glViewport(0, 0, evt->window.data1, evt->window.data2);
}

int
main(int argc, char *argv[])
{
    /* Platform */
    int running = 1;
    int width, height;
    const char *font_path;
    int font_height;
    SDL_Window *win;
    SDL_GLContext glContext;
    NVGcontext *vg = NULL;
    int poll = 1;

    /* GUI */
    struct demo gui;
    if (argc < 2) {
        fprintf(stdout,"Missing TTF Font file argument: binary <font-path>\n");
        exit(EXIT_FAILURE);
    }
    font_path = argv[1];
    font_height = 15;

    /* SDL */
    SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_EVENTS);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    win = SDL_CreateWindow("Demo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    glContext = SDL_GL_CreateContext(win);
    SDL_GetWindowSize(win, &width, &height);

    /* OpenGL */
    glewExperimental = 1;
    if (glewInit() != GLEW_OK)
        die("[GLEW] failed setup\n");
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

    /* nanovg */
    vg = nvgCreateGLES3(NVG_ANTIALIAS);
    if (!vg) die("[NVG]: failed to init\n");
    nvgCreateFont(vg, "fixed", font_path);
    nvgFontFace(vg, "fixed");
    nvgFontSize(vg, font_height);
    nvgTextAlign(vg, NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);

    {
        /* GUI */
        struct zr_user_font usrfnt;
        usrfnt.userdata = zr_handle_ptr(vg);
        usrfnt.width = font_get_width;
        nvgTextMetrics(vg, NULL, NULL, &usrfnt.height);

        memset(&gui, 0, sizeof gui);
        gui.memory = calloc(MAX_MEMORY, 1);
        zr_init_fixed(&gui.ctx, gui.memory, MAX_MEMORY, &usrfnt);
    }

    while (running) {
        /* Input */
        int ret;
        SDL_Event evt;
        zr_input_begin(&gui.ctx);
        if (!poll) {
            ret = SDL_WaitEvent(&evt);
            poll = 1;
        } else ret = SDL_PollEvent(&evt);
        while (ret) {
            if (evt.type == SDL_WINDOWEVENT) resize(&evt);
            else if (evt.type == SDL_QUIT) goto cleanup;
            else if (evt.type == SDL_KEYUP) input_key(&gui.ctx, &evt, zr_false);
            else if (evt.type == SDL_KEYDOWN) input_key(&gui.ctx, &evt, zr_true);
            else if (evt.type == SDL_MOUSEBUTTONDOWN) input_button(&gui.ctx, &evt, zr_true);
            else if (evt.type == SDL_MOUSEBUTTONUP) input_button(&gui.ctx, &evt, zr_false);
            else if (evt.type == SDL_MOUSEMOTION) input_motion(&gui.ctx, &evt);
            else if (evt.type == SDL_TEXTINPUT) input_text(&gui.ctx, &evt);
            else if (evt.type == SDL_MOUSEWHEEL)
                zr_input_scroll(&gui.ctx,(float)evt.wheel.y);
            ret = SDL_PollEvent(&evt);
        }
        zr_input_end(&gui.ctx);

        /* GUI */
        SDL_GetWindowSize(win, &width, &height);
        running = run_demo(&gui);

        /* Draw */
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        draw(vg, &gui.ctx, width, height);
        SDL_GL_SwapWindow(win);
        poll = ((poll+1) & 4);
    }

cleanup:
    /* Cleanup */
    free(gui.memory);
    nvgDeleteGLES3(vg);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}


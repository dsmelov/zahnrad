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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/Xlocale.h>

/* macros */
#define DTIME       16
#include "../../zahnrad.h"
#include "../demo.c"

typedef struct XFont XFont;
typedef struct XSurface XSurface;
typedef struct XWindow XWindow;

struct XFont {
    int ascent;
    int descent;
    int height;
    XFontSet set;
    XFontStruct *xfont;
};

struct XSurface {
    GC gc;
    Display *dpy;
    int screen;
    Window root;
    Drawable drawable;
    unsigned int w, h;
};

struct XWindow {
    Display *dpy;
    Window root;
    Visual *vis;
    XFont *font;
    XSurface *surf;
    Colormap cmap;
    XWindowAttributes attr;
    XSetWindowAttributes swa;
    Window win;
    int screen;
    unsigned int width;
    unsigned int height;
};

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

static void*
xcalloc(size_t siz, size_t n)
{
    void *ptr = calloc(siz, n);
    if (!ptr) die("Out of memory\n");
    return ptr;
}

static long
timestamp(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) return 0;
    return (long)((long)tv.tv_sec * 1000 + (long)tv.tv_usec/1000);
}

static void
sleep_for(long t)
{
    struct timespec req;
    const time_t sec = (int)(t/1000);
    const long ms = t - (sec * 1000);
    req.tv_sec = sec;
    req.tv_nsec = ms * 1000000L;
    while(-1 == nanosleep(&req, &req));
}

static XFont*
font_create(Display *dpy, const char *name)
{
    int n;
    char *def, **missing;
    XFont *font = (XFont*)xcalloc(1, sizeof(XFont));
    font->set = XCreateFontSet(dpy, name, &missing, &n, &def);
    if(missing) {
        while(n--)
            fprintf(stderr, "missing fontset: %s\n", missing[n]);
        XFreeStringList(missing);
    }

    if(font->set) {
        XFontStruct **xfonts;
        char **font_names;
        XExtentsOfFontSet(font->set);
        n = XFontsOfFontSet(font->set, &xfonts, &font_names);
        while(n--) {
            font->ascent = MAX(font->ascent, (*xfonts)->ascent);
            font->descent = MAX(font->descent,(*xfonts)->descent);
            xfonts++;
        }
    } else {
        if(!(font->xfont = XLoadQueryFont(dpy, name))
        && !(font->xfont = XLoadQueryFont(dpy, "fixed")))
            die("error, cannot load font: '%s'\n", name);
        font->ascent = font->xfont->ascent;
        font->descent = font->xfont->descent;
    }
    font->height = font->ascent + font->descent;
    return font;
}

static zr_size
font_get_text_width(zr_handle handle, float height, const char *text, zr_size len)
{
    XFont *font = (XFont*)handle.ptr;
    XRectangle r;
    if(!font || !text)
        return 0;

    UNUSED(height);
    if(font->set) {
        XmbTextExtents(font->set, (const char*)text, (int)len, NULL, &r);
        return r.width;
    } else return (zr_size)XTextWidth(font->xfont, (const char*)text, (int)len);
}

static void
font_del(Display *dpy, XFont *font)
{
    if(!font) return;
    if(font->set)
        XFreeFontSet(dpy, font->set);
    else
        XFreeFont(dpy, font->xfont);
    free(font);
}

static unsigned long
color_from_byte(const zr_byte *c)
{
    unsigned long res = 0;
    res |= (unsigned long)c[0] << 16;
    res |= (unsigned long)c[1] << 8;
    res |= (unsigned long)c[2] << 0;
    return (res);
}

static XSurface*
surface_create(Display *dpy,  int screen, Window root, unsigned int w, unsigned int h)
{
    XSurface *surface = (XSurface*)xcalloc(1, sizeof(XSurface));
    surface->w = w;
    surface->h = h;
    surface->dpy = dpy;
    surface->screen = screen;
    surface->root = root;
    surface->gc = XCreateGC(dpy, root, 0, NULL);
    XSetLineAttributes(dpy, surface->gc, 1, LineSolid, CapButt, JoinMiter);
    surface->drawable = XCreatePixmap(dpy, root, w, h, (unsigned int)DefaultDepth(dpy, screen));
    return surface;
}

static void
surface_resize(XSurface *surf, unsigned int w, unsigned int h) {
    if(!surf) return;
    if (surf->w == w && surf->h == h) return;
    surf->w = w; surf->h = h;
    if(surf->drawable) XFreePixmap(surf->dpy, surf->drawable);
    surf->drawable = XCreatePixmap(surf->dpy, surf->root, w, h,
        (unsigned int)DefaultDepth(surf->dpy, surf->screen));
}

static void
surface_scissor(XSurface *surf, float x, float y, float w, float h)
{
    XRectangle clip_rect;
    clip_rect.x = (short)(x-1);
    clip_rect.y = (short)(y-1);
    clip_rect.width = (unsigned short)(w+2);
    clip_rect.height = (unsigned short)(h+2);
    XSetClipRectangles(surf->dpy, surf->gc, 0, 0, &clip_rect, 1, Unsorted);
}

static void
surface_draw_line(XSurface *surf, int16_t x0, int16_t y0, int16_t x1,
    int16_t y1, struct zr_color col)
{
    unsigned long c = color_from_byte(&col.r);
    XSetForeground(surf->dpy, surf->gc, c);
    XDrawLine(surf->dpy, surf->drawable, surf->gc, (int)x0, (int)y0, (int)x1, (int)y1);
}

static void
surface_draw_rect(XSurface* surf, int16_t x, int16_t y, uint16_t w,
    uint16_t h, uint16_t r, struct zr_color col)
{
    unsigned long c = color_from_byte(&col.r);
    XSetForeground(surf->dpy, surf->gc, c);
    if (r == 0) {
        XFillRectangle(surf->dpy, surf->drawable, surf->gc, x, y, w, h);
    } else {
        short xc = x + r;
        short yc = y + r;
        short wc = (short)(w - 2 * r);
        short hc = (short)(h - 2 * r);

        XPoint pnts[12];
        pnts[0].x = x;
        pnts[0].y = yc;
        pnts[1].x = xc;
        pnts[1].y = yc;
        pnts[2].x = xc;
        pnts[2].y = y;

        pnts[3].x = xc + wc;
        pnts[3].y = y;
        pnts[4].x = xc + wc;
        pnts[4].y = yc;
        pnts[5].x = x + w;
        pnts[5].y = yc;

        pnts[6].x = x + w;
        pnts[6].y = yc + hc;
        pnts[7].x = xc + wc;
        pnts[7].y = yc + hc;
        pnts[8].x = xc + wc;
        pnts[8].y = y + h;

        pnts[9].x = xc;
        pnts[9].y = y + h;
        pnts[10].x = xc;
        pnts[10].y = yc + hc;
        pnts[11].x = x;
        pnts[11].y = yc + hc;

        XFillPolygon(surf->dpy, surf->drawable, surf->gc, pnts, 12, Convex, CoordModeOrigin);
        XFillArc(surf->dpy, surf->drawable, surf->gc, xc + wc - r, y,
            (unsigned)r*2, (unsigned)r*2, 0 * 64, 90 * 64);
        XFillArc(surf->dpy, surf->drawable, surf->gc, x, y,
            (unsigned)r*2, (unsigned)r*2, 90 * 64, 90 * 64);
        XFillArc(surf->dpy, surf->drawable, surf->gc, x, yc + hc - r,
            (unsigned)r*2, (unsigned)2*r, 180 * 64, 90 * 64);
        XFillArc(surf->dpy, surf->drawable, surf->gc, xc + wc - r, yc + hc - r,
            (unsigned)r*2, (unsigned)2*r, -90 * 64, 90 * 64);
    }
}

static void
surface_draw_triangle(XSurface *surf, int16_t x0, int16_t y0, int16_t x1,
    int16_t y1, int16_t x2, int16_t y2, struct zr_color col)
{
    XPoint pnts[3];
    unsigned long c = color_from_byte(&col.r);
    pnts[0].x = (short)x0;
    pnts[0].y = (short)y0;
    pnts[1].x = (short)x1;
    pnts[1].y = (short)y1;
    pnts[2].x = (short)x2;
    pnts[2].y = (short)y2;
    XSetForeground(surf->dpy, surf->gc, c);
    XFillPolygon(surf->dpy, surf->drawable, surf->gc, pnts, 3, Convex, CoordModeOrigin);
}

static void
surface_draw_circle(XSurface *surf, int16_t x, int16_t y, uint16_t w,
    uint16_t h, struct zr_color col)
{
    unsigned long c = color_from_byte(&col.r);
    XSetForeground(surf->dpy, surf->gc, c);
    XFillArc(surf->dpy, surf->drawable, surf->gc, (int)x, (int)y,
        (unsigned)w, (unsigned)h, 0, 360 * 64);
}

static void
surface_draw_text(XSurface *surf, int16_t x, int16_t y, uint16_t w, uint16_t h,
    const char *text, size_t len, XFont *font, struct zr_color cbg, struct zr_color cfg)
{
    int tx, ty, th;
    unsigned long bg = color_from_byte(&cbg.r);
    unsigned long fg = color_from_byte(&cfg.r);

    XSetForeground(surf->dpy, surf->gc, bg);
    XFillRectangle(surf->dpy, surf->drawable, surf->gc, (int)x, (int)y, (unsigned)w, (unsigned)h);
    if(!text || !font || !len) return;

    tx = (int)x;
    th = font->ascent + font->descent;
    ty = (int)y + ((int)h / 2) - (th / 2) + font->ascent;
    XSetForeground(surf->dpy, surf->gc, fg);
    if(font->set)
        XmbDrawString(surf->dpy,surf->drawable,font->set,surf->gc,tx,ty,(const char*)text,(int)len);
    else
        XDrawString(surf->dpy, surf->drawable, surf->gc, tx, ty, (const char*)text, (int)len);
}

static void
surface_clear(XSurface *surf, unsigned long color)
{
    XSetForeground(surf->dpy, surf->gc, color);
    XFillRectangle(surf->dpy, surf->drawable, surf->gc, 0, 0, surf->w, surf->h);
}

static void
surface_blit(Drawable target, XSurface *surf, unsigned int width, unsigned int height)
{
    XCopyArea(surf->dpy, surf->drawable, target, surf->gc, 0, 0, width, height, 0, 0);
}

static void
surface_del(XSurface *surf)
{
    XFreePixmap(surf->dpy, surf->drawable);
    XFreeGC(surf->dpy, surf->gc);
    free(surf);
}

static void
input_key(struct XWindow *xw, struct zr_context *ctx, XEvent *evt, int down)
{
    int ret;
    KeySym *code = XGetKeyboardMapping(xw->dpy, (KeyCode)evt->xkey.keycode, 1, &ret);
    if (*code == XK_Shift_L || *code == XK_Shift_R)
        zr_input_key(ctx, ZR_KEY_SHIFT, down);
    else if (*code == XK_Delete)
        zr_input_key(ctx, ZR_KEY_DEL, down);
    else if (*code == XK_Return)
        zr_input_key(ctx, ZR_KEY_ENTER, down);
    else if (*code == XK_Tab)
        zr_input_key(ctx, ZR_KEY_TAB, down);
    else if (*code == XK_space && !down)
        zr_input_char(ctx, ' ');
    else if (*code == XK_Left)
        zr_input_key(ctx, ZR_KEY_LEFT, down);
    else if (*code == XK_Right)
        zr_input_key(ctx, ZR_KEY_RIGHT, down);
    else if (*code == XK_BackSpace)
        zr_input_key(ctx, ZR_KEY_BACKSPACE, down);
    else {
        if (*code == 'c' && (evt->xkey.state & ControlMask))
            zr_input_key(ctx, ZR_KEY_COPY, down);
        else if (*code == 'v' && (evt->xkey.state & ControlMask))
            zr_input_key(ctx, ZR_KEY_PASTE, down);
        else if (*code == 'x' && (evt->xkey.state & ControlMask))
            zr_input_key(ctx, ZR_KEY_CUT, down);
        else if (!down) {
            KeySym keysym = 0;
            char buf[32];
            XLookupString((XKeyEvent*)evt, buf, 32, &keysym, NULL);
            zr_input_glyph(ctx, buf);
        }
    }
    XFree(code);
}

static void
input_motion(struct zr_context *ctx, XEvent *evt)
{
    const int x = evt->xmotion.x;
    const int y = evt->xmotion.y;
    zr_input_motion(ctx, x, y);
}

static void
input_button(struct zr_context *ctx, XEvent *evt, int down)
{
    const int x = evt->xbutton.x;
    const int y = evt->xbutton.y;
    if (evt->xbutton.button == Button1)
        zr_input_button(ctx, ZR_BUTTON_LEFT, x, y, down);
    else if (evt->xbutton.button == Button3)
        zr_input_button(ctx, ZR_BUTTON_RIGHT, x, y, down);
    else if (evt->xbutton.button == Button4)
        zr_input_scroll(ctx, 1.0f);
    else if (evt->xbutton.button == Button5)
        zr_input_scroll(ctx, -1.0f);
}

static void
resize(struct XWindow *xw, XSurface *surf)
{
    XGetWindowAttributes(xw->dpy, xw->win, &xw->attr);
    xw->width = (unsigned int)xw->attr.width;
    xw->height = (unsigned int)xw->attr.height;
    surface_resize(surf, xw->width, xw->height);
}

int
main(int argc, char *argv[])
{
    long dt;
    long started;
    XWindow xw;
    struct demo gui;
    struct zr_user_font font;
    int running = 1;

    /* X11 */
    UNUSED(argc); UNUSED(argv);
    memset(&xw, 0, sizeof xw);
    if (setlocale(LC_ALL, "") == NULL) return 9;
    if (!XSupportsLocale()) return 10;
    if (!XSetLocaleModifiers("@im=none")) return 11;

    xw.dpy = XOpenDisplay(NULL);
    xw.root = DefaultRootWindow(xw.dpy);
    xw.screen = XDefaultScreen(xw.dpy);
    xw.vis = XDefaultVisual(xw.dpy, xw.screen);
    xw.cmap = XCreateColormap(xw.dpy,xw.root,xw.vis,AllocNone);
    xw.swa.colormap = xw.cmap;
    xw.swa.event_mask =
        ExposureMask | KeyPressMask | KeyReleaseMask |
        ButtonPress | ButtonReleaseMask| ButtonMotionMask |
        Button1MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask|
        PointerMotionMask | KeymapStateMask;
    xw.win = XCreateWindow(xw.dpy, xw.root, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
        XDefaultDepth(xw.dpy, xw.screen), InputOutput,
        xw.vis, CWEventMask | CWColormap, &xw.swa);
    XStoreName(xw.dpy, xw.win, "X11");
    XMapWindow(xw.dpy, xw.win);

    XGetWindowAttributes(xw.dpy, xw.win, &xw.attr);
    xw.width = (unsigned int)xw.attr.width;
    xw.height = (unsigned int)xw.attr.height;
    xw.surf = surface_create(xw.dpy, xw.screen, xw.win, xw.width, xw.height);
    xw.font = font_create(xw.dpy, "fixed");

    /* GUI */
    font.userdata = zr_handle_ptr(xw.font);
    font.height = (float)xw.font->height;
    font.width = font_get_text_width;
    memset(&gui, 0, sizeof gui);
    gui.memory = calloc(MAX_MEMORY, 1);
    zr_init_fixed(&gui.ctx, gui.memory, MAX_MEMORY, &font);

    while (running) {
        /* Input */
        XEvent evt;
        started = timestamp();
        zr_input_begin(&gui.ctx);
        while (XCheckWindowEvent(xw.dpy, xw.win, xw.swa.event_mask, &evt)) {
            if (XFilterEvent(&evt, xw.win)) continue;
            if (evt.type == KeyPress)
                input_key(&xw, &gui.ctx, &evt, zr_true);
            else if (evt.type == KeyRelease)
                input_key(&xw, &gui.ctx, &evt, zr_false);
            else if (evt.type == ButtonPress)
                input_button(&gui.ctx, &evt, zr_true);
            else if (evt.type == ButtonRelease)
                input_button(&gui.ctx, &evt, zr_false);
            else if (evt.type == MotionNotify)
                input_motion(&gui.ctx, &evt);
            else if (evt.type == Expose || evt.type == ConfigureNotify)
                resize(&xw, xw.surf);
            else if (evt.type == KeymapNotify)
                XRefreshKeyboardMapping(&evt.xmapping);
        }
        zr_input_end(&gui.ctx);

        /* GUI */
        running = run_demo(&gui);

        /* Draw */
        XClearWindow(xw.dpy, xw.win);
        surface_clear(xw.surf, 0x00303030);
        {
            const struct zr_command *cmd;
            zr_foreach(cmd, &gui.ctx) {
                switch (cmd->type) {
                case ZR_COMMAND_NOP: break;
                case ZR_COMMAND_SCISSOR: {
                    const struct zr_command_scissor *s = zr_command(scissor, cmd);
                    surface_scissor(xw.surf, s->x, s->y, s->w, s->h);
                } break;
                case ZR_COMMAND_LINE: {
                    const struct zr_command_line *l = zr_command(line, cmd);
                    surface_draw_line(xw.surf, l->begin.x, l->begin.y, l->end.x,
                        l->end.y, l->color);
                } break;
                case ZR_COMMAND_RECT: {
                    const struct zr_command_rect *r = zr_command(rect, cmd);
                    surface_draw_rect(xw.surf, r->x, r->y, r->w, r->h, (uint16_t)r->rounding, r->color);
                } break;
                case ZR_COMMAND_CIRCLE: {
                    const struct zr_command_circle *c = zr_command(circle, cmd);
                    surface_draw_circle(xw.surf, c->x, c->y, c->w, c->h, c->color);
                } break;
                case ZR_COMMAND_TRIANGLE: {
                    const struct zr_command_triangle *t = zr_command(triangle, cmd);
                    surface_draw_triangle(xw.surf, t->a.x, t->a.y, t->b.x, t->b.y,
                        t->c.x, t->c.y, t->color);
                } break;
                case ZR_COMMAND_TEXT: {
                    const struct zr_command_text *t = zr_command(text, cmd);
                    surface_draw_text(xw.surf, t->x, t->y, t->w, t->h,
                        (const char*)t->string, t->length,
                        (XFont*)t->font->userdata.ptr,
                        t->background, t->foreground);
                } break;
                case ZR_COMMAND_CURVE:
                case ZR_COMMAND_IMAGE:
                case ZR_COMMAND_ARC:
                default: break;
                }
            }
            zr_clear(&gui.ctx);
        }
        surface_blit(xw.win, xw.surf, xw.width, xw.height);
        XFlush(xw.dpy);

        /* Timing */
        dt = timestamp() - started;
        if (dt < DTIME)
            sleep_for(DTIME - dt);
    }

    free(gui.memory);
    font_del(xw.dpy, xw.font);
    surface_del(xw.surf);
    XUnmapWindow(xw.dpy, xw.win);
    XFreeColormap(xw.dpy, xw.cmap);
    XDestroyWindow(xw.dpy, xw.win);
    XCloseDisplay(xw.dpy);
    return 0;
}


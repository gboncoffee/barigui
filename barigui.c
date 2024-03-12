#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/X.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#define DRW_IMPLEMENTATION
#include "drw.h"

/*
 * Copyright (C) 2024  Gabriel de Brito
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define LENGTH(X) (sizeof X / sizeof X[0])

typedef struct Client {
	char *name;
	Window id;
	Window title;
	GC title_gc;
	int req_w;
	int req_h;
	int req_x;
	int req_y;
	struct Client *next;
	struct Client *prev;
} Client;

typedef struct Cursors {
	Cursor left_ptr;
	Cursor fleur;
	Cursor sizing;
} Cursors;

typedef struct {
	Window win;
	Drw *drw;
	Clr *color;
	Clr *color_f;
	Fnt *font;
} Dock;

typedef struct {
	Client *floating;
	Client *tiled;
	Client *hidden;
	Client *current;
	int n_hidden;
	int n_tiled;
	Dock menu;
	Dock right_bar;
	Dock left_bar;
	unsigned int hid_w;
	unsigned int spawn_w;
	unsigned int bar_h;
	char *status;
	Display *dpy;
	int screen;
	int sw;
	int sh;
	Window root;
	Cursors cursors;
	KeyCode fkey;
} Wm;

typedef struct {
	const char *label;
	const char **command;
	unsigned int lsize;
	unsigned int w;
} MenuItem;

#include "config.h"

void handle_event(Wm *i, XEvent *ev);

int error_handler(Display *dpy, XErrorEvent *e)
{
	return 0;
}

typedef struct {
	Client *c;
	short int is_float;
	short int is_hidden;
	short int is_tiled;
	short int is_title;
} FindResult;

FindResult find_window(Wm *wm, Window win)
{
	Client *c;
	FindResult r;
	r.c = NULL;
	r.is_float = 0;
	r.is_hidden = 0;
	r.is_tiled = 0;
	r.is_title = 0;

	for (c = wm->floating; c != NULL; c = c->next) {
		if (c->id == win || (r.is_title = (c->title == win))) {
			r.is_float = 1;
			r.c = c;
			return r;
		}
	}
	for (c = wm->tiled; c != NULL; c = c->next) {
		if (c->id == win || (r.is_title = (c->title == win))) {
			r.is_tiled = 1;
			r.c = c;
			return r;
		}
	}
	for (c = wm->hidden; c != NULL; c = c->next) {
		if (c->id == win || (r.is_title = (c->title == win))) {
			r.is_hidden = 1;
			r.c = c;
			return r;
		}
	}

	return r;
}

void render_right_bar(Wm *wm)
{
	unsigned int w, h;
	Dock *bar = &wm->right_bar;

	if (wm->status != NULL) {
		drw_font_getexts(bar->font, wm->status, strlen(wm->status), &w, &h);
		XMoveResizeWindow(wm->dpy, bar->win, wm->sw - w - BORDER_WIDTH * 2, BORDER_WIDTH, w, h);
		drw_resize(bar->drw, w, h);
		drw_text(bar->drw, 0, 0, w, h, 0, wm->status, 0);
		drw_map(bar->drw, bar->win, 0, 0, w, h);
	}
}

/* 0 = no opened, 1 = hidden, 2 = spawn. */
void render_left_bar(Wm *wm, short int opened)
{
	unsigned int w, h, totw, toth;
	Dock *bar = &wm->left_bar;
	char *hidt = " Hidden ";
	char *spawnt = " Spawn  ";
	int i;

	if (opened == 1)
		hidt = "[Hidden]";
	else if (opened == 2)
		spawnt = "[Spawn] ";

	drw_font_getexts(bar->font, hidt, 8, &w, &h);
	wm->hid_w = w;
	toth = h;
	drw_font_getexts(bar->font, spawnt, 8, &w, &h);
	wm->spawn_w = w;
	toth = h > toth ? h : toth;
	totw = wm->hid_w + wm->spawn_w;

	for (i = 0; i < LENGTH(items); i++) {
		drw_font_getexts(bar->font, items[i].label, items[i].lsize, &items[i].w, &h);
		totw += items[i].w;
	}

	XMoveResizeWindow(wm->dpy, bar->win, BORDER_WIDTH, BORDER_WIDTH, totw, toth);
	drw_resize(bar->drw, totw, toth);
	drw_text(bar->drw, 0, 0, wm->hid_w, toth, 0, hidt, 0);
	drw_text(bar->drw, wm->hid_w, 0, wm->spawn_w, toth, 0, spawnt, 0);

	w = wm->hid_w + wm->spawn_w;
	for (i = 0; i < LENGTH(items); i++) {
		drw_text(bar->drw, w, 0, items[i].w, toth, 0, items[i].label, 0);
		w += items[i].w;
	}

	drw_map(bar->drw, bar->win, 0, 0, totw, toth);
}

void render_title(Wm *wm, Client *c, short int focus)
{
	Window _dumbw;
	unsigned int w, h, _dumbu;
	int _dumbi;

	XSetWindowBackground(wm->dpy, c->title, focus ? TITLE_FOCUS_COLOR : TITLE_COLOR);
	XClearWindow(wm->dpy, c->title);

	XGetGeometry(wm->dpy, c->title, &_dumbw, &_dumbi, &_dumbi, &w, &h, &_dumbu, &_dumbu);

	XSetForeground(wm->dpy, c->title_gc, CLOSE_BUTTON_COLOR);
	XFillRectangle(wm->dpy, c->title, c->title_gc, 0, 0, w, w);
	XSetForeground(wm->dpy, c->title_gc, HIDE_BUTTON_COLOR);
	XFillRectangle(wm->dpy, c->title, c->title_gc, 0, h - w, w, w);
}

void restore_focus(Wm *wm)
{
	Client *c;
	int height, title_x, i;

#define FORCLIENT(cli) \
	for (c = (cli); c != NULL; c = c->next) { \
		XGrabButton(wm->dpy, \
			AnyButton, \
			AnyModifier, \
			c->id, \
			False, \
			ButtonPressMask, \
			GrabModeAsync, \
			GrabModeSync, \
			None, \
			None); \
		render_title(wm, c, 0); \
	}

	FORCLIENT(wm->floating)
	FORCLIENT(wm->tiled)

	if (wm->current == NULL)
		return;

	XRaiseWindow(wm->dpy, wm->current->id);
	XRaiseWindow(wm->dpy, wm->current->title);

	if (wm->n_tiled == 0)
		goto current;

	XLowerWindow(wm->dpy, wm->tiled->id);
	XLowerWindow(wm->dpy, wm->tiled->title);
	if (wm->n_tiled == 1) {
		XMoveResizeWindow(
			wm->dpy,
			wm->tiled->id,
			BORDER_WIDTH,
			wm->bar_h + BORDER_WIDTH * 3,
			wm->sw - BORDER_WIDTH * 4 - TITLE_WIDTH,
			wm->sh - wm->bar_h - BORDER_WIDTH * 4);
		XMoveResizeWindow(
			wm->dpy,
			wm->tiled->title,
			wm->sw - BORDER_WIDTH - TITLE_WIDTH,
			wm->bar_h + BORDER_WIDTH * 3,
			TITLE_WIDTH,
			wm->sh - wm->bar_h - BORDER_WIDTH * 4);
		render_title(wm, wm->tiled, 0);
		goto current;
	}

	XMoveResizeWindow(
		wm->dpy,
		wm->tiled->id,
		BORDER_WIDTH,
		wm->bar_h + BORDER_WIDTH * 3,
		wm->sw / 2 - BORDER_WIDTH * 4 - TITLE_WIDTH,
		wm->sh - wm->bar_h - BORDER_WIDTH * 4);
	XMoveResizeWindow(
		wm->dpy,
		wm->tiled->title,
		wm->sw / 2 - BORDER_WIDTH - TITLE_WIDTH,
		wm->bar_h + BORDER_WIDTH * 3,
		TITLE_WIDTH,
		wm->sh - wm->bar_h - BORDER_WIDTH * 4);
	render_title(wm, wm->tiled, 0);

	height = (wm->sh - (wm->bar_h + BORDER_WIDTH * 2)) / (wm->n_tiled - 1);
	title_x = wm->sw - BORDER_WIDTH - TITLE_WIDTH;

	i = 0;
	for (c = wm->tiled->next; c != NULL; c = c->next) {
		XLowerWindow(wm->dpy, c->id);
		XLowerWindow(wm->dpy, c->title);
		XMoveResizeWindow(
			wm->dpy,
			c->id,
			wm->sw / 2 + BORDER_WIDTH,
			height * i + BORDER_WIDTH + (wm->bar_h + BORDER_WIDTH * 2),
			title_x - (wm->sw / 2 + BORDER_WIDTH),
			height - BORDER_WIDTH * 2);
		XMoveResizeWindow(
			wm->dpy,
			c->title,
			title_x,
			height * i + BORDER_WIDTH + (wm->bar_h + BORDER_WIDTH * 2),
			TITLE_WIDTH,
			height - BORDER_WIDTH * 2);
		render_title(wm, c, 0);

		i++;
	}

current:
	XSetInputFocus(wm->dpy, wm->current->id, RevertToParent, CurrentTime);
	XUngrabButton(wm->dpy, AnyButton, AnyModifier, wm->current->id);
	render_title(wm, wm->current, 1);
}

void fullscreen(Wm *wm)
{
	Client *i;
	Client *c;
	XEvent ev;

	if (wm->current == NULL)
		return;
	XMoveResizeWindow(wm->dpy, wm->current->id, 0, 0, wm->sw, wm->sh);
	XRaiseWindow(wm->dpy, wm->current->id);

	for (;;) {
		XSync(wm->dpy, False);
		XNextEvent(wm->dpy, &ev);

		switch (ev.type) {
		case MapRequest:
		case DestroyNotify:
		case ButtonPress:
			handle_event(wm, &ev);
			break;
		case KeyPress:
			goto out;
		}
	}
out:
	for (i = wm->tiled; i != NULL; i = i->next) {
		if (i == wm->current) {
			restore_focus(wm);
			return;
		}
	}

	c = wm->current;
	XMoveResizeWindow(wm->dpy, c->id, c->req_x, c->req_y, c->req_w, c->req_h);
	XMoveResizeWindow(
		wm->dpy,
		c->title,
		c->req_x + c->req_w + BORDER_WIDTH * 2,
		c->req_y,
		TITLE_WIDTH,
		c->req_h);
	restore_focus(wm);
}

void unhide_by_idx(Wm *wm, int sel)
{
	Client *c = wm->hidden;

	for (; c != NULL; c = c->next) {
		if (sel == 0) {
			XMapRaised(wm->dpy, c->id);
			XMapRaised(wm->dpy, c->title);
			XMoveResizeWindow(wm->dpy, c->id, c->req_x, c->req_y, c->req_w, c->req_h);
			XMoveResizeWindow(
				wm->dpy,
				c->title,
				c->req_x + c->req_w + BORDER_WIDTH * 2,
				c->req_y,
				TITLE_WIDTH,
				c->req_h);
			if (c->prev != NULL)
				c->prev->next = c->next;
			if (c->next != NULL)
				c->next->prev = c->prev;
			if (wm->hidden == c) {
				wm->hidden = c->prev;
				if (wm->hidden == NULL)
					wm->hidden = c->next;
			}
			c->prev = NULL;
			if (wm->floating != NULL)
				wm->floating->prev = c;
			c->next = wm->floating;
			wm->floating = c;
			wm->current = c;
			restore_focus(wm);
			wm->n_hidden--;
			return;
		}

		sel--;
	}
}

int draw_hidden_menu(Wm *wm, int x, int y, int curx, int cury, unsigned int w, unsigned int h)
{
	Client *c;
	int in_menu = curx >= x && cury >= y && curx < x + w && cury < y + h;
	int i = 0;
	int t = h / wm->n_hidden;
	int r = -1;

	for (c = wm->hidden; c != NULL; c = c->next) {
		if (in_menu && cury - y >= t * i && cury - y < t * (i + 1)) {
			drw_setscheme(wm->menu.drw, wm->menu.color_f);
			r = i;
		} else {
			drw_setscheme(wm->menu.drw, wm->menu.color);
		}

		if (c->name != NULL)
			drw_text(wm->menu.drw, 0, t * i, w, t, 0, c->name, 0);

		i++;
	}

	drw_map(wm->menu.drw, wm->menu.win, 0, 0, w, h);

	return r;
}

void hidden_window(Wm *wm)
{
	XEvent ev;
	unsigned int w, h, _dumbu;
	Window _dumbw;
	int _dumbi, x, y, curx, cury;
	int sel = -1;

	if (wm->n_hidden == 0)
		return;

	XMapRaised(wm->dpy, wm->menu.win);

	XGetGeometry(wm->dpy, wm->left_bar.win, &_dumbw, &_dumbi, &_dumbi, &w, &h, &_dumbu, &_dumbu);
	x = BORDER_WIDTH;
	y = h + BORDER_WIDTH * 2;
	h = h * wm->n_hidden;
	XMoveResizeWindow(wm->dpy, wm->menu.win, x, y, w, h);
	drw_resize(wm->menu.drw, w, h * wm->n_hidden);

	XGrabPointer(wm->dpy,
		wm->menu.win,
		False,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
		wm->cursors.left_ptr,
		CurrentTime);

	for (;;) {
		XSync(wm->dpy, False);
		XNextEvent(wm->dpy, &ev);

		switch (ev.type) {
		/* Draw window as soon as the button is released. */
		case ButtonRelease:
		case MotionNotify:
			curx = ev.xbutton.x_root;
			cury = ev.xbutton.y_root;
			if (curx > x + w || cury > y + h)
				goto unmap;
			sel = draw_hidden_menu(
				wm,
				x,
				y,
				curx,
				cury,
				w,
				h);
			break;
		case ButtonPress:
			curx = ev.xbutton.x_root;
			cury = ev.xbutton.y_root;
			if (curx > x + w || cury > y + h)
				goto unmap;
			sel = draw_hidden_menu(
				wm,
				x,
				y,
				curx,
				cury,
				w,
				h);
			goto unhide;
		default:
			handle_event(wm, &ev);
		}
	}
unhide:
	if (sel > -1)
		unhide_by_idx(wm, sel);

unmap:
	XUnmapWindow(wm->dpy, wm->menu.win);
	XUngrabPointer(wm->dpy, CurrentTime);
}

int draw_spawn_menu(Wm *wm, int x, int y, int curx, int cury, unsigned int w, unsigned int h)
{
	int in_menu = curx >= x && cury >= y && curx < x + w && cury < y + h;
	int i;
	int t = h / LENGTH(spawn_items);
	int r = -1;

	for (i = 0; i < LENGTH(spawn_items); i++) {
		if (in_menu && cury - y >= t * i && cury - y < t * (i + 1)) {
			drw_setscheme(wm->menu.drw, wm->menu.color_f);
			r = i;
		} else {
			drw_setscheme(wm->menu.drw, wm->menu.color);
		}

		drw_text(wm->menu.drw, 0, t * i, w, t, 0, spawn_items[i].label, 0);
	}

	drw_map(wm->menu.drw, wm->menu.win, 0, 0, w, h);

	return r;
}

void spawn_window(Wm *wm)
{
	XEvent ev;
	unsigned int w, h, _dumbu;
	Window _dumbw;
	int _dumbi, x, y, curx, cury;
	int sel = -1;

	XMapRaised(wm->dpy, wm->menu.win);

	XGetGeometry(wm->dpy, wm->left_bar.win, &_dumbw, &_dumbi, &_dumbi, &w, &h, &_dumbu, &_dumbu);
	x = BORDER_WIDTH;
	y = h + BORDER_WIDTH * 2;
	h = h * LENGTH(spawn_items);
	XMoveResizeWindow(wm->dpy, wm->menu.win, x, y, w, h);
	drw_resize(wm->menu.drw, w, h * LENGTH(spawn_items));

	XGrabPointer(wm->dpy,
		wm->menu.win,
		False,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
		wm->cursors.left_ptr,
		CurrentTime);

	for (;;) {
		XSync(wm->dpy, False);
		XNextEvent(wm->dpy, &ev);

		switch (ev.type) {
		/* Draw window as soon as the button is released. */
		case ButtonRelease:
		case MotionNotify:
			curx = ev.xbutton.x_root;
			cury = ev.xbutton.y_root;
			if (curx > x + w || cury > y + h)
				goto unmap;
			sel = draw_spawn_menu(
				wm,
				x,
				y,
				curx,
				cury,
				w,
				h);
			break;
		case ButtonPress:
			curx = ev.xbutton.x_root;
			cury = ev.xbutton.y_root;
			if (curx > x + w || cury > y + h)
				goto unmap;
			sel = draw_spawn_menu(
				wm,
				x,
				y,
				curx,
				cury,
				w,
				h);
			goto unhide;
		default:
			handle_event(wm, &ev);
		}
	}
unhide:
	if (sel > -1) {
		if (fork() == 0) {
			execvp(spawn_items[sel].command[0], (char * const *) spawn_items[sel].command);
			exit(1);
		}
	}

unmap:
	XUnmapWindow(wm->dpy, wm->menu.win);
	XUngrabPointer(wm->dpy, CurrentTime);
}

void left_bar_click(Wm *wm, XButtonEvent *e)
{
	unsigned int w;
	int i;

	if (e->x < wm->hid_w) {
		hidden_window(wm);
		return;
	}

	w = wm->hid_w + wm->spawn_w;
	if (e->x < w) {
		spawn_window(wm);
		return;
	}

	for (i = 0; i < LENGTH(items); i++) {
		w += items[i].w;
		if (e->x < w) {
			if (fork() == 0) {
				execvp(items[i].command[0], (char * const *) items[i].command);
				exit(1);
			}
			break;
		}
	}
}

void unmanage_floating(Wm *wm, Client *c)
{
	if (c->prev != NULL)
		c->prev->next = c->next;
	if (c->next != NULL)
		c->next->prev = c->prev;
	if (wm->floating == c)
		wm->floating = c->next;
	if (wm->current == c) {
		if (c->next != NULL)
			wm->current = c->next;
		else if (c->prev != NULL)
			wm->current = c->prev;
		else if (wm->tiled != NULL)
			wm->current = wm->tiled;
		else
			wm->current = NULL;
	}

	if (c->name != NULL)
		XFree(c->name);
	XFreeGC(wm->dpy, c->title_gc);
	XDestroyWindow(wm->dpy, c->title);
	free(c);

	restore_focus(wm);
}

void unmanage_tiled(Wm *wm, Client *c)
{
	if (c->prev != NULL)
		c->prev->next = c->next;
	if (c->next != NULL)
		c->next->prev = c->prev;
	if (wm->tiled == c)
		wm->tiled = c->next;
	if (wm->current == c) {
		if (c->next != NULL)
			wm->current = c->next;
		else if (c->prev != NULL)
			wm->current = c->prev;
		else if (wm->floating != NULL)
			wm->current = wm->floating;
		else
			wm->current = NULL;
	}

	if (c->name != NULL)
		XFree(c->name);
	XFreeGC(wm->dpy, c->title_gc);
	XDestroyWindow(wm->dpy, c->title);
	free(c);

	wm->n_tiled--;
	restore_focus(wm);
}

void unmanage_hidden(Wm *wm, Client *c)
{
	if (c->prev != NULL)
		c->prev->next = c->next;
	if (c->next != NULL)
		c->next->prev = c->prev;
	if (wm->hidden == c)
		wm->hidden = c->next;
	if (c->name != NULL)
		XFree(c->name);
	XFreeGC(wm->dpy, c->title_gc);
	XDestroyWindow(wm->dpy, c->title);
	free(c);

	wm->n_hidden--;
}

void resize_client(Wm *wm, Client *c, int padx, int pady)
{
	XEvent ev;
	XButtonEvent e;
	int x, y;
	Window _dumbw;
	unsigned int w, h, _dumbu;

	XGetGeometry(wm->dpy, c->id, &_dumbw, &x, &y, &w, &h, &_dumbu, &_dumbu);

	XGrabPointer(
		wm->dpy,
		wm->root,
		True,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
		wm->cursors.sizing,
		CurrentTime);

	for (;;) {
		XSync(wm->dpy, False);
		XNextEvent(wm->dpy, &ev);

		switch (ev.type) {
		case ButtonRelease:
			goto out;
			break;
		case MotionNotify:
			e = ev.xbutton;
			w = e.x_root - x - padx;
			h = e.y_root - y + pady;
			XResizeWindow(wm->dpy, c->id, w, h);
			XMoveResizeWindow(wm->dpy, c->title, x + w + BORDER_WIDTH * 2, y, TITLE_WIDTH, h);
			render_title(wm, c, 1);
			break;
		default:
			handle_event(wm, &ev);
			break;
		}
	}
out:
	c->req_w = w;
	c->req_h = h;
	XUngrabPointer(wm->dpy, CurrentTime);
}

void move_client(Wm *wm, Client *c, int padx, int pady)
{
	XEvent ev;
	XButtonEvent e;
	int x, y;
	Window _dumbw;
	unsigned int _dumbu, w, h;

	XGetGeometry(wm->dpy, c->id, &_dumbw, &x, &y, &w, &h, &_dumbu, &_dumbu);

	XGrabPointer(
		wm->dpy,
		wm->root,
		True,
		PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync,
		GrabModeAsync,
		None,
		wm->cursors.fleur,
		CurrentTime);

	for (;;) {
		XSync(wm->dpy, False);
		XNextEvent(wm->dpy, &ev);

		switch (ev.type) {
		case ButtonRelease:
			goto out;
			break;
		case MotionNotify:
			e = ev.xbutton;
			x = e.x_root - w - padx;
			y = e.y_root - h + pady;
			XMoveWindow(wm->dpy, c->id, x, y);
			XMoveWindow(wm->dpy, c->title, x + w + BORDER_WIDTH * 2, y);
			render_title(wm, c, 1);
			break;
		default:
			handle_event(wm, &ev);
			break;
		}
	}
out:
	c->req_x = x;
	c->req_y = y;
	XUngrabPointer(wm->dpy, CurrentTime);
}

void hide_client(Wm *wm, Client *c)
{
	Client *i;

	for (i = wm->tiled; i != NULL; i = i->next) {
		if (i == c) {
			wm->n_tiled--;
			break;
		}
	}

	if (c->prev != NULL)
		c->prev->next = c->next;
	if (c->next != NULL)
		c->next->prev = c->prev;
	if (wm->floating == c)
		wm->floating = c->next;
	if (wm->tiled == c)
		wm->tiled = c->next;
	if (wm->current == c) {
		if (c->next != NULL)
			wm->current = c->next;
		else if (c->prev != NULL)
			wm->current = c->prev;
		else if (wm->tiled != NULL)
			wm->current = wm->tiled;
		else if (wm->floating != NULL)
			wm->current = wm->floating;
		else
			wm->current = NULL;
	}

	c->next = wm->hidden;
	if (wm->hidden != NULL)
		wm->hidden->prev = c;
	wm->hidden = c;
	c->prev = NULL;
	XUnmapWindow(wm->dpy, c->id);
	XUnmapWindow(wm->dpy, c->title);

	wm->n_hidden++;

	restore_focus(wm);
}

void tile_client(Wm *wm, Client *c)
{
	if (c->prev != NULL)
		c->prev->next = c->next;
	if (c->next != NULL)
		c->next->prev = c->prev;
	if (wm->floating == c)
		wm->floating = c->next;

	c->next = wm->tiled;
	if (wm->tiled != NULL)
		wm->tiled->prev = c;
	c->prev = NULL;
	wm->tiled = c;
	wm->n_tiled++;
}

void float_client(Wm *wm, Client *c)
{
	if (c->prev != NULL)
		c->prev->next = c->next;
	if (c->next != NULL)
		c->next->prev = c->prev;
	if (wm->tiled == c)
		wm->tiled = c->next;

	c->next = wm->floating;
	if (wm->floating != NULL)
		wm->floating->prev = c;
	c->prev = NULL;
	wm->floating = c;
	wm->n_tiled--;

	XMoveResizeWindow(wm->dpy, c->id, c->req_x, c->req_y, c->req_w, c->req_h);
	XMoveResizeWindow(
		wm->dpy,
		c->title,
		c->req_x + c->req_w + BORDER_WIDTH * 2,
		c->req_y,
		TITLE_WIDTH,
		c->req_h);
}

void toggle_tile(Wm *wm, Client *c)
{
	Client *i;
	wm->current = c;

	for (i = wm->floating; i != NULL; i = i->next) {
		if (i == c) {
			tile_client(wm, c);
			restore_focus(wm);
			return;
		}
	}

	for (i = wm->tiled; i != NULL; i = i->next) {
		if (i == c) {
			float_client(wm, c);
			restore_focus(wm);
			return;
		}
	}
}

void zoom_tiled_client(Wm *wm, Client *c)
{
	if (c == wm->tiled)
		c = c->next;
	if (c == NULL)
		return;

	if (c->next != NULL)
		c->next->prev = c->prev;
	if (c->prev != NULL)
		c->prev->next = c->next;
	c->prev = NULL;
	c->next = wm->tiled;
	wm->tiled->prev = c;
	wm->tiled = c;
	wm->current = c;

	restore_focus(wm);
}

void title_click(Wm *wm, Client *c, XButtonEvent *e)
{
	Window _dumbw;
	unsigned int w, h, _dumbu;
	int _dumbi;
	Client *i;

	XGetGeometry(wm->dpy, c->title, &_dumbw, &_dumbi, &_dumbi, &w, &h, &_dumbu, &_dumbu);

	if (e->y <= w) {
		if (e->button == Button3)
			XKillClient(wm->dpy, c->id);
		else
			hide_client(wm, c);
	} else if (e->y >= h - w) {
		for (i = wm->tiled; i != NULL; i = i->next) {
			if (i == c) {
				zoom_tiled_client(wm, c);
				return;
			}
		}
		resize_client(wm, c, e->x + BORDER_WIDTH * 2, h - e->y);
	} else {
		if (e->button == Button3) {
			toggle_tile(wm, c);
		} else {
			for (i = wm->tiled; i != NULL; i = i->next) {
				if (i == c) {
					wm->current = c;
					restore_focus(wm);
					return;
				}
			}
			move_client(wm, c, e->x + BORDER_WIDTH * 2, h - e->y);
		}
	}
}

void manage(Wm *wm, Window win)
{
	XTextProperty prop;
	unsigned int _dumbu, w, h;
	Window _dumbw;
	Client *new = malloc(sizeof(Client));
	assert(new != NULL && "Buy more ram lol");

	new->id = win;

	XGetGeometry(wm->dpy, win, &_dumbw, &new->req_x, &new->req_y, &w, &h, &_dumbu, &_dumbu);
	new->req_w = (int) w;
	new->req_h = (int) h;

	new->name = NULL;
	if (XGetWMName(wm->dpy, win, &prop))
		new->name = (char*) prop.value;

	XSetWindowBorder(wm->dpy, win, BORDER_COLOR);
	XSetWindowBorderWidth(wm->dpy, win, BORDER_WIDTH);

	new->title = XCreateSimpleWindow(
		wm->dpy,
		wm->root,
		new->req_x + new->req_w + BORDER_WIDTH * 2,
		new->req_y,
		TITLE_WIDTH,
		new->req_h,
		BORDER_WIDTH,
		BORDER_COLOR,
		TITLE_COLOR);
	new->title_gc = XCreateGC(wm->dpy, new->title, 0, NULL);

	XSelectInput(wm->dpy, new->title, ExposureMask | ButtonPressMask);
	XSelectInput(wm->dpy, win, PointerMotionMask | PropertyChangeMask);

	XMapWindow(wm->dpy, win);
	XMapWindow(wm->dpy, new->title);

	if (wm->floating != NULL)
		wm->floating->prev = new;
	new->next = wm->floating;
	new->prev = NULL;
	wm->floating = new;
	wm->current = new;

	restore_focus(wm);
}

void map_request(Wm *wm, XEvent *ev)
{
	XWindowAttributes wa;
	XMapRequestEvent *e = &ev->xmaprequest;
	if (e->window == wm->right_bar.win
		|| e->window == wm->left_bar.win
		|| e->window == wm->menu.win
		|| !XGetWindowAttributes(wm->dpy, e->window, &wa)
		|| wa.override_redirect
		|| find_window(wm, e->window).c != NULL)
	{
		return;
	}

	manage(wm, e->window);
}

void destroy_notify(Wm *wm, XEvent *ev)
{
	XDestroyWindowEvent *e = &ev->xdestroywindow;
	FindResult r = find_window(wm, e->window);
	if (r.c != NULL) {
		if (r.is_float)
			unmanage_floating(wm, r.c);
		if (r.is_hidden) {
			unmanage_hidden(wm, r.c);
		}
		if (r.is_tiled)
			unmanage_tiled(wm, r.c);
	}
}

void button_press(Wm *wm, XEvent *ev)
{
	XButtonEvent *e = &ev->xbutton;

	if (e->window == wm->left_bar.win)
		left_bar_click(wm, e);

	FindResult r = find_window(wm, e->window);
	if (r.c != NULL) {
		wm->current = r.c;
		restore_focus(wm);
		if (r.is_title)
			title_click(wm, r.c, e);
	}
}

void property_change(Wm *wm, XEvent *ev)
{
	XTextProperty prop;
	XPropertyEvent *e = &ev->xproperty;
	FindResult r;
	if (e->window == wm->root) {
		XFree(wm->status);
		wm->status = NULL;
		if (XGetWMName(wm->dpy, wm->root, &prop)) {
			wm->status = (char*) prop.value;
			render_right_bar(wm);
		}
	} else {
		r = find_window(wm, e->window);
		if (r.c != NULL && !r.is_title) {
			if (r.c->name != NULL)
				XFree(r.c->name);
			if (XGetWMName(wm->dpy, r.c->id, &prop))
				r.c->name = (char*) prop.value;
		}
	}
}

void expose(Wm *wm, XEvent *ev)
{
	XExposeEvent *e = &ev->xexpose;
	FindResult r;

	if (e->window == wm->right_bar.win)
		render_right_bar(wm);
	if (e->window == wm->left_bar.win)
		render_left_bar(wm, 0);
	r = find_window(wm, e->window);
	if (r.is_title && r.c != NULL)
		render_title(wm, r.c, r.c == wm->current);
}

void configure_request(Wm *wm, XEvent *ev)
{
	XConfigureRequestEvent *e = &ev->xconfigurerequest;
	FindResult r = find_window(wm, e->window);

	if (r.c == NULL || r.is_title || r.is_tiled)
		return;

	r.c->req_x = e->value_mask & CWX ? e->x : r.c->req_x;
	r.c->req_y = e->value_mask & CWY ? e->y : r.c->req_y;
	r.c->req_w = e->value_mask & CWWidth ? e->width : r.c->req_w;
	r.c->req_h = e->value_mask & CWHeight ? e->height : r.c->req_h;

	XMoveResizeWindow(wm->dpy, e->window, r.c->req_x, r.c->req_y, r.c->req_w, r.c->req_h);
	XMoveResizeWindow(
		wm->dpy,
		r.c->title,
		r.c->req_x + r.c->req_w + BORDER_WIDTH * 2,
		r.c->req_y,
		TITLE_WIDTH,
		r.c->req_h);
}

void key_press(Wm *wm, XEvent *ev)
{
	XKeyEvent *e = &ev->xkey;

	if (e->state == MODMASK && wm->fkey == e->keycode)
		fullscreen(wm);
}

void handle_event(Wm *wm, XEvent *ev)
{
	switch (ev->type) {
	case PropertyNotify:
		property_change(wm, ev);
		break;
	case Expose:
		expose(wm, ev);
		break;
	case MapRequest:
		map_request(wm, ev);
		break;
	case DestroyNotify:
		destroy_notify(wm, ev);
		break;
	case ButtonPress:
		button_press(wm, ev);
		break;
	case ConfigureRequest:
		configure_request(wm, ev);
		break;
	case KeyPress:
		key_press(wm, ev);
		break;
	}
}

void main_loop(Wm *wm)
{
	XEvent ev;

	for (;;) {
		XSync(wm->dpy, False);
		XNextEvent(wm->dpy, &ev);
		handle_event(wm, &ev);
	}
}

void init_right_bar(Wm *wm)
{
	XTextProperty prop;
	wm->status = NULL;
	if (XGetWMName(wm->dpy, wm->root, &prop))
		wm->status = (char*) prop.value;
	XMapWindow(wm->dpy, wm->right_bar.win);
	render_right_bar(wm);
}

void init_left_bar(Wm *wm)
{
	unsigned int _dumbu;
	Window _dumbw;
	int _dumbi;

	XMapWindow(wm->dpy, wm->left_bar.win);
	render_left_bar(wm, 0);

	XGetGeometry(wm->dpy, wm->left_bar.win, &_dumbw, &_dumbi, &_dumbi, &_dumbu, &wm->bar_h, &_dumbu, &_dumbu);
}

void init_dock_or_die(Wm *wm, Dock *dock)
{
	long mask;

	dock->drw = drw_create(wm->dpy, wm->screen, wm->root, 10, 10);
	if (dock->drw == NULL)
		exit(1);
	dock->font = drw_fontset_create(dock->drw, font, 1);
	if (dock->font == NULL)
		exit(1);
	dock->color = drw_scm_create(dock->drw, color, 2);
	if (dock->color == NULL)
		exit(1);
	dock->color_f = drw_scm_create(dock->drw, color_f, 2);
	if (dock->color_f == NULL)
		exit(1);

	drw_setscheme(dock->drw, dock->color);
	dock->win = XCreateSimpleWindow(
		wm->dpy,
		wm->root,
		0,
		0,
		10,
		10,
		BORDER_WIDTH,
		BORDER_COLOR,
		BAR_BACKGROUND);
	mask = ExposureMask | ButtonPressMask;
	XSelectInput(wm->dpy, dock->win, mask);
}

int main(void)
{
	Wm wm;

	if (!(wm.dpy = XOpenDisplay(NULL)))
		return 1;

	wm.screen = DefaultScreen(wm.dpy);
	wm.sw = DisplayWidth(wm.dpy, wm.screen);
	wm.sh = DisplayHeight(wm.dpy, wm.screen);
	wm.root = RootWindow(wm.dpy, wm.screen);

	XStoreName(wm.dpy, wm.root, "barigui");

	wm.floating = NULL;
	wm.tiled = NULL;
	wm.hidden = NULL;
	wm.current = NULL;
	wm.n_tiled = 0;
	wm.n_hidden = 0;

	/* Register to get the events. */
	long mask = SubstructureRedirectMask
		| SubstructureNotifyMask
		| ButtonPressMask
		| ButtonReleaseMask
		| StructureNotifyMask
		| PropertyChangeMask;
	XSelectInput(wm.dpy, wm.root, mask);

	init_dock_or_die(&wm, &wm.left_bar);
	init_dock_or_die(&wm, &wm.right_bar);
	init_dock_or_die(&wm, &wm.menu);

	/* Create the cursors. */
	wm.cursors.left_ptr = XCreateFontCursor(wm.dpy, 68);
	wm.cursors.fleur = XCreateFontCursor(wm.dpy, 52);
	wm.cursors.sizing = XCreateFontCursor(wm.dpy, 120);

#ifdef BACKGROUND
	XSetWindowBackground(wm.dpy, wm.root, BACKGROUND);
	XClearWindow(wm.dpy, wm.root);
#endif
	XDefineCursor(wm.dpy, wm.root, wm.cursors.left_ptr);

	/* Grab key. */
	wm.fkey = XKeysymToKeycode(wm.dpy, FULLSCREEN_KEY);
	XGrabKey(wm.dpy,
		wm.fkey,
		MODMASK,
		wm.root,
		True,
		GrabModeAsync,
		GrabModeAsync);

	XSetErrorHandler(error_handler);

	init_right_bar(&wm);
	init_left_bar(&wm);

	main_loop(&wm);
}

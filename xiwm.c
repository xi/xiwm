/* See LICENSE file for copyright and license details.
 *
 * Code is based on dwm <https://dwm.suckless.org>
 */

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

/* macros */
#define BUTTONMASK       (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)  (mask & ~(LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define ISVISIBLE(C)     (!(C)->isdock && ((C)->desktop == desktop))
#define LENGTH(X)        (sizeof X / sizeof X[0])
#define MOUSEMASK        (BUTTONMASK|PointerMotionMask)
#define MAX(A, B)        ((A) > (B) ? (A) : (B))
#define WINMASK          (FocusChangeMask|PropertyChangeMask)
#define ROOTMASK         (SubstructureRedirectMask|SubstructureNotifyMask|ButtonPressMask|PointerMotionMask|PropertyChangeMask)

/* enums */
enum {
	NetSupported, NetWMName, NetWMDesktop, NetWMState, NetWMCheck,
	NetWMFullscreen, NetActiveWindow, NetWMWindowType,
	NetWMWindowTypeDialog, NetWMWindowTypeDock,
	NetClientList, NetCurrentDesktop, NetNumberOfDesktops, NetLast
}; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMLast }; /* default atoms */
typedef enum { PFloat, PMax, PLeft, PRight } Position;

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct Client Client;
struct Client {
	int x, y, w, h;
	int fx, fy, fw, fh;
	unsigned int desktop;
	Position position;
	Bool isfixed, isfullscreen, isdock;
	Client *next, *transient;
	Window win;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *class;
	const char *instance;
	unsigned int desktop;
	Position position;
} Rule;

/* actions */
static void tag(const Arg *arg);
static void tagrel(const Arg *arg);
static void view(const Arg *arg);
static void viewrel(const Arg *arg);
static void focusstack(const Arg *arg);
static void setposition(const Arg *arg);
static void setmfact(const Arg *arg);
static void killclient(const Arg *arg);
static void spawn(const Arg *arg);

/* X event handlers */
static void keypress(XEvent *e);
static void buttonpress(XEvent *e);
static void clientmessage(XEvent *e);
static void unmapnotify(XEvent *e);
static void configurerequest(XEvent *e);
static void maprequest(XEvent *e);

/* signals */
static void sigchld(int unused);

/* configuration, allows nested code to access above variables */
#include "config.h"

/* variables */
static const char broken[] = "broken";
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar geometry */
static void (*handler[LASTEvent]) (XEvent *) = {
	[KeyPress] = keypress,
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[UnmapNotify] = unmapnotify,
	[MapRequest] = maprequest,
	[ConfigureRequest] = configurerequest,
};
static Atom wmatom[WMLast], netatom[NetLast];
static unsigned int desktop;
static float mfact = 0.5;
static Display *dpy;
static Client *clients;
static Client *sel[DESKTOPS];
static Window root, wmcheckwin;

void
die(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("can't install SIGCHLD handler:");
	while (0 < waitpid(-1, NULL, WNOHANG));
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

Client *
wintoclient(Window w)
{
	Client *c;

	for (c = clients; c; c = c->next)
		if (c->win == w)
			return c;
	return NULL;
}

void
grabbuttons(Client *c, Bool focused)
{
	unsigned int i;
	unsigned int modifiers[] = { 0, LockMask };

	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	if (!focused)
		XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
			BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
	for (i = 0; i < LENGTH(modifiers); i++) {
		XGrabButton(dpy, Button1, Mod1Mask|modifiers[i], c->win,
			False, BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
		XGrabButton(dpy, Button3, Mod1Mask|modifiers[i], c->win,
			False, BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void)
{
	unsigned int i, j;
	unsigned int modifiers[] = { 0, LockMask };
	KeyCode code;

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < LENGTH(keys); i++)
		if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
			for (j = 0; j < LENGTH(modifiers); j++)
				XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
					True, GrabModeAsync, GrabModeAsync);
}

Bool
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	Bool exists = False;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
xsetclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

void
xsetclientdesktop(Client *c)
{
	XChangeProperty(dpy, c->win, netatom[NetWMDesktop], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *) &(c->desktop), 1);
}

void
resize(Client *c, int x, int y, int w, int h, int bw)
{
	XWindowChanges wc;

	c->x = wc.x = x;
	c->y = wc.y = y;
	c->w = wc.width = w;
	c->h = wc.height = h;
	if (c->position == PFloat && !c->isfullscreen) {
		c->fx = c->x;
		c->fy = c->y;
		c->fw = c->w;
		c->fh = c->h;
	}
	wc.border_width = bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
}

void
layoutcolumn(Position pos, int x, int w)
{
	Client *c;
	unsigned int n = 0;
	int y = bh, h;

	for (c = clients; c; c = c->next)
		if (ISVISIBLE(c) && !c->isfullscreen && c->position == pos)
			n++;

	for (c = clients; c; c = c->next)
		if (ISVISIBLE(c) && !c->isfullscreen && c->position == pos) {
			h = (sh - y) / n;
			resize(c, x, y, w - 2, h - 2, 1);
			y += h;
			n -= 1;
		}
}

void
layout(void)
{
	Client *c;

	// show before hide to avoid flicker
	for (c = clients; c; c = c->next) {
		if (c->isdock || !ISVISIBLE(c))
			continue;
		if (c->isfullscreen)
			resize(c, 0, 0, sw, sh, 0);
		else if (c->position == PFloat)
			resize(c, c->fx, c->fy, c->fw, c->fh, 1);
		else if (c->position == PMax)
			resize(c, 0, bh, sw, sh - bh, 0);
	}

	for (c = clients; c; c = c->next)
		if (!ISVISIBLE(c) && !c->isdock)
			XMoveWindow(dpy, c->win, sw * -2, c->y);

	layoutcolumn(PLeft, 0, sw * mfact);
	layoutcolumn(PRight, sw * mfact, sw - sw * mfact);
}

void
raiseclient(Client *c)
{
	Client *i;

	XRaiseWindow(dpy, c->win);
	for (i = clients; i; i = i->next)
		if (i->transient == c)
			XRaiseWindow(dpy, i->win);
}

void
restack(void)
{
	Client *c;
	XEvent ev;

	if (!sel[desktop])
		return;
	if (sel[desktop]->position == PLeft || sel[desktop]->position == PRight)
		for (c = clients; c; c = c->next)
			if (ISVISIBLE(c) && (c->position == PLeft || c->position == PRight))
				raiseclient(c);
	raiseclient(sel[desktop]);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
setfullscreen(Client *c, Bool fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*) &netatom[NetWMFullscreen], 1);
		c->isfullscreen = True;
		layout();
	} else if (!fullscreen && c->isfullscreen){
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*) 0, 0);
		c->isfullscreen = False;
		layout();
	}
}

void
setdesktop(unsigned int i)
{
	if (i == desktop || i >= DESKTOPS)
		return;
	desktop = i;
	XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *) &desktop, 1);
	layout();
}

Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

void
updatefixed(Client *c)
{
	long msize;
	XSizeHints size;

	c->isfixed = 0;
	if (XGetWMNormalHints(dpy, c->win, &size, &msize))
		if (size.flags & PMaxSize && size.flags & PMinSize)
			c->isfixed = (size.max_width == size.min_width && size.max_height == size.min_height);
}

void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, True);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->position = PFloat;
	if (wtype == netatom[NetWMWindowTypeDock])
		c->isdock = True;
}

void
applyrules(Client *c)
{
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->position = r->position;
			c->desktop = r->desktop;
		}
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
}

void
attach(Client *c)
{
	c->next = clients;
	clients = c;
}

void
detach(Client *c)
{
	int i;
	Client **tc;

	for (tc = &clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;

	for (i = 0; i < DESKTOPS; i++)
		if (c == sel[i])
			sel[i] = c->transient;
}

void
focus(Client *c)
{
	Client *i, *d;

	if (!c || !ISVISIBLE(c))
		for (c = clients; c && !ISVISIBLE(c); c = c->next);
	if (sel[desktop] && sel[desktop] != c) {
		grabbuttons(sel[desktop], False);
		XSetWindowBorder(dpy, sel[desktop]->win, COL_NORM);
	}
	if (c) {
		d = c;
		for (i = clients; i; i = i->next)
			if (i->transient == c)
				d = i;
		XSetWindowBorder(dpy, d->win, COL_HIGH);
		grabbuttons(c, True);
		XSetInputFocus(dpy, d->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
			PropModeReplace, (unsigned char *) &(d->win), 1);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	sel[desktop] = c;
	restack();
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL;
	Window trans = None;

	c = calloc(1, sizeof(Client));
	c->win = w;
	c->position = PMax;
	c->desktop = desktop;
	/* geometry */
	c->fx = c->x = wa->x == 0 ? (sw - wa->width) / 2 : wa->x;
	c->fy = c->y = wa->y == 0 ? (sh + bh - wa->height) / 2 : wa->y;

	c->fw = c->w = wa->width;
	c->fh = c->h = wa->height;

	applyrules(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans)))
		c->desktop = t->desktop;
	c->transient = t;
	xsetclientdesktop(c);
	updatewindowtype(c);
	updatefixed(c);
	if (trans != None || c->isfixed)
		c->position = PFloat;

	if (c->isdock)
		bh = c->h;

	XSetWindowBorder(dpy, c->win, COL_NORM);
	XSelectInput(dpy, w, WINMASK);
	grabbuttons(c, False);
	attach(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
		PropModeAppend, (unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	xsetclientstate(c, NormalState);
	XMapWindow(dpy, c->win);
	focus(NULL);
	layout();
}

void
unmanage(Client *c)
{
	Client *i;

	detach(c);
	free(c);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (i = clients; i; i = i->next)
		XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
			PropModeAppend, (unsigned char *) &(i->win), 1);
	focus(sel[desktop]);
	layout();
}

/* event handlers */
void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
movemouse(void)
{
	int x, y, ocx, ocy, nx, ny, di;
	unsigned int dui;
	Client *c;
	XEvent ev;
	Time lasttime = 0;
	Window dummy;

	if (!(c = sel[desktop]))
		return;
	if (c->isfullscreen || c->position != PFloat)
		return;
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, None, CurrentTime) != GrabSuccess)
		return;
	if (!XQueryPointer(dpy, root, &dummy, &dummy, &x, &y, &di, &di, &dui))
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (c->position == PFloat)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
}

void
resizemouse(void)
{
	int ocx, ocy, nw, nh;
	Client *c;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = sel[desktop]))
		return;
	if (c->isfullscreen || c->position != PFloat)
		return;
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, None, CurrentTime) != GrabSuccess)
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nw = MAX(ev.xmotion.x - ocx - 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 1, 1);
			if (c->position == PFloat)
				resize(c, c->x, c->y, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
buttonpress(XEvent *e)
{
	Client *c;
	XButtonPressedEvent *ev = &e->xbutton;

	if ((c = wintoclient(ev->window))) {
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		if (c->isdock)
			return;
		focus(c);
		if (CLEANMASK(Mod1Mask) == CLEANMASK(ev->state)) {
			if (ev->button == Button1)
				movemouse();
			if (ev->button == Button3)
				resizemouse();
		}
	}
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 || (cme->data.l[0] == 2 && !c->isfullscreen)));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (c != sel[desktop]) {
			setdesktop(c->desktop);
			focus(c);
		}
	}
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			xsetclientstate(c, WithdrawnState);
		else
			unmanage(c);
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	XConfigureEvent ce;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc = { ev->x, ev->y, ev->width, ev->height, ev->border_width, ev->above, ev->detail };

	if ((c = wintoclient(ev->window)) && !c->isdock) {
		ce.type = ConfigureNotify;
		ce.display = dpy;
		ce.event = c->win;
		ce.window = c->win;
		ce.x = c->x;
		ce.y = c->y;
		ce.width = c->w;
		ce.height = c->h;
		ce.border_width = 0;
		ce.above = None;
		ce.override_redirect = False;
		XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
	} else
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if (!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if (wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

/* commands */
void
tag(const Arg *arg)
{
	Client *c;

	if (!(c = sel[desktop]))
		return;
	if (arg->ui >= DESKTOPS)
		return;
	if (c->desktop == arg->ui)
		return;
	sel[desktop] = NULL;
	c->desktop = arg->ui;
	xsetclientdesktop(c);
	setdesktop(arg->ui);
	focus(c);
}

void
tagrel(const Arg *arg)
{
	Arg a = {.ui = desktop + arg->i};
	tag(&a);
}

void
view(const Arg *arg)
{
	setdesktop(arg->ui);
	focus(sel[desktop]);
}

void
viewrel(const Arg *arg)
{
	setdesktop(desktop + arg->i);
	focus(sel[desktop]);
}

void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!sel[desktop])
		return;
	if (arg->i > 0) {
		for (c = sel[desktop]->next; c && (!ISVISIBLE(c) || c->transient); c = c->next);
		if (!c)
			for (c = clients; c && (!ISVISIBLE(c) || c->transient); c = c->next);
	} else {
		for (i = clients; i && i != sel[desktop]; i = i->next)
			if (ISVISIBLE(i) && !i->transient)
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i) && !i->transient)
					c = i;
	}
	if (c)
		focus(c);
}

void
setposition(const Arg *arg)
{
	if (!sel[desktop])
		return;
	sel[desktop]->position = arg->i;
	layout();
	restack();
}

void
setmfact(const Arg *arg)
{
	mfact += arg->f;
	layout();
}

void
killclient(const Arg *arg)
{
	if (!sel[desktop])
		return;
	if (!sendevent(sel[desktop], wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, sel[desktop]->win);
		XUngrabServer(dpy);
	}
}

void
spawn(const Arg *arg)
{
	if (fork())
		return;
	if (dpy)
		close(ConnectionNumber(dpy));
	setsid();
	execvp(((char **)arg->v)[0], (char **)arg->v);
}

/* main */
void
setup(void)
{
	int i, screen;
	Atom utf8string;
	const unsigned int desktops = DESKTOPS;

	XSetErrorHandler(xerrordummy);

	/* clean up any zombies immediately */
	sigchld(0);

	for (i = 0; i < DESKTOPS; i++)
		sel[i] = NULL;

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	bh = 0;

	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMDesktop] = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetWMWindowTypeDock] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);

	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "xiwm", 4);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);

	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *) &desktops, 1);

	/* select events */
	XSelectInput(dpy, root, ROOTMASK);
	grabkeys();
	setdesktop(INIDESKTOP);
	focus(NULL);
}

void
runautostart(void) {
	system("~/.config/xiwm/autostart.sh");
}

void
run(void)
{
	XEvent ev;
	while (!XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev);
}

int
main(int argc, char *argv[])
{
	if (argc != 1)
		die("usage: xiwm");
	if (!(dpy = XOpenDisplay(NULL)))
		die("xiwm: cannot open display");
	setup();
	runautostart();
	run();
}

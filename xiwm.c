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
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define ISVISIBLE(C)            (!(C)->isdock && ((C)->desktop == desktop))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define MAX(A, B)               ((A) > (B) ? (A) : (B))

/* enums */
enum { NetSupported, NetWMName, NetWMDesktop, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetWMWindowTypeDock,
       NetClientList, NetCurrentDesktop, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Client Client;
struct Client {
	int x, y, w, h;
	int fx, fy, fw, fh;
	unsigned int desktop;
	int position;
	int isfixed, isfullscreen, isdock;
	Client *next;
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
	int position;
} Rule;

/* actions */
static void tag(const Arg *arg);
static void tagrel(const Arg *arg);
static void view(const Arg *arg);
static void viewrel(const Arg *arg);
static void focusstack(const Arg *arg);
static void setposition(const Arg *arg);
static void setmfact(const Arg *arg);
static void movemouse(const Arg *arg);
static void resizemouse(const Arg *arg);
static void killclient(const Arg *arg);
static void quit(const Arg *arg);
static void spawn(const Arg *arg);

/* X event handlers */
static void keypress(XEvent *e);
static void buttonpress(XEvent *e);
static void clientmessage(XEvent *e);
static void configurenotify(XEvent *e);
static void destroynotify(XEvent *e);
static void unmapnotify(XEvent *e);
static void configurerequest(XEvent *e);
static void maprequest(XEvent *e);

/* signals */
static void sigchld(int unused);

/* variables */
static const char broken[] = "broken";
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar geometry */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static void (*handler[LASTEvent]) (XEvent *) = {
	[KeyPress] = keypress,
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[UnmapNotify] = unmapnotify,
	[MapRequest] = maprequest,
	[ConfigureRequest] = configurerequest,
};
static Atom wmatom[WMLast], netatom[NetLast];
static int running = True;
static unsigned int desktop;
static float mfact = 0.5;
static Display *dpy;
static Client *clients, *sel;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

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

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "xiwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

void
configure(Client *c)
{
	XConfigureEvent ce;

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
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
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
grabbuttons(Client *c, int focused)
{
	unsigned int i, j;
	unsigned int modifiers[] = { 0, LockMask };

	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	if (!focused)
		XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
			BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
	for (i = 0; i < LENGTH(buttons); i++)
		for (j = 0; j < LENGTH(modifiers); j++)
			XGrabButton(dpy, buttons[i].button,
				buttons[i].mask | modifiers[j],
				c->win, False, BUTTONMASK,
				GrabModeAsync, GrabModeSync, None, None);
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

int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
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
xsetdesktop(void)
{
	XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *) &desktop, 1);
}

void
resize(Client *c, int x, int y, int w, int h, int bw)
{
	XWindowChanges wc;

	c->x = wc.x = x;
	c->y = wc.y = y;
	c->w = wc.width = w;
	c->h = wc.height = h;
	if (c->position < 0 && !c->isfullscreen) {
		c->fx = c->x;
		c->fy = c->y;
		c->fw = c->w;
		c->fh = c->h;
	}
	wc.border_width = bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void
layoutcolumn(int pos, int x, int w)
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

	for (c = clients; c; c = c->next) {
		if (c->isdock) {}
		else if (!ISVISIBLE(c))
			XMoveWindow(dpy, c->win, sw * -2, c->y);
		else if (c->isfullscreen)
			resize(c, 0, 0, sw, sh, 0);
		else if (c->position < 0)
			resize(c, c->fx, c->fy, c->fw, c->fh, 1);
		else if (c->position == 0)
			resize(c, 0, bh, sw, sh - bh, 0);
	}

	layoutcolumn(1, 0, sw * mfact);
	layoutcolumn(2, sw * mfact, sw - sw * mfact);
}

void
restack(void)
{
	Client *c;
	XEvent ev;

	if (!sel)
		return;
	if (sel->position > 0)
		for (c = clients; c; c = c->next)
			if (ISVISIBLE(c) && c->position > 0)
				XRaiseWindow(dpy, c->win);
	XRaiseWindow(dpy, sel->win);
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
arrange(void)
{
	layout();
	restack();
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*) &netatom[NetWMFullscreen], 1);
		c->isfullscreen = True;
		arrange();
	} else if (!fullscreen && c->isfullscreen){
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*) 0, 0);
		c->isfullscreen = False;
		arrange();
	}
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
		c->position = -1;
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
	xsetclientdesktop(c);
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
	Client **tc;

	for (tc = &clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
unfocus(Client *c)
{
	if (!c)
		return;
	grabbuttons(c, False);
	XSetWindowBorder(dpy, c->win, col_norm);
}

void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c))
		for (c = clients; c && !ISVISIBLE(c); c = c->next);
	if (sel && sel != c)
		unfocus(sel);
	if (c) {
		XSetWindowBorder(dpy, c->win, col_high);
		grabbuttons(c, True);
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
			PropModeReplace, (unsigned char *) &(c->win), 1);
		sendevent(c, wmatom[WMTakeFocus]);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	sel = c;
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL;
	Window trans = None;

	c = calloc(1, sizeof(Client));
	c->win = w;
	/* geometry */
	c->fx = c->x = wa->x == 0 ? (sw - wa->width) / 2 : wa->x;
	c->fy = c->y = wa->y == 0 ? (sh - wa->height) / 2 : wa->y;
	c->fw = c->w = wa->width;
	c->fh = c->h = wa->height;

	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->desktop = t->desktop;
		xsetclientdesktop(c);
	} else {
		c->desktop = desktop;
		applyrules(c);
	}
	updatewindowtype(c);

	XSetWindowBorder(dpy, c->win, col_norm);
	updatefixed(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, False);
	if (trans != None || c->isfixed)
		c->position = -1;
	if (c->position < 0)
		XRaiseWindow(dpy, c->win);
	attach(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
		PropModeAppend, (unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	xsetclientstate(c, NormalState);
	unfocus(sel);
	sel = c;
	arrange();
	XMapWindow(dpy, c->win);
	focus(NULL);

	if (c->isdock) {
		bh = c->h;
		arrange();
	}
}

void
unmanage(Client *c, int destroyed)
{
	Client *i;
	XWindowChanges wc;

	detach(c);
	if (!destroyed) {
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		xsetclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (i = clients; i; i = i->next)
		XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32,
			PropModeAppend, (unsigned char *) &(i->win), 1);
	arrange();
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
buttonpress(XEvent *e)
{
	unsigned int i;
	Client *c;
	XButtonPressedEvent *ev = &e->xbutton;

	if ((c = wintoclient(ev->window))) {
		focus(c);
		restack();
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].func && buttons[i].button == ev->button
			&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
				buttons[i].func(&buttons[i].arg);
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
		if (c != sel) {
			if (c->desktop != desktop) {
				desktop = c->desktop;
				xsetdesktop();
			}
			focus(c);
			arrange();
		}
	}
}

void
configurenotify(XEvent *e)
{
	XConfigureEvent *ev = &e->xconfigure;

	if (ev->window == root) {
		sw = ev->width;
		sh = ev->height;
		focus(NULL);
		arrange();
	}
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, True);
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
			unmanage(c, False);
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (c->position < 0) {
			if (ev->value_mask & CWX) {
				c->x = ev->x;
			}
			if (ev->value_mask & CWY) {
				c->y = ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->h = ev->height;
			}
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c) || c->isdock)
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
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
	if (!sel)
		return;
	if (arg->ui >= desktops)
		return;
	if (sel->desktop == arg->ui)
		return;
	sel->desktop = arg->ui;
	xsetclientdesktop(sel);
	if (desktop != arg->ui) {
		desktop = arg->ui;
		xsetdesktop();
	}
	focus(NULL);
	arrange();
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
	if (arg->ui == desktop)
		return;
	if (arg->ui >= desktops)
		return;
	desktop = arg->ui;
	focus(NULL);
	arrange();
	xsetdesktop();
}

void
viewrel(const Arg *arg)
{
	Arg a = {.ui = desktop + arg->i};
	view(&a);
}

void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!sel)
		return;
	if (arg->i > 0) {
		for (c = sel->next; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			for (c = clients; c && !ISVISIBLE(c); c = c->next);
	} else {
		for (i = clients; i != sel; i = i->next)
			if (ISVISIBLE(i))
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i))
					c = i;
	}
	if (c) {
		focus(c);
		restack();
	}
}

void
setposition(const Arg *arg)
{
	if (!sel)
		return;
	sel->position = arg->i;
	arrange();
}

void
setmfact(const Arg *arg)
{
	mfact += arg->f;
	arrange();
}

void
killclient(const Arg *arg)
{
	if (!sel)
		return;
	if (!sendevent(sel, wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
movemouse(const Arg *arg)
{
	int x, y, ocx, ocy, nx, ny, di;
	unsigned int dui;
	Client *c;
	XEvent ev;
	Time lasttime = 0;
	Window dummy;

	if (!(c = sel))
		return;
	if (c->isfullscreen || c->position >= 0)
		return;
	restack();
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
			if (c->position < 0)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
}

void
resizemouse(const Arg *arg)
{
	int ocx, ocy, nw, nh;
	Client *c;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = sel))
		return;
	if (c->isfullscreen || c->position >= 0)
		return;
	restack();
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, None, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w, c->h);
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
			if (c->position < 0)
				resize(c, c->x, c->y, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w, c->h);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
quit(const Arg *arg)
{
	running = False;
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "xiwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

/* main */
void
setup(void)
{
	int screen;
	XSetWindowAttributes wa;
	Atom utf8string;

	XSync(dpy, False);
	XSetErrorHandler(xerror);

	/* clean up any zombies immediately */
	sigchld(0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	bh = 0;
	desktop = inidesktop;

	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
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

	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "xiwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);

	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	xsetdesktop();
	XDeleteProperty(dpy, root, netatom[NetClientList]);

	/* select events */
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void
runAutostart(void) {
	system("~/.config/xiwm/autostart.sh");
}

void
run(void)
{
	XEvent ev;
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev);
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};

	view(&a);
	while (clients)
		unmanage(clients, False);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	XDestroyWindow(dpy, wmcheckwin);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

int
main(int argc, char *argv[])
{
	if (argc != 1)
		die("usage: xiwm");
	if (!(dpy = XOpenDisplay(NULL)))
		die("xiwm: cannot open display");
	setup();
	scan();
	runAutostart();
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
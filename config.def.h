/* See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int desktops   = 3;
static const unsigned int inidesktop = 1;
static const unsigned int col_norm   = 0x444444;
static const unsigned int col_high   = 0x335588;

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 */
	/* class         instance    desktop   position */
	{ "Thunderbird", NULL,       0,        PMax },
};

/* commands */
static const char *termcmd[]   = { "x-terminal-emulator", NULL };
static const char *runcmd[]    = { "dmenu_run", NULL };

static Key keys[] = {
	/* modifier              key        function      argument */
	{ Mod1Mask|ControlMask,  XK_t,      spawn,        {.v = termcmd } },
	{ Mod4Mask,              XK_r,      spawn,        {.v = runcmd } },
	{ Mod1Mask,              XK_Tab,    focusstack,   {.i = +1 } },
	{ Mod1Mask|ShiftMask,    XK_Tab,    focusstack,   {.i = -1 } },
	{ Mod1Mask,              XK_l,      setmfact,     {.f = +0.02 } },
	{ Mod1Mask,              XK_h,      setmfact,     {.f = -0.02 } },
	{ Mod1Mask,              XK_F4,     killclient,   {0} },
	{ Mod1Mask|ShiftMask,    XK_q,      quit,         {0} },
	{ Mod4Mask,              XK_F1,     view,         {.ui = 0 } },
	{ Mod4Mask,              XK_F2,     view,         {.ui = 1 } },
	{ Mod4Mask,              XK_F3,     view,         {.ui = 2 } },
	{ Mod1Mask|ControlMask,  XK_Right,  viewrel,      {.i = +1 } },
	{ Mod1Mask|ControlMask,  XK_Left,   viewrel,      {.i = -1 } },
	{ Mod1Mask|ShiftMask,    XK_Right,  tagrel,       {.i = +1 } },
	{ Mod1Mask|ShiftMask,    XK_Left,   tagrel,       {.i = -1 } },
	{ Mod1Mask,              XK_Down,   setposition,  {.i = PFloat } },
	{ Mod1Mask,              XK_Up,     setposition,  {.i = PMax } },
	{ Mod1Mask,              XK_Left,   setposition,  {.i = PLeft } },
	{ Mod1Mask,              XK_Right,  setposition,  {.i = PRight } },
};

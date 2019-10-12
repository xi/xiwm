# xiwm

This is a simple window manager I hacked together in my free time. It is based
on [dwm](https://dwm.suckless.org)'s code, but the functionality is more
influenced by [openbox](https://openbox.org).

## Features

-	like dwm
	-	extremely small (only ~1000 lines of C, roughly half of dwm)
	-	supports tiling (see below for details)
	-	configured by editing the source code
-	like openbox
	-	reads `~/.config/xiwm/environment` and `~/.config/xiwm/autostart.sh`
	-	supports multiple desktops (instead of dwm's tags)
	-	works with external panels/bars (I use lxpanel)
-	no multi monitor support

## Default key bindings

-	`A-C-t`     launch terminal
-	`W-r`       launch dmenu
-	`A-Tab`     focus next window
-	`A-S-Tab`   focus previous window
-	`A-l`       increase left column width
-	`A-h`       decrease left column width
-	`A-F4`      close window
-	`A-S-q`     quit
-	`W-F1`      go to desktop 1
-	`W-F2`      go to desktop 2
-	`W-F3`      go to desktop 3
-	`A-C-Right` go to next desktop
-	`A-C-Left`  go to previous desktop
-	`A-S-Right` move window to next desktop
-	`A-S-Left`  move window to previous desktop
-	`A-Down`    set window to floating mode
-	`A-Up`      maximize window
-	`A-Left`    move window to left column
-	`A-Right`   move window to right column

## Layout concept

With floating window managers it is simple to control where an individual
window is, it is hard to control general properties such as avoiding overlap.
With tiling window managers it is simple to control exactly those general
properties. The flipside is that it gets much harder to position an individual
window.

I usually have all my windows maximized. Just sometimes I want to position two
windows side by side. This works reasonably well with floating window managers,
but I wanted to see if I could improve on that.

With xiwm, all windows start out maximized. However, you can position them on
the left or right. When you focus one of the positioned windows, all of them
are raised.

I am not sure yet if the positioning should influence the tab order. Still
experimenting.

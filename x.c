#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

#include "utils.h"
#include "x.h"

struct X11 {
	Display *dpy;
	XScreenSaverInfo *info;
};

X11 *
x11_init(void)
{
	X11 *x;

	x = ecalloc(1, sizeof(*x));

	x->dpy = XOpenDisplay(NULL);
	if (!x->dpy)
		die("[X11] cannot open X display");

	x->info = XScreenSaverAllocInfo();
	if (!x->info)
		die("[X11] XScreenSaverAllocInfo failed");

	return x;
}

void
x11_cleanup(X11 *x)
{
	if (!x)
		return;

	if (x->info)
		XFree(x->info);

	if (x->dpy)
		XCloseDisplay(x->dpy);

	free(x);
}

unsigned long
x11_idle_ms(X11 *x)
{
	if (!x || !x->dpy || !x->info)
		die("[X11] Connection lost");

	if (!XScreenSaverQueryInfo(x->dpy, DefaultRootWindow(x->dpy), x->info))
		die("[X11] XScreenSaverQueryInfo failed");

	return x->info->idle;
}

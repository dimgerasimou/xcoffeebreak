#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

#include "utils.h"
#include "x.h"

static Display          *dpy;
static XScreenSaverInfo *info;

unsigned long
x11_idle_ms(void)
{
	if (!info)
		return 0;

	XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), info);
	return info->idle;
}

void
x11_init(void)
{
	dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("[X11] cannot open X display");

	info = XScreenSaverAllocInfo();
	if (!info)
		die("[X11] XScreenSaverAllocInfo failed");

}

void
x11_cleanup(void)
{
        XFree(info);
	XCloseDisplay(dpy);
}

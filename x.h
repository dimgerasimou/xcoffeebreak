#ifndef X_H
#define X_H

typedef struct X11 X11;

X11* x11_init(void);
void x11_cleanup(X11 *x);
unsigned long x11_idle_ms(X11 *x);

#endif /* X_H */
# xcoffeebreak

A lightweight, **X11** idle management daemon. It executes user-configurable actions after a period of inactivity.
Usefull in case you tend to forget to do so lock your computer manually before having a coffee break.

A small, modern [xautolock](https://linux.die.net/man/1/xautolock) replacement.

## Build / install

Edit `Makefile` to match your intended configuration. Then run:

```bash
make
sudo make install
```

### Dependencies

- C compiler (gcc/clang)
- make
- pkg-config
- X11 development libraries (`libX11`, `libXss`)
- DBus development libraries (`libdbus-1`)

## Usage

Behavior is configured via flags (see `xcoffeebreak --help` or the man page).

Quick example:

```bash
xcoffeebreak --lock_s 600 --lock_cmd "slock" --off_s 1200 --suspend_s 1800
```

### Running at Startup

**With .xinitrc or .xsession**:

Add to your `~/.xinitrc`:

```bash
xcoffeebreak &
```

## Features

- **Progressive state management**: Automatically locks ➝ screen off ➝ suspend based on idle time
- **MPRIS media player integration**: Prevents locking while music/video is playing
- **Suspend detection**: Automatically resets idle timers after system resume
- **Configurable timeouts and commands**: Customize lock, screen-off, and suspend behaviors

## License

This project is licensed under the GNU General Public License v3.
See the LICENSE file for details.

© 2026 Dimitris Gerasimou

## See Also

- [xautolock](https://linux.die.net/man/1/xautolock) - Alternative X11 idle locker
- [xss-lock](https://bitbucket.org/raymonad/xss-lock) - Use X screensaver with systemd
- [xidlehook](https://gitlab.com/jD91mZM2/xidlehook) - Rust-based alternative
- [MPRIS D-Bus Interface Specification](https://specifications.freedesktop.org/mpris-spec/latest/)

# AfterStep

AfterStep is a highly configurable X11 window manager inspired by NeXTSTEP.

This repository contains the AfterStep 2.x codebase plus updates to build and run on modern
Linux/Xorg systems.

## Screenshots (2026-01-18, Xvfb)

<p>
  <img src="screenshots/2026-01-18-xvfb/01-desktop.png" width="48%" alt="AfterStep desktop (full init)" />
  <img src="screenshots/2026-01-18-xvfb/02-root-menu.png" width="48%" alt="AfterStep root menu open" />
</p>
<p>
  <img src="screenshots/2026-01-18-xvfb/03-clients.png" width="48%" alt="AfterStep with client windows" />
  <img src="screenshots/2026-01-18-xvfb/04-clients-menu.png" width="48%" alt="AfterStep with client windows and menu open" />
</p>

Full in-repo gallery: [screenshots/2026-01-18-xvfb/index.html](screenshots/2026-01-18-xvfb/index.html)

## Build + install (local prefix)

```sh
make distclean
./configure --prefix="$PWD/_install"
make -j"$(nproc)"
make install install.data
```

## Headless smoke tests (optional)

```sh
tools/xvfb-smoke.sh
tools/xvfb-full-smoke.sh
tools/xvfb-soak.sh _install 60
```

Optional:

- Xephyr-based RandR smoke test: `tools/xephyr-randr-smoke.sh`
- Headless Xorg dummy driver smoke test: `tools/xorg-dummy-smoke.sh` (requires `sudo`)

## Running AfterStep

- `startx` / `xinit`: put `exec /path/to/_install/bin/afterstep` in `~/.xinitrc`
- Display managers: install `AfterStep.desktop` / `AfterStep.session` into your system’s `xsessions`
  directory (location varies by distro/prefix)

## Wayland status (experimental)

AfterStep itself is still an **X11 window manager**. Native Wayland support is being approached in two tracks:

- **Wayland clients (“B track”)** in `wayland/` (e.g. a panel/launcher that runs as a normal Wayland client).
- **Compositor work (“C track”)** via a wlroots-based compositor scaffold.

Quick start:

```sh
make -C wayland
./wayland/aswlpanel
```

Optional compositor scaffold (requires `wlroots` development files on your system):

```sh
make -C wayland aswlcomp
```

Nested Wayland dev run (from an existing Wayland session):

```sh
WLR_BACKENDS=wayland ./wayland/aswlcomp --socket aswlcomp-0 --spawn "./wayland/aswlpanel"
```

Convenience target (auto-selects `WLR_BACKENDS=wayland` or `x11` and uses a temporary autostart file):

```sh
make -C wayland run-nested
```

Tips:

- `Alt+Escape` exits `aswlcomp`
- `Alt+LMB` moves windows, `Alt+RMB` resizes windows
- `--spawn` may be repeated to launch multiple clients
- Optional autostart file: `~/.config/afterstep/aswlcomp.autostart` (one command per line, optional `exec ` prefix; also supports `bind MODS+KEY exec CMD`)
- If Xwayland is available, `aswlcomp` sets `DISPLAY` (printed at startup) so it can run legacy X11 apps under Wayland.

## Notes

- Wharf/MonitorWharf buttons may be disabled if the configured applications aren’t installed.
- Set `TERMINAL` to choose the terminal emulator used by terminal-launch actions.

## More

- Historical upstream README (kept for context): [README](README)

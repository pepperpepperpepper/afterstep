# AfterStep (modern X11 compatibility)

This source tree has been updated and validated to build and run on modern Xorg/X11 toolchains
(tested on Arch Linux using Xvfb/Xephyr/headless Xorg).

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

## Quick start

```sh
make distclean
./configure --prefix="$PWD/_install"
make -j"$(nproc)"
make install install.data
tools/xvfb-smoke.sh
```

More detail:

- Modern status + rationale: [plan.md](plan.md)
- Historical upstream README: [README](README)


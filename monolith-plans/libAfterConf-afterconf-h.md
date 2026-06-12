# Plan: split `libAfterConf/afterconf.h`

Snapshot (2026-02-13): ~2679 LOC.

`libAfterConf/afterconf.h` is a huge umbrella header that exposes config syntax definitions, config structures, and a large
number of unrelated externs across many modules. It increases compile-time coupling and makes it harder to find the
“right” config surface for a given subsystem.

## Why it’s a monolith

- One header mixes:
  - Function parsing definitions
  - Top-level syntax declarations for many modules
  - Shared config structs
  - Inclusion of multiple `libAfterStep/*` headers (pulls in a lot transitively)
- Most translation units include it “just in case”, which hides actual dependencies.

## Split targets (phase 1: mechanical extraction, API-compatible)

Goal: split into smaller headers **without breaking existing includes**.

Introduce a set of focused headers, for example:

- `libAfterConf/afterconf_syntax.h`: shared `SyntaxDef` declarations + function parsing declarations.
- `libAfterConf/afterconf_afterstep.h`: AfterStep core config structs + syntax (`AfterStepSyntax`, feel/look/theme).
- `libAfterConf/afterconf_pager.h`: Pager config structs + syntax.
- `libAfterConf/afterconf_wharf.h`: Wharf config structs + syntax.
- `libAfterConf/afterconf_winlist.h`: WinList config structs + syntax.
- (etc., one per module family)

Then change `libAfterConf/afterconf.h` to become an umbrella:

- It should `#include` the new focused headers to preserve “include one file” legacy behavior.

Optionally add:

- a “minimal include” macro (e.g., `AFTERCONF_MINIMAL`) so new code can opt into a smaller dependency surface.

## Refactor steps (suggested order)

- [ ] Create `afterconf_syntax.h` and move the shared forward declarations + core externs first.
- [ ] Split module-family headers one at a time, keeping `afterconf.h` including them all.
- [ ] Adjust translation units to include the specific header they need (opportunistic; no need for a flag day).
- [ ] Validate that there are no new include cycles.

## Validation

- `make -C libAfterConf`
- Full build (recommended when this is changed): `make -j\"$(nproc)\"`


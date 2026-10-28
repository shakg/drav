# drav

Offline vector design tool. Desktop app, Qt 6.8 / QML UI, Skia GPU canvas. No network, no telemetry, no accounts.

![drav](docs/screenshot.png)

## Status

Feature parity with Figma for offline, single-person UI/UX work, except prototyping. Linux is
tested; macOS and Windows build and package through the same CMake setup but are untested.

| Area | What works |
| --- | --- |
| Canvas | Infinite canvas, pan (Space/middle drag/scroll), zoom (Ctrl+scroll, pinch, Shift+0/1/2), pages, outline mode, layout grids, pixel snapping, rulers and guides, Alt-hover measurements |
| Layers | Frames, sections, groups, rectangles, ellipses (arcs/donuts), polygons, stars, lines/arrows, vectors, text, boolean groups, masks; layer search |
| Tools | Move/resize/rotate (Shift/Alt modifiers, flip by dragging past), marquee, drill-in selection, drag into/out of frames, smart snapping guides, frame, section, shapes, pen, pencil, text, hand, scale tool (K), eyedropper (I), image crop |
| Vectors | Vector networks (branching paths, open and closed regions, paint bucket to fill/unfill faces), point editing, union/subtract/intersect/exclude, flatten, outline stroke, corner radius, corner smoothing |
| Auto layout | Horizontal/vertical/wrap/grid (column and row spans), gap, space-between, padding, 3×3 alignment, hug/fill/fixed sizing, min/max, absolute children, baseline, stroke inclusion, reverse z-order, drag-reorder |
| Constraints | Left/right/left-right/center/scale (and vertical equivalents) on frame resize |
| Components | Create, instances, per-property overrides, nested instances, reset/detach/go to main, swap, variants, component properties (boolean, text, instance swap) |
| Styles and variables | Color, text, effect and grid styles; variable collections with modes (color, number, string, boolean), aliases, per-frame mode switching, binding to fills, strokes, sizes, spacing, radii, opacity, text and visibility |
| Libraries | Link other `.drav` files: use their styles, variables and components; update when the source changes; unlink |
| Paint | Solid, linear/radial/angular/diamond gradients, images (fill/fit/crop/tile) with exposure, contrast, saturation, temperature, tint, highlights and shadows; blend modes; multiple fills and strokes |
| Strokes | Inside/center/outside, per-stroke weights, joins, dashes, caps and arrowheads, variable width profiles |
| Effects | Drop shadow (with spread), inner shadow, layer blur, background blur |
| Text | Mixed styles in one layer (family, weight, size, color, decoration, case, links), bullet and numbered lists, OpenType features, variable font axes, text on a path, missing-font replacement; bundled Inter + system fonts |
| Color | sRGB or Display P3 documents (assign or convert), color-managed canvas and P3-tagged exports |
| Files | Native `.drav` (CBOR), Figma `.fig` import, SVG import, image import/paste/drag-drop, export PNG/JPG/WEBP/SVG/PDF at any scale, export presets, copy as PNG/SVG/CSS, inspect panel (CSS) |
| Safety | Autosave and crash recovery, named version history, recent files |
| Editing | Undo/redo for everything, copy/paste (in place), duplicate, alt-drag copy, align/distribute, z-order, lock/hide, rename and bulk rename, find and replace text, select matching layers |
| Scripting | JavaScript console and plugins against a sandboxed `drav` API ([docs/scripting.md](docs/scripting.md)) |
| Packaging | Self-contained install tree and CPack packages: `.tar.gz`, `.dmg`, `.zip` ([docs/packaging.md](docs/packaging.md)) |

Not done: prototyping, comments and multiplayer (out of scope offline). `.fig` import is written against
Figma's published field names and tested on a generated fixture, not on real Figma exports — expect gaps.

## Performance

The canvas is designed to stay at display refresh rate (120 Hz on a 120 Hz panel):

- Rendering runs on Qt Quick's render thread with Skia's Ganesh GL backend. Skia uses its own GL context that shares textures with Qt's, so the two never fight over GL state.
- The document is recorded into `SkPicture`s per container subtree, keyed by a per-node version counter. Panning/zooming replays cached pictures; an edit re-records only the changed subtree.
- Paragraph layout is cached per text node; decoded images are cached and shared across threads.
- The GUI thread only builds a frame snapshot (cached content picture + a small overlay picture). On the showcase file its per-frame cost rounds to 0 ms in Qt's scene graph timing (`QSG_RENDER_TIMING=1`).

`drav --bench 5` pans/zooms the showcase file and prints the frame rate.

## Build (Linux)

Requirements: Qt 6.8, CMake ≥ 3.24, Ninja, a C++20 compiler, and a static Skia build.

```bash
micromamba create -f environment.yml
```

```bash
micromamba activate drav
```

```bash
scripts/build-skia.sh third_party/skia
```

```bash
cmake -S . -B build -G Ninja -DSKIA_DIR=third_party/skia -DCMAKE_PREFIX_PATH="$CONDA_PREFIX"
```

```bash
cmake --build build && ctest --test-dir build
```

```bash
./build/src/app/drav --demo
```

Command-line options: `drav [file]`, `--demo`, `--select <layer name>`, `--script <js>`, `--show <objectName>`, `--screenshot <png>`, `--bench <seconds>`.

Package: `scripts/package.sh` (see [docs/packaging.md](docs/packaging.md)).

## Layout

```
src/core     document model, undo, auto layout, components, styles/variables, libraries, vector networks,
             geometry, text, color profiles, file format, .fig import (no GUI)
src/render   Skia renderer with picture caching, exporters, CSS generation
src/app      editor state, tools, layer model, SVG import, scripting, GPU canvas item, main
qml          UI: layers/pages/assets, design inspector, toolbar, menus
tests        core, render and editor (tool-driven) tests
```

### Core ideas

- **Document** owns all nodes. Every change runs inside a transaction: `edit(id)` snapshots a node's properties on first touch, structural moves are logged, and `commit()` produces one undo step. Derived state (auto layout, instance sync, group bounds, text auto-size) is recomputed inside the same transaction so undo restores it exactly.
- **Instances** are real node trees. Each instance node keeps a `sourceId` to its counterpart in the main component and a bitmask of overridden property groups. Syncing copies only non-overridden groups, so main edits propagate while overrides stick.
- **Auto layout** runs bottom-up over changed subtrees (hug sizes) and top-down for fill children.

## Offline guarantee

The app never opens network connections. QML URL loading is restricted to bundled (`qrc:`) and local files; anything else is refused. There is no update check, crash reporter, analytics or font download — the Inter font ships in the binary.

## Shortcuts

| | |
| --- | --- |
| V / F / R / O / L / Shift+L / P / Shift+P / T / H | Move, frame, rectangle, ellipse, line, arrow, pen, pencil, text, hand |
| Ctrl+G / Ctrl+Shift+G / Ctrl+Alt+G | Group, ungroup, frame selection |
| Shift+A | Add auto layout |
| Ctrl+Alt+K / Ctrl+Alt+B | Create component, detach instance |
| Ctrl+E / Ctrl+Shift+O / Ctrl+Alt+M | Flatten, outline stroke, use as mask |
| Ctrl+D, Ctrl+C/X/V, Ctrl+Shift+V | Duplicate, clipboard, paste in place |
| Alt+A/D/W/S/H/V, Alt+Shift+H/V | Align, distribute |
| Ctrl+[ / ] (with Shift: to back/front) | Reorder |
| Shift+H / Shift+V | Flip |
| Enter / Shift+Enter / Tab | Select children / parent / next sibling; edit text or vector |
| 1–0 | Opacity 10–100% |
| Shift+0 / 1 / 2, Ctrl+Y | Zoom 100% / fit / selection, outlines |
| Shift+S / K / I | Section, scale tool, eyedropper |
| Shift+R, Alt (hover) | Rulers, measure distances |
| Ctrl+F / Ctrl+R / Ctrl+Alt+A | Find and replace, rename (bulk with several selected), select matching |
| Ctrl+B / I / U (editing text) | Bold, italic, underline on the selected characters |
| Ctrl+Enter (script console) | Run script |

## License

Code: to be decided by the repository owner. Bundled font: Inter, SIL Open Font License 1.1 (`resources/fonts/Inter-LICENSE.txt`).

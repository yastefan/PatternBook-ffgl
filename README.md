# PatternBook

**Minimal generative line patterns for Resolume.**

PatternBook draws clean line work (Truchet tiles, spirals, tunnels, guilloché
rosettes, string art, Lissajous and Spirograph curves, isometric wireframes,
flow lines, contour maps, a wireframe landscape, op-art swellings, ripple
meshes, moiré and Chladni figures) in two colours. Every pattern has three
**Form** controls for its shape and two **Phase** controls that loop
seamlessly, made for Resolume's own parameter animation (timeline, BPM sync).
**Drift** runs Phase 1 on its own in time with the BPM, and a **Hit**, by hand
or on the beat, sends a wave out from the centre. A shared **Lines** section
dashes, thins, mirrors and warps every pattern alike.

PatternBook is a native FFGL source plugin for **Resolume Arena and Avenue** on
**macOS and Windows**.

![All 25 patterns](docs/images/patterns.png)

- 25 line patterns in six families: Tiles, Radial, Curves, Iso, Field, Waves
- Three Form controls and two looping Phase controls per pattern
- Lines section: dashes that can run along the lines, Thin Out, Mirror up to
  12-fold kaleidoscopes, Warp
- Line width in pixels, so lines stay equally sharp at any output resolution
- Ink and paper colours, each with alpha, so the paper can be transparent
- Most patterns take 2–4 ms per frame in 1080p on an Intel Iris Plus

## Download and install

1. Download the zip for your system from the
   [Releases page](https://github.com/yastefan/PatternBook-ffgl/releases).
2. Quit Resolume.
3. Copy the plugin from the zip into Resolume's **Extra Effects** folder:

   | System  | File from the zip    | Folder |
   |---------|----------------------|--------|
   | Windows | `PatternBook.dll`    | `Documents\Resolume Arena\Extra Effects` |
   | macOS   | `PatternBook.bundle` | `Documents/Resolume Arena/Extra Effects` |

   For Avenue, use `Resolume Avenue` instead of `Resolume Arena` in the path.
4. **macOS only:** macOS blocks plugins downloaded from the internet. Open
   Terminal in that folder and run:

   ```bash
   xattr -dr com.apple.quarantine PatternBook.bundle
   ```

5. Start Resolume. PatternBook appears in the **Sources** tab.

Requirements: Resolume Arena or Avenue 7 and a graphics card with OpenGL 4.1.

## Animating it

Each pattern has two **Phase** controls, 0–100 %. Wherever they stand, 0 % and
100 % give exactly the same picture: a phase only turns something by its own
symmetry, shifts a repeating structure by one repeat, or drives a wave. So you
can let Resolume animate a Phase with a looping (saw) animation, BPM-synced or
on the timeline, and it never jumps. **Drift** adds a steady run of Phase 1 on
its own; set it to 0 to stop it and drive Phase 1 entirely from Resolume
(which can also run it backwards).

## Parameters

### Pattern

| Parameter | What it does |
|---|---|
| **Pattern** | Which of the 25 patterns to draw, grouped by family. |
| **Seed** | Picks a different random layout (0–99). |
| **Scale** | Cells across the frame height, from 2 to 48. In the Curves, the number of chords or copies. |
| **Form 1**, **Form 2**, **Form 3** | The pattern's shape; the display names what each does (see the table below). |
| **Line Width** | In pixels, from 0.5 to 24. |
| **Rotation** | Turns the whole pattern. In Field: Terrain it turns the camera instead, so the flight pans and heads off in a new direction. |

### Animation

| Parameter | What it does |
|---|---|
| **Phase 1**, **Phase 2** | The pattern's two looping movements (see the table below). |
| **Drift** | Runs Phase 1 on its own, locked to Resolume's BPM: 0 stops it, turned up it runs faster, up to one cycle per beat. The display shows the cycle length in bars. With Drift at 0 and Hit Every off the pattern stands still. |
| **Hit** | Sends a wave out from the centre. |
| **Hit Amount** | How strong a hit is: how far its wave pushes, how high the terrain jumps, what share of the tiles it turns. 0 switches hits off. |
| **Hit Every** | Fires Hit automatically every beat up to every 8 bars. |

### Lines

These work the same in every pattern.

| Parameter | What it does |
|---|---|
| **Dash** | Breaks the lines into dashes; turned up, the gaps grow until only dots are left. |
| **Dash Length** | Length of one dash and gap. |
| **Dash Shift** | Moves the dashes along their lines; loops like the Phase controls, so animated it makes them run. |
| **Thin Out** | Leaves out a random share of the lines. |
| **Mirror** | Mirror X, Y or both, or a 6-, 8- or 12-fold kaleidoscope. The Curves get mirrored copies instead. |
| **Warp** | Bends the whole frame with smooth noise; **Warp Size** sets how large the bends are. Not for the Curves. |

### Ink and Paper

Two colour pickers: **Ink** for the lines, **Paper** for the background. Set
Paper's alpha to 0 to lay the pattern over other layers.

## Patterns

| Pattern | Form 1 | Form 2 | Form 3 | Phase 1 | Phase 2 |
|---|---|---|---|---|---|
| Tiles: Arcs | ribbon | lines | density | tiles turn | ribbons breathe |
| Tiles: Diagonals | lines | density | direction | tiles turn | lines pulse |
| Tiles: Hex Arcs | ribbon | lines | density | tiles turn | ribbons breathe |
| Tiles: Squares | size | nested | roundness | squares turn | squares breathe |
| Radial: Spiral | arms | tightness | hole | arms flow out | spacing breathes |
| Radial: Broken Rings | gaps | segments | speed spread | rings turn | gaps breathe |
| Radial: Rays | inner radius | count | lengths | rays turn | lengths pulse |
| Radial: Polygons | sides | twist | star / round | tunnel flows | tunnel turns |
| Radial: Rosette | lobes | depth | bands | curves turn | lobes breathe |
| Curves: Chords | factor | inner radius | span | inner circle rolls | factor swings |
| Curves: Lissajous | x frequency | y frequency | spread | figure tumbles | figure spins |
| Curves: Spirograph | lobes | turns | pen | copies weave | figure spins |
| Iso: Wire Cubes | inner cube | outline | size | cubes flip | inner cubes breathe |
| Iso: Hatched Cubes | hatching | top hatching | size | hatching slides | cubes breathe |
| Iso: Lattice | weave | weave length | families | lines glide | weave travels |
| Field: Flow Lines | turbulence | feature size | detail | eddies change | middle swirls |
| Field: Contours | levels | feature size | detail | contours rise | landscape changes |
| Field: Terrain | height | valley width | view distance | camera flies | camera sways |
| Field: Circles | rings | density | size spread | rings flow out | circles loop |
| Waves: Waves | frequency | cross wave | vertical change | waves travel | lines drift |
| Waves: Resonance | band width | crossing | left calm | generators drift | band wanders |
| Waves: Bulge | strength | lens size | lenses | lenses orbit | swelling breathes |
| Waves: Mesh | wavelength | sources | source distance | ripples travel | sources orbit |
| Waves: Moire | ring ratio | separation | squareness | centres orbit | rings flow |
| Waves: Chladni | complexity | levels | warp | modes morph | plate turns |

## For developers

### Building

```bash
git clone --recursive git@github.com:yastefan/PatternBook-ffgl.git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows use `cmake -S . -B build -A x64` and
`cmake --build build --config Release` (Visual Studio 2022). A Debug build
defines `DEBUG`, which turns on the FFGL SDK's check that the plugin leaves the
OpenGL context in its default state.

GitHub Actions builds a universal macOS bundle and an x64 Windows DLL on every
pull request, publishes **Latest build from main** on every push to `main`,
and a versioned release for tags `v<version>` that match
`project(PatternBook VERSION …)`. `main` only takes pull requests whose macOS
and Windows builds pass.

The Pattern menu is a table in [src/PatternBook.cpp](src/PatternBook.cpp),
grouped by family, which also names what each Form and Phase does.

### Testing without Resolume (macOS)

`patternbook-harness` loads the built plugin like a host, renders offscreen on
an OpenGL 4.1 core context and writes PNG frames:

```bash
./build/patternbook-harness --plugin build/PatternBook.bundle --out renders \
    --bpm 120 --frames 240 --every 30 --set Pattern=8 --at 60 Hit=1 --at 61 Hit=0
```

`--list` prints the parameter names, `--throughput` measures frame time.

### How it works

The line patterns are drawn in a full-screen fragment shader
([src/Shaders.h](src/Shaders.h)), compiled once per family so each program
stays small. Every pattern returns the nearest line: its distance in pixels, a
position along it and a number that tells lines apart. One shared function
then draws it with the line width, the dashes and Thin Out, anti-aliased over
one pixel. Lines that are level lines of a field (waves, contours, meshes)
take their distance and direction from screen-space derivatives, so they keep
their width however the field is bent. Mirror and Warp transform the frame
before any pattern sees it.

The Curves are drawn as geometry instead: each chord or curve segment is a
thin quad generated in the vertex shader, because testing hundreds of long
lines per pixel would be far too slow. Field: Terrain ray-marches a tileable
height map that the plugin builds once at start-up; the landscape sinks into a
valley along the flight line, and the plugin reads the same map on the CPU to
keep the camera just above the valley floor.

The patterns are original implementations of well-known techniques: Truchet
tiles, Archimedean spirals, guilloché rosettes, modular-multiplication chords,
Lissajous figures, hypotrochoids, isometric tilings, gradient and value noise,
stream functions, op-art lens distortion, interference, moiré and Chladni
plate modes. They are not taken from any pattern collection.

## License

PatternBook is free software under the [GNU General Public License v3.0 or
later](LICENSE). The FFGL SDK is © Resolume and contributors under its own
license, included with each release.

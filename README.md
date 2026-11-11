# PatternBook

**Minimal generative patterns for Resolume, animated to the beat.**

PatternBook draws calm, graphic patterns (Truchet tiles, spirals, broken
rings, isometric cubes, warped meshes, moiré) in two colours and moves them in
time with Resolume's BPM. A **Hit**, by hand or on the beat, sends a wave out
from the centre that turns tiles, flips cubes or pushes the mesh.

PatternBook is a native FFGL source plugin for **Resolume Arena and Avenue** on
**macOS and Windows**.

![All 16 patterns](docs/images/patterns.png)

- 16 patterns in four families: Grid, Radial, Isometric and Distortion
- Line width in pixels, so lines stay equally sharp at any output resolution
- Animation in musical units from 1 beat to 16 bars, locked to Resolume's BPM
- **Hit** button and automatic hits on the beat
- Ink and paper colours, each with alpha, so the paper can be transparent
- One shader, about 2–4 ms per frame in 1080p on an Intel Iris Plus

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

## Patterns

| Family | Patterns | Animation | Hit |
|---|---|---|---|
| Grid | Arcs, Diagonals, Squares, Dots | tiles make quarter turns, squares turn in a wave, dots breathe | tiles turn as the wave passes |
| Radial | Spiral, Broken Rings, Rays, Dot Rings | spiral and rings spin, rays grow and shrink, dot rings slide past each other | twist, gaps open, rays and dots swell |
| Isometric | Cubes, Wire Cubes, Hatched Cubes, Lattice | cubes shrink and grow in a wave, wire cubes flip inside out, lattice weaves | cubes pop, wire cubes flip |
| Distortion | Interference, Ripple, Wave Lines, Moire Rings | mesh pushed by waves, ripples, fabric, orbiting ring centres | a ring of displacement runs outwards |

## Parameters

### Pattern

| Parameter | What it does |
|---|---|
| **Pattern** | Which of the 16 patterns to draw. |
| **Seed** | Picks a different random layout (0–99). |
| **Scale** | Cells across the frame height, from 2 to 48. |
| **Form** | Pattern-specific: ribbon width, number of lines or arms, gap size, dot size, hatching, wavelength. The display says which. |
| **Line Width** | In pixels, from 0.5 to 24. |
| **Rotation** | Turns the whole pattern. |

### Animation

| Parameter | What it does |
|---|---|
| **Rate** | Length of one animation cycle, from 1 beat to 16 bars, or Off to freeze. Changing it changes the speed without a jump. |
| **Amount** | How strong the animation is: how many tiles turn, how far things move, how fast things spin. 0 holds the pattern still. |
| **Hit** | Sends a wave out from the centre. |
| **Hit Every** | Fires Hit automatically every beat up to every 8 bars. |

### Ink and Paper

Two colour pickers: **Ink** for the lines and shapes, **Paper** for the
background. Set Paper's alpha to 0 to lay the pattern over other layers.

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
`project(PatternBook VERSION …)`.

Parameter IDs and the order of the Pattern menu follow the menu order. Once a
version is released, only append, or saved Resolume compositions will mix up
their values.

### Testing without Resolume (macOS)

`patternbook-harness` loads the built plugin like a host, renders offscreen on
an OpenGL 4.1 core context and writes PNG frames:

```bash
./build/patternbook-harness --plugin build/PatternBook.bundle --out renders \
    --bpm 120 --frames 240 --every 30 --set Pattern=8 --at 60 Hit=1 --at 61 Hit=0
```

`--list` prints the parameter names, `--throughput` measures frame time.

### How it works

Everything is drawn in one full-screen fragment shader
([src/Shaders.h](src/Shaders.h)). Each pattern computes, per pixel, how much
ink covers it, from distances to lines and shapes anti-aliased over one pixel.
Warped patterns use screen-space derivatives so lines keep their width however
the mesh is bent. The clock, rate and hits run on the CPU
([src/PatternBook.cpp](src/PatternBook.cpp)).

The patterns are original implementations of well-known generative techniques
(Truchet tiles, Archimedean spirals, isometric tilings, interference and moiré)
and are not taken from any pattern collection.

## License

PatternBook is free software under the [GNU General Public License v3.0 or
later](LICENSE). The FFGL SDK is © Resolume and contributors under its own
license, included with each release.

# Toy Story 2 Fix
Toy Story 2 Fix is a program that fixes and enhances Toy Story 2 for the PC. Features include:
* Fixes the "Unable to Enumerate Device" error.
* Enables the selection of 32-bit resolutions.
* Fixes the framerate issues that can occur on modern PCs.
* Adds configurable mouse look for normal gameplay and visor aiming.
* Allows the player to immediately skip the ESRB and Copyright screens.
* Allows the game to be played in widescreen with no 3D stretching.
* Increases the render distance of levels, with a configurable distance value.
* Works on every regional release of the game.

# Download
Get the latest version [from the releases page](https://github.com/Juan-Antonio-Doe/ToyStory2Fix/releases/latest). Extract the ZIP file into the folder you installed Toy Story 2 into.

# Configuration
You can enable or disable any part of the patch by opening the `scripts\ToyStory2Fix.ini` file and setting the options to `true` or `false`.

## Mouse look

Mouse look is enabled by default. It uses relative cursor movement while the game window is focused and leaves the original keyboard/controller action masks untouched, so Tab continues to enter visor/aim mode and target-lock actions continue to work.

- `MouseSensitivity` controls camera movement in game-angle units per mouse pixel (default `4.0`; valid range `0.1`–`32.0`).
- `InvertMouseX` and `InvertMouseY` invert their respective axes.
- Set `MouseLook = false` to disable the feature.

## Render distance value
When `IncreaseRenderDistance` is enabled, the `RenderDistanceValue` option controls how far the render distance is increased. It accepts:
* A plain or scientific-notation number, with an optional trailing `f`/`F` (e.g. `1.45e8`, `1.45e8f`, `100`, `100.0f`).
* The keyword `SQRT_FLT_MAX` (default), the maximum safe distance.
* The keyword `INFINITY`, which disables distance culling entirely, can cause issues such as some NPCs rendering incorrectly or FPS drops.

Keywords are not case sensitive. `1.45e8f` is the value closest to the game's original, unmodified render distance. Invalid, zero, or negative values automatically fall back to a safe default.

## Log file
A `ToyStory2Fix.log` file is created alongside the `.asi` file, recording runtime information such as the render distance value that was parsed and applied. Use it to confirm your `RenderDistanceValue` setting is being read correctly.

# Credits
* [RibShark](https://github.com/RibShark/ToyStory2Fix) — main repository and original developer.
* [AndetSTK](https://github.com/AndetSTK/ToyStory2Fix) — upstream repository.

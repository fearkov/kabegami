# Kabegami

A wallpaper studio for the Nintendo 3DS HOME Menu, made for real consoles.
It restyles the default top screen, the wall of drifting squares, on your
own 3DS.

Kabegami reads your console's own HOME Menu files and draws the wallpaper
from them the way the console does. The model, material, camera, animations
and vertex shader run through a software PICA200. Your changes go back into
the same file, and Luma3DS loads it from the SD card. Nothing is written to
the NAND.

## What you can change

- **Colours**: the wall, the light the squares and the gradient reach, the
  gradient's strength, the squares' opacity, the second layer's weight.
- **Cell**: any picture, as the 64x64 tile both layers repeat.
- **Layers**: how many cells each layer repeats, and where the still one sits.
- **Motion**: how far the squares drift each loop, and how long a loop lasts.
- **Waves**: a ready look with soft light ribbons in place of the squares.

A theme with its own top screen replaces these while it is active.

## What you need

- A 3DS with Luma3DS 13.4 or newer.
- Your HOME Menu, dumped with GodMode9: the decrypted CIA it builds from
  `SYSNAND CTRNAND > title > 00040030 > <id> > content`, a decrypted CXI, or
  the extracted RomFS. Kabegami contains no Nintendo data.

## Using it

`kabegami` opens the window. Drop your dump on it, change the look, and
install it to your SD card.

The command line does the same:

```sh
kabegami render HOME.cia --waves -o waves.png
kabegami install HOME.cia --waves --sd /run/media/you/3DS
kabegami uninstall HOME.cia --sd /run/media/you/3DS
```

`kabegami help` lists every option.

The mod goes to `luma/titles/<HOME Menu title id>/romfs/3D/BannerBG_LZ.bin`
on the SD card. Turn on "Enable game patching" in Luma's settings: hold
SELECT while powering on. If the HOME Menu doesn't start, turn game patching
off there, or delete that folder.

## Building

You need CMake 3.24 or newer, a C++20 compiler, and GLFW with OpenGL 3.3 for
the window. CMake fetches the rest.

```sh
cmake -S . -B build
cmake --build build -j
```

`-DKABEGAMI_GUI=OFF` builds the command line alone.

## License

MIT. `docs/` describes how it works.

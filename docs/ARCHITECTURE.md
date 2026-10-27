# How Kabegami is put together

Kabegami draws the HOME Menu's default top-screen wallpaper from the console's
own files, the way the console draws it, and writes edits back as a LayeredFS
mod. Its counterpart for the PS3 is [moyou](https://github.com/sagemono/moyou),
which runs the XMB's own shaders.

On the 3DS, the wallpaper is the CGFX model `3D/BannerBG_LZ.bin` in the HOME
Menu's RomFS. Its squares drift by the material animation `BannerBG_Loop`,
stored in the same file. The HOME Menu draws it through NintendoWare for CTR
(NW4C): `3D/BannerCamera_LZ.bin` holds the camera, and
`3D/nwgfx_DefaultShader_LZ.bin` the vertex shader.

Kabegami reads the three files, rebuilds the PICA200 state NW4C would set, and
runs it through a software PICA200: a C++ port of
[Zakuro](https://github.com/fearkov/zakuro)'s GPU. So the picture comes from
the console's own data, shader and material settings, not from an imitation.

## Parts

Each part is a static library in `src/core/<part>`, with its tests in
`tests/<part>` (`CMakeLists.txt`, `kabegami_module`).

| Part | What it does |
|---|---|
| `.` (base) | `bytes.hpp` (bounds-checked reads), `lz11` (the `_LZ.bin` compression), `sha256`, `md5` (for the material hash words), `home` (the HOME Menu from a decrypted CIA, CXI or NCSD, or an extracted RomFS) |
| `cgfx` | the CGFX container: header, the 15 DATA dictionary slots, DICTs and their Patricia lookup, self-relative links |
| `cgfx/model` | models, meshes, shapes, vertex and index buffers, skeletons and bones, textures |
| `cgfx/material` | materials: colours, texture coordinators and their matrices, texture mappers, the TEV stages, the raw PICA command blocks, the hash words |
| `cgfx/anim` | animations: curves, segments, key formats, and how HOME plays them |
| `cgfx/program` | the camera and its matrices, and the shader program (SHDR, DVLB, DVLE) |
| `pica/shader` | the vertex shader unit, ported from Zakuro |
| `pica/fragment` | textures, the TEV, and the depth, stencil and blend tests, ported from Zakuro |
| `pica/gpu` | command lists, registers, vertex fetch, the rasterizer, the framebuffer, ported from Zakuro |
| `scene` | the wallpaper drawn as HOME draws it: the animations evaluated, the bone palette, the uniforms, the material's command blocks with what the runtime fills in, one command list for the software PICA200, the picture turned upright |
| `edit` | what a user changes (colours, drift, cell), written into BannerBG_LZ.bin at its own size, and recipes of it |
| `image` | pictures in and out (stb) |
| `install` | where a mod goes for Luma3DS, Azahar and Zakuro, with `.orig` backups |

`src/app` holds the command line (`cli.cpp`) and the window (`gui/`, GLFW,
OpenGL 3.3, Dear ImGui). The window only shows what the software PICA200
draws, on a thread of its own. `KABEGAMI_SCREENSHOT=<file.png> kabegami window
<home>` keeps the window hidden, draws it into a framebuffer for two seconds
and saves that, for checking the window without one on the screen.

## Specifications

`docs/research/` holds the format and runtime specifications the code follows.
They were written from the files themselves and from HOME's own code, and each
says which facts are verified and which are not:

- `cgfx-model.md`: the container, models, shapes, skeletons;
- `cgfx-material-texture.md`: materials, textures, what HOME's theme code
  changes in the wallpaper's material;
- `cgfx-animation.md`: animations and HOME's curve evaluator;
- `camera-shader-nw4c.md`: the camera's matrices, DefaultShader, and what NW4C
  puts in each uniform;
- `zakuro-gpu-recipe.md`: driving the PICA200 emulation register by register.

## Data

Kabegami never ships Nintendo data. It works only on the user's own HOME Menu,
dumped with GodMode9. Tests that need real files run when `KABEGAMI_ROMFS`
names an extracted RomFS, and pass without it.

# zakuro-gpu as a library: the recipe

Kabegami research note. Checked against Zakuro commit `e174910` (v0.2.30), crate
`crates/zakuro-gpu`, on 2026-10-25, by reading the source and by building and
running the example in section 10. Source references are `file:line` in that
tree; a bare file name is in `crates/zakuro-gpu/src/`, `core/` means
`crates/zakuro-core/src/`. HOME Menu offsets are into the **decompressed** data
of the EUR HOME Menu 11.17 RomFS (title 0004003000009802 v29696).

Files read, SHA-256 of the compressed file / of the decompressed data:

| File | Compressed | Decompressed |
|---|---|---|
| `3D/BannerBG_LZ.bin` (7212 → 86080 bytes) | `27d58c2113d2c2d46e3bcc36bb2ae56c35e19d9823488287b6759df998108711` | `092c8682d0cfabf0a1823a8e3a2c12556515c437afba2aa6f6ac7fc4d5e34595` |
| `3D/BannerBGmask_LZ.bin` (8354 → 23360) | `d8c3cf350e2af35263f640a80144a40aa1d9a1b878f1b6eb872be0b4ec669541` | `d9139c60d5d608704b26252699a275f034ba04640859e0a8b1b9037047e1d233` |
| `3D/nwgfx_DefaultShader_LZ.bin` (3607 → 10896) | `e5240dd7d4e6dcd9fd362980a60b6fe093d9bfbde038c6182a948344808ce592` | `cfe8d7421feceacdcd3c5f675e4a152f164107afb8abb903bc2b2c647bf65a48` |

"Verified by the example" means an assertion in the example's `main.rs`
passed. "3dbrew" means the fact comes from 3dbrew's GPU pages and zakuro-gpu
does not read that field, so nothing here checked it.

## 1. Purpose

Kabegami will draw the HOME Menu wallpaper by running the files' own PICA200
data (command words, shader binary, textures, vertices) through zakuro-gpu's
software rasterizer, outside the emulator. This page is the recipe for that:
what to implement on the host side, what to put in memory, which registers a
command list has to set for one draw to land in a colour buffer, and how to
read the pixels back as RGBA8 the right way up. A working example (section 10)
draws one triangle with a hand-written pass-through vertex shader and a
constant-colour TEV stage and saves PNGs.

## 2. The recipe in eight steps

1. Depend on `zakuro-gpu` by path. With `default-features = false` the vertex
   shaders are interpreted and Cranelift stays out of the build (14 s for the
   example from cold); the `jit` feature compiles them (49 s). Both gave
   byte-identical pictures.
2. Implement `GpuMemory` over a host `Vec<u8>` that stands for a window of
   **physical** addresses, and keep the trait's identity `translate`.
3. `Gpu::new()` and `SoftwareRenderer::default()`. The rasterizing happens
   inside `Gpu`; the renderer only hears about draws.
4. Write the vertex arrays, the index buffer, the textures (bytes exactly as the
   file holds them) and the command list into the window. Align buffers to 8
   bytes, and make the list's size a multiple of 16 bytes.
5. Clear the colour buffer with `gpu.memory_fill(...)` (section 7.4).
6. Build the command list (section 5): colour buffer, viewport, output merger
   (**set LOGIC_OP to COPY**), TEV, texture units, vertex shader (code,
   descriptors, entry point, input permutation, output mask and map,
   uniforms), attribute loaders, index buffer, draw trigger, then
   GPUREG_FINALIZE.
7. `gpu.process_command_list(&mut memory, &mut renderer, list_paddr, list_bytes)`.
8. Read back by untiling with `format::morton_offset` and
   `ColorFormat::decode`, or with `gpu.display_transfer` into a linear buffer.
   The first row is the top of the picture. If the projection was the
   console's quarter-turned one, turn the picture a quarter counter-clockwise
   (section 8).

## 3. The library surface

| Item | Signature, behaviour | Source |
|---|---|---|
| `trait GpuMemory` | `read(&mut self, addr, &mut [u8])` and `write(&mut self, addr, &[u8])` are required. `slice`/`slice_mut` (default `None`) give the bytes in place. `read_u32` has a default. `translate(paddr) -> addr` defaults to identity. `guard`/`guard_writes` are no-ops that only the Vulkan renderer uses. | `lib.rs:31-64` |
| `Gpu::new()` | 0x300 internal registers, all 0; 16 fixed attributes at (0, 0, 0, 1); empty shader units | `lib.rs:191-212` |
| `Gpu::process_command_list(&mut self, memory: &mut M, renderer: &mut dyn Renderer, paddr: u32, size: u32)` | runs `size` bytes of commands from `paddr`, following jumps, and draws | `lib.rs:583-594`, `800-867` |
| `Gpu::memory_fill(&mut self, memory, start, end, value, width)` | the PSC fill engine; `end` is exclusive; `width` 2 or 3 gives a 16- or 24-bit pattern, any other value a 32-bit one | `lib.rs:296-344` |
| `Gpu::display_transfer(&mut self, memory, input, output, input_dimensions, output_dimensions, flags)` | the PPF transfer engine, tiled to linear, linear to tiled, format conversion, flip, downscale | `lib.rs:348-529` |
| `Gpu::texture_copy(&mut self, memory, input, output, size, input_gap, output_gap)` | raw copy with line gaps in 16-byte units | `lib.rs:532-577` |
| `Gpu::set_framebuffer`, `write_external`, `framebuffers` | the LCD registers; zakuro-gpu stores them and reads no screen out itself (zakuro-core does, section 9) | `lib.rs:217-291` |
| `pub` fields | `internal: Box<[u32]>` is the register file. Reading it is fine; writing it skips every side effect (draws, uploads). `vertex_shader` / `geometry_shader` are `ShaderUnit`s with `pub program, descriptors, float_uniforms, int_uniforms, bool_uniforms, entry_point`. The counters are `draw_calls, vertices_drawn, command_lists, fills, transfers, busy`. | `lib.rs:140-177`, `shader/mod.rs:40-60` |
| `SoftwareRenderer { draws }`, `trait Renderer`, `DrawCall { indexed, registers }` | a renderer is told of each draw, with the whole register file, and of each shader code and descriptor word. A custom one is a free per-draw register dump for debugging. | `renderer.rs:1-67`, `lib.rs:1056-1061` |
| `format::{ColorFormat, morton_offset, morton_interleave}` | pixel decode/encode and the tiled layout | `format.rs:4-163` |
| `texture::{TextureFormat, sample_texel}`, `shader::isa::decode_float24` | texture formats, float24 | `texture.rs:34-80`, `shader/isa.rs:287` |

`write_internal` is private (`lib.rs:1124`). A command list is the only way to
write registers with their effects.

## 4. Guest memory: `GpuMemory` over a host buffer

Every address zakuro-gpu uses comes from a register or an argument as a
physical address and goes through `translate` before any `read`, `write` or
`slice` (`lib.rs:308, 402, 1258`; `raster.rs:240, 1373, 1628, 2173, 2411`).
zakuro-core's `GuestMemory` turns physical addresses into its process's virtual
ones there (`core/lib.rs:1208`). A host buffer can skip all that: index the
buffer by physical address minus a base, and leave `translate` alone.

| Call | Who uses it | Notes |
|---|---|---|
| `slice` | the command list reader (`lib.rs:1260`), vertex fetch (`raster.rs:242`), the index buffer (`raster.rs:2185`), texture bytes (`raster.rs:1382`) | `None` falls back to a `read` into a copy. The tests' `FlatMemory` (`lib.rs:1402`) implements only `read`/`write`. |
| `slice_mut` | `memory_fill` in place (`lib.rs:324`) | `None` falls back to `write` |
| `read` / `write` | colour and depth buffers, which a draw always copies: the rows of 8x8 tiles it can reach are read, then written back if a pixel changed (`raster.rs:1700-1718`, `2659-2680`); display and texture transfers | keep reads outside the window from panicking: return zeros |

Constraints that come from the registers:

* Registers hold an address shifted right by 3, in 28 bits (`raster.rs:24-26`),
  so buffers are 8-byte aligned and below 0x8000_0000.
* A texture unit pointed at address 0 counts as no texture (`raster.rs:1367-1371`).
  A window at VRAM's 0x1800_0000, as in the example, keeps clear of that.
* Window size: the example's 6 MiB holds a 240x400 RGBA8 buffer (375 KiB), the
  transfer outputs, vertices and the list.

## 5. Command lists

### 5.1 Layout

A command is a parameter word, then a header word, then the extra parameters,
then one padding word when there is an odd number of them, so every command
starts 8-byte aligned.

| Word, bits | Meaning | zakuro-gpu | Seen in BannerBG: the TEV stage 0 command, parameter at 0x01134, header at 0x01138 = `0x804F00C0` |
|---|---|---|---|
| word 0 | the first parameter | `lib.rs:818` | `0x0E0403EE` (TEXENV0_SOURCE) |
| header bits 0-15 | register id. zakuro-gpu ends the list at an id of 0x300 or more | `lib.rs:822`, `830-837` | 0x00C0 |
| header bits 16-19 | byte enable mask: bit 16 enables bits 0-7 of the register, and so on; 0xF writes the word | `lib.rs:823`, `1275-1289` | 0xF |
| header bits 20-27 | number of extra parameters, 0-255 | `lib.rs:824` (8 bits) | 4 |
| header bits 28-30 | unused, 0 | not read | 0 |
| header bit 31 | consecutive: parameter *i* goes to register + *i*. Clear, every parameter goes to the same register. | `lib.rs:825`, `915-918` | 1 |
| extra parameters | | | OPERAND `0x00060020`, COMBINER `0x00010004`, COLOR `0xFF000000`, SCALE `0` |
| padding | after an odd extra count, not read | `lib.rs:847-849` | the blend block (header `0x803F0100` at 0x00DAC, 3 extra) is followed by a 0 at 0x00DBC |

* Several parameters to one plain register: they are written in turn and the
  last stays (`lib.rs:892-897`). Several parameters to a port register (shader
  code, operand descriptors, float uniform data, the lighting, fog and
  procedural texture tables) are consumed one by one (`lib.rs:926-1022`).
* BannerBG uses masked writes. DEPTH_COLOR_MASK has mask 0x1 (header
  `0x00010107` at 0x00D88, value `0x10` at 0x00D84) and TEXENV_UPDATE_BUFFER
  has mask 0x2 (header `0x000200E0` at 0x011EC, value `0x400` at 0x011E8).
  zakuro-gpu applies byte masks on every path (tests
  `byte_masks_cover_the_bytes_their_bits_name` `lib.rs:2325` and
  `every_register_takes_commands_as_single_writes` `lib.rs:2009`).
* The material command blocks in BannerBG are complete commands. They can be
  copied into a list one after another as they are.
* Size: zakuro-gpu reads `size` bytes as words and runs until they end
  (`lib.rs:810-817`). The console also needs the size to be a multiple of 16
  bytes and GPUREG_FINALIZE (0x0010, any value, 0x12345678 by custom) in the
  last 8 bytes (3dbrew). zakuro-gpu only stores 0x0010. The example pads with
  a mask-0 write to register 0x0000, as SPICA's `PICACommandWriter.WriteEnd`
  does.
* Registers persist from list to list in `gpu.internal`, as on the console. A
  frame's list only needs what changed.
* Immediate-mode vertices still waiting when the list ends are drawn
  (`lib.rs:865`).

### 5.2 Jumps

| Register | Bits | Meaning | zakuro-gpu |
|---|---|---|---|
| 0x238 / 0x239 CMDBUF_SIZE0/1 | 0-20 | size in bytes >> 3 | `lib.rs:1158` |
| 0x23A / 0x23B CMDBUF_ADDR0/1 | 0-28 | physical address >> 3 | `lib.rs:1159` |
| 0x23C / 0x23D CMDBUF_JUMP0/1 | any write | continue at that buffer once the current command ends. Nothing returns there; jumping back is the callee's job. A list that jumps more than 0x10000 times is stopped. | `lib.rs:1154-1161`, `853-861`; test `command_buffers_jump_into_each_other` `lib.rs:1617` |

A sub-buffer that ends with a JUMP1 write (the shape SPICA writes) continues at
whatever ADDR1/SIZE1 hold. Those registers persist, so write SIZE1 = 0 (the
jump then reads nothing and the list ends) or point them at your own
continuation before running such a buffer alone.

## 6. Registers by group

Every table lists what zakuro-gpu's software path reads, plus the registers the
console also wants (marked "not read", from 3dbrew). "Example" is the value the
example writes. "BannerBG" is a value from BannerBG's material command blocks
(section 6.10) where they carry the register.

### 6.1 Colour and depth buffers

| Register | Bits | Meaning | zakuro-gpu | Example | BannerBG |
|---|---|---|---|---|---|
| 0x11D COLORBUFFER_LOC | 0-27 | physical address >> 3 | `raster.rs:2410` | `0x03000000` (0x1800_0000) | — |
| 0x117 COLORBUFFER_FORMAT | 16-18 | 0 RGBA8, 1 RGB8, 2 RGB5A1, 3 RGB565, 4 RGBA4. 2 and 3 are swapped compared with transfers and screens. | `raster.rs:2414`, `format.rs:27-33` | `0x00000002` | — |
| | 0-1 | pixel size, 0 16-bit, 2 32-bit | not read | 2 | — |
| 0x11E FRAMEBUFFER_DIM | 0-10 | width | `raster.rs:2421` | 240 | — |
| | 12-21 | height − 1 | `raster.rs:2422` | 399 | — |
| | 24 | 3dbrew: vertical flip, 0 on, 1 off | not read | 1 (`0x0118F0F0` in all) | — |
| 0x11C DEPTHBUFFER_LOC | 0-27 | physical address >> 3 | `raster.rs:1628` | `0x03020000` | — |
| 0x116 DEPTHBUFFER_FORMAT | 0-1 | 0 D16 (2 bytes), 2 D24 (3 bytes), 3 D24S8 (4 bytes, stencil in the top byte); zakuro-gpu treats 1 as 4 bytes | `raster.rs:1620`, `1629-1633` | 3 | — |
| 0x113 COLORBUFFER_WRITE | 0-3 | non-zero allows colour writes | `raster.rs:2443` | 0xF | — |
| 0x115 DEPTHBUFFER_WRITE | 0-1 | non-zero allows depth and stencil writes (3dbrew: bit 0 stencil, bit 1 depth) | `raster.rs:1619` | 0 | — |
| 0x112 / 0x114 COLORBUFFER_READ / DEPTHBUFFER_READ | | blending reads the buffer whatever these say | not read | 0xF / 0 | — |
| 0x110 / 0x111 FRAMEBUFFER_INVALIDATE / FLUSH, 0x11B FRAMEBUFFER_BLOCK32, 0x06E RENDERBUF_DIM | | zakuro-gpu always tiles 8x8 | not read | 1 / 1, 0, as 0x11E | — |

A buffer's size is the larger of FRAMEBUFFER_DIM and the viewport's far corner
(`raster.rs:2418-2423`). A viewport bigger than the buffer makes zakuro-gpu
write past it.

### 6.2 Viewport, depth map, culling, scissor

| Register | Bits | Meaning | zakuro-gpu | Example |
|---|---|---|---|---|
| 0x041 VIEWPORT_WIDTH | 0-23 | float24, width / 2 | `raster.rs:2403`; 0 or below drops the draw (`2405-2407`) | `0x45E000` (120.0) |
| 0x043 VIEWPORT_HEIGHT | 0-23 | float24, height / 2 | `raster.rs:2404` | `0x469000` (200.0) |
| 0x042 / 0x044 VIEWPORT_INVW / INVH | 1-31 | float1.7.23 of 2 / size (3dbrew) | not read | not written |
| 0x068 VIEWPORT_XY | 0-9, 16-25 | signed x and y of the corner. y counts up from the bottom row of the buffer. | `raster.rs:2397-2399`; test `a_viewport_offset_places_the_image_from_the_bottom` `raster.rs:3110` | 0 |
| 0x065-0x067 SCISSORTEST_* | | scissor. **zakuro-gpu has none.** Drawing is held to the viewport, cut to the buffer. | not read; bounds at `raster.rs:2474-2479` | 0 |
| 0x04D DEPTHMAP_SCALE | 0-23 | float24 | `raster.rs:1682` | `0xBF0000` (−1.0) |
| 0x04E DEPTHMAP_OFFSET | 0-23 | float24 | `raster.rs:1683` | 0 |
| 0x06D DEPTHMAP_ENABLE | 0 | 1 z-buffer; 0 w-buffer (the depth is multiplied by w) | `raster.rs:1684`, `1908-1913` | 1 |
| 0x040 FACECULLING_CONFIG | 0-1 | 0 keeps all; 1 drops counter-clockwise triangles, 2 drops clockwise ones, with y up | `raster.rs:2514`, `2585-2588`; test `face_culling_keeps_one_winding` `raster.rs:3094` | 0 |

BannerBG's material sets FACECULLING_CONFIG to 1 with mask 0x1 (value at
0x00D78, header `0x00010040` at 0x00D7C). Its triangles have to be clockwise
on screen, or they are dropped.

Clipping keeps w > 1e-5 and −w ≤ z ≤ 0 (`raster.rs:861`). x and y are not
clipped, only held to the viewport rectangle. Vertices snap to 1/16 pixel
(`raster.rs:846`). Depth is (z / w) · scale + offset, clamped to 0..1.

### 6.3 Output merger

| Register | Bits | Meaning | zakuro-gpu | Example | BannerBG |
|---|---|---|---|---|---|
| 0x100 COLOR_OPERATION | 8 | 1 blend, 0 logic op | `blend.rs:136-140`, `225-230` | 0 (`0x00E40000`) | 1 (`0x00E40100` at 0x00DA8) |
| | 0-1 | fragment mode: 0 default, 1 gas, 3 shadow | not read | 0 | 0 |
| | 16-25 | fixed 0x0E4 (3dbrew) | not read | 0xE4 | 0xE4 |
| 0x101 BLEND_FUNC | 0-2, 8-10 | RGB and alpha equation: 0 add, 1 subtract, 2 reverse subtract, 3 min, 4 max, 5-7 add | `blend.rs:19-29`, `145-150` | — | `0x01010000` at 0x00DB0: add, source one, destination zero, for RGB and alpha |
| | 16-19, 20-23, 24-27, 28-31 | RGB source, RGB destination, alpha source, alpha destination: 0 zero, 1 one, 2/3 source colour (1−), 4/5 destination colour (1−), 6/7 source alpha (1−), 8/9 destination alpha (1−), 10/11 constant colour (1−), 12/13 constant alpha (1−), 14 source alpha saturate, 15 one | `blend.rs:65-83` | — | BannerBGmask has `0x01760000` at 0x00DB0: RGB source alpha / one minus source alpha, alpha one / zero |
| 0x102 LOGIC_OP | 0-3 | 0 clear, 1 and, 2 reverse and, **3 copy**, 4 set, 5 inverted copy, 6 noop, 7 invert, 8 nand, 9 or, 10 nor, 11 xor, 12 equivalent, 13 inverted and, 14 reverse or, 15 inverted or | `blend.rs:225-230` | 3 | 0 at 0x00DB4 (blending is on) |
| 0x103 BLEND_COLOR | 0-31 | RGBA8 constant, R in bits 0-7 | `blend.rs:143`, `151-156` | — | `0xFF000000` at 0x00DB8 |
| 0x104 FRAGOP_ALPHA_TEST | 0, 4-6, 8-15 | enable, function (0 never, 1 always, 2 =, 3 ≠, 4 <, 5 ≤, 6 >, 7 ≥), reference | `raster.rs:1502-1508` | 0 | `0x10` at 0x011D8 (off) |
| 0x105 STENCIL_TEST | 0, 4-6, 8-15, 16-23, 24-31 | enable, function, write mask, reference, compare mask. The reference is on the left of the comparison. Only with a D24S8 buffer. | `raster.rs:1573-1594`, `1623` | 0 | 0 with mask 0xD (value 0x00DC0, header `0x000D0105` at 0x00DC4) |
| 0x106 STENCIL_OP | 0-2, 4-6, 8-10 | on stencil fail, depth fail, pass: 0 keep, 1 zero, 2 replace, 3/4 increment/decrement saturating, 5 invert, 6/7 increment/decrement wrapping | `raster.rs:1529-1540`, `1584-1586` | — | 0 at 0x00DC8 |
| 0x107 DEPTH_COLOR_MASK | 0, 4-6 | depth test enable, function | `raster.rs:1618-1621` | 0 | `0x10` with mask 0x1 (byte 0 only: test off, "always") |
| | 8-11 | R, G, B, A write enable | `raster.rs:2444`, `2483` | 0xF (`0x00000F00` in all) | not in the block |
| | 12 | depth write. Turning the test off does not stop depth writes (3dbrew); zakuro-gpu also writes depth whatever bit 0 says. | `raster.rs:1622` | 0 | not in the block |

zakuro-gpu runs a fragment through these steps (`raster.rs:1834-2035`):

1. textures and the TEV, with fragment lighting when a stage reads it;
2. the float result → 8 bits by `(c * 255.0) as u8`, truncating (`raster.rs:1970`);
3. alpha test, then fog;
4. stencil test and update, depth test and write;
5. blend or logic op, the channel mask, and the write.

Every 8-bit TEV constant comes back unchanged through step 2: a check of all
256 values with f32 arithmetic found none that changes. Interpolated values
truncate, so 0.5 gives 127.

### 6.4 Texture combiners (TEV)

Stage *n* has five registers from its base: SOURCE, OPERAND, COMBINER, COLOR,
SCALE. The bases are 0x0C0, 0x0C8, 0x0D0, 0x0D8, 0x0F0, 0x0F8 (`tev.rs:4`).

| Register | Bits | Meaning | zakuro-gpu | Example (stage 0; stages 1-5) | BannerBG stage 0 |
|---|---|---|---|---|---|
| SOURCE | 0-3, 4-7, 8-11 | RGB inputs 0-2 | `tev.rs:150-159` | 0xE (constant); 0xF | `0x0E0403EE` at 0x01134: RGB constant, constant, texture 0 |
| | 16-19, 20-23, 24-27 | alpha inputs 0-2. 0 primary (vertex) colour, 1 fragment primary, 2 fragment secondary, 3-6 texture 0-3, 0xD previous buffer, 0xE constant, 0xF previous; zakuro-gpu takes other values as previous | `tev.rs:28-42` | 0xE; 0xF | alpha: texture 1, primary, constant |
| OPERAND | 0-3, 4-7, 8-11 | RGB: 0 colour, 1 1−colour, 2 alpha, 3 1−alpha, 4/5 red (1−), 8/9 green (1−), 0xC/0xD blue (1−) | `tev.rs:160`, `305-319` | 0 | `0x00060020` at 0x0113C: colour, alpha, colour |
| | 12-14, 16-18, 20-22 | alpha: 0 alpha, 1 1−alpha, 2/3 red, 4/5 green, 6/7 blue | `tev.rs:161-165`, `321-332` | 0 | alpha, blue, alpha |
| COMBINER | 0-3, 16-19 | RGB and alpha: 0 replace, 1 modulate, 2 add, 3 add signed, 4 interpolate (s0·s2 + s1·(1−s2)), 5 subtract, 6 dot3 RGB, 7 dot3 RGBA (also replaces alpha), 8 multiply-add, 9 add-multiply | `tev.rs:60-74`, `334-375` | 0 | `0x00010004` at 0x01140: RGB interpolate, alpha modulate |
| COLOR | 0-31 | constant, R in bits 0-7 up to A in 24-31 | `tev.rs:287-294` | `0xFF2040E0`; 0 | `0xFF000000` at 0x01144 |
| SCALE | 0-1, 16-17 | RGB and alpha scale, 0 ×1, 1 ×2, 2 ×4 | `tev.rs:297-303` | 0 | 0 at 0x01148 |
| 0x0E0 TEXENV_UPDATE_BUFFER | 8-11, 12-15 | stage 0-3 writes its RGB, its alpha into the buffer that later stages read as source 0xD (3dbrew numbers them TEV 1-4) | `tev.rs:178-181` | 0 | `0x400` with mask 0x2 at 0x011E8: stage 2's RGB |
| | 0-2, 16 | fog mode (5 fog; 7 gas, which zakuro-gpu ignores), fog z flip | `fog.rs:79-83` | 0 | not in the block (mask 0x2) |
| 0x0FD TEXENV_BUFFER_COLOR | 0-31 | the buffer's first colour. Stage 0 reads 0 there, stage 1 this colour, and an update shows two stages later. | `tev.rs:204-259` | 0 | `0xFF000000` at 0x011E0 |

All registers at 0 (a fresh `Gpu`) make every stage REPLACE with the primary
colour, so a draw comes out in its vertex colour. A shader that outputs no
colour gives white (`raster.rs:512-516`).

In all six BannerBG stages the COLOR word is `0xFF000000` (0x01144, 0x01160,
0x0117C, 0x01198, 0x011B4, 0x011D0). The word just before each stage's
command (0x01130, 0x0114C, 0x01168, 0x01184, 0x011A0, 0x011BC) reads 0, 5, 1,
2, 3, 4. SPICA reads that word as `GfxTexEnv.Constant`, which material
constant colour the stage uses. So the runtime presumably writes the
material's constant colours into the COLOR registers, and Kabegami has to do
the same (unverified beyond the field order, see section 12).

### 6.5 Texture units

| Register | Bits | Meaning | zakuro-gpu | BannerBG (unit 0 block: header `0x809F0081` at 0x00F5C) |
|---|---|---|---|---|
| 0x080 TEXUNIT_CONFIG | 0, 1, 2 | units 0-2 on | `raster.rs:1353` | not in the material blocks |
| | 10, 8-9 | unit 3 (procedural) on, the coordinates it reads | `proctex.rs:147-149`, `157` | — |
| | 13 | unit 2 reads texcoord 1 | `raster.rs:2492` | — |
| | 12, 16 | fixed 1; clear the texture cache (3dbrew) | not read | — |
| BORDER_COLOR 0x081 / 0x091 / 0x099 | 0-31 | RGBA8, R in bits 0-7 | `raster.rs:1404` | `0xFF000000` at 0x00F58 |
| DIM 0x082 / 0x092 / 0x09A | 0-10, 16-26 | height, width | `raster.rs:1358-1360` | **0** at 0x00F60 |
| PARAM 0x083 / 0x093 / 0x09B | 1 | linear filter (bilinear). zakuro-gpu uses it when minifying too. | `raster.rs:1398` | `0x1106` at 0x00F64: linear |
| | 2 | minification filter | not read | 1 |
| | 8-10, 12-14 | wrap T, wrap S: 0 clamp to edge, 1 clamp to border, 2 repeat, 3 mirrored repeat; zakuro-gpu treats 4/5 as 0/1 and 6/7 as repeat | `raster.rs:1245-1254`, `1399-1400` | T 1, S 1 (clamp to border); units 1 and 2: `0x2206` at 0x00FF0 and 0x0107C (repeat) |
| | 4-5, 20, 24, 28-30 | ETC1 flag, shadow, mipmap filter, type 2D/cube/shadow/projection | not read: zakuro-gpu samples a plain 2D level 0 | 0 |
| LOD 0x084 / 0x094 / 0x09C | | bias, levels | not read | 0 at 0x00F68 |
| ADDR 0x085 / 0x095 / 0x09D | 0-27 | physical address >> 3 (0x086-0x08A are cube faces) | `raster.rs:1367` | **0** at 0x00F6C |
| TYPE 0x08E / 0x096 / 0x09E | 0-3 | 0 RGBA8, 1 RGB8, 2 RGBA5551, 3 RGB565, 4 RGBA4, 5 LA8, 6 HILO8, 7 L8, 8 A8, 9 LA4, 0xA L4, 0xB A4, 0xC ETC1, 0xD ETC1A4 | `raster.rs:1365-1366`, `texture.rs:34-52` | **0** with mask 0x1 (value at 0x00F50, header `0x0001008E` at 0x00F54) |

* With DIM 0 or ADDR 0 the unit samples transparent black
  (`raster.rs:1322-1340`, `1361-1371`). BannerBG's material texture blocks
  (unit 0 at 0x00F58, unit 1 at 0x00FE4, unit 2 at 0x01070) carry DIM 0,
  ADDR 0 and TYPE 0. The runtime fills those in, and so does Kabegami: write
  DIM, ADDR and TYPE after the file's block, and turn the units on in
  TEXUNIT_CONFIG.
* Texel data is the PICA's tiled layout, the same 8x8 Morton tiles as the
  colour buffer. Multi-byte texels are stored in reverse (RGBA8 as A B G R,
  `texture.rs:115-120`), and 4-bit formats put the even texel in the low
  nibble (`texture.rs:97-112`). Put the file's image bytes in memory
  unchanged. A private decode of BG_64_00 (L8) and BG_CapMask_00 (A4) with
  this layout gave coherent pictures. BG_CapMask_00 is coherent only at 256
  wide by 512 tall, which confirms SPICA's height-then-width field order.
* Sampling: v = 1 is the first row in memory and v = 0 the last
  (`raster.rs:1290-1292`), the colour buffer's convention (section 8), so a
  rendered buffer can be sampled as a texture without a flip. Texel centres
  are at half texels; filtering is bilinear over 2x2 (`raster.rs:1298-1315`).
* Decoded textures are cached by (address, format, width, height) and checked
  again with a hash in each new command list (`raster.rs:1113-1201`). Editing
  texture bytes between `process_command_list` calls is picked up.

BannerBG's textures, from the TXOB fields (layout per SPICA's `GfxTexture` and
`GfxTextureImageData`), and the unit registers they make:

| Texture | TXOB at | Size | PICA format (TXOB +0x34) | Level 0 bytes, at | DIM | TYPE |
|---|---|---|---|---|---|---|
| BG_64_00 | 0x015FC | 64x64 | 7 L8 | 4096 at 0x03E00 | `0x00400040` | 0x7 |
| BG_CapMask_00 | 0x01658 | 256x512 | 11 A4 | 65536 at 0x04E00 | `0x01000200` | 0xB |
| BG_DefBase_00 | 0x016B4 | 32x32 | 12 ETC1 | 512 at 0x14E00 | `0x00200020` | 0xC |
| BG_DmyApp_00 | 0x01710 | 8x8 | 7 L8 | 64 at 0x15000 | `0x00080008` | 0x7 |

All four have one mip level. zakuro-gpu decodes L8, A4 and ETC1 (`texture.rs`).

### 6.6 Vertex shader

The vertex shader block is at 0x2B0 (`registers.rs:115-140`). Writes are applied
by `configure_shader` (`lib.rs:1338-1357`) and the ports (`lib.rs:926-1022`).

| Register | Bits | Meaning | zakuro-gpu | Example |
|---|---|---|---|---|
| 0x2B0 VSH_BOOLUNIFORM | 0-15, 16-31 | b0-b15; 0x7FFF on the console | `lib.rs:1340` (masks 0xFFFF) | `0x7FFF0005` |
| 0x2B1-0x2B4 VSH_INTUNIFORM_I0-I3 | 0-7, 8-15, 16-23 | x, y, z: a `loop` runs x + 1 times, starts aL at y, steps it by z (signed) | `lib.rs:1341-1343`, `shader/mod.rs:796-808` | i0 `0x00020103` |
| 0x2B9 VSH_INPUTBUFFER_CONFIG | 0-3 | number of input attributes − 1 | `raster.rs:182` | `0xA0000001` |
| | 24-31 | 0xA0 for the vertex shader (3dbrew) | not read | 0xA0 |
| 0x2BA VSH_ENTRYPOINT | 0-15 | entry, in words. zakuro-gpu masks it to 0-4095. Bits 16-31 are 0x7FFF on the console. | `lib.rs:1344` | `0x7FFF0000` |
| 0x2BB / 0x2BC VSH_ATTRIBUTES_PERMUTATION_LOW / HIGH | 4 bits each | the input register (v0-v15) of attribute 0-7 / 8-11 | `raster.rs:183-188` | `0x00000010` / 0 |
| 0x2BD VSH_OUTMAP_MASK | 0-15 | o0-o15 the shader outputs. The output map's register *i* is the *i*-th enabled one, which need not be o*i*. A mask of 0 takes the registers as they are. | `raster.rs:480-497`, `518-528` | `0b11` |
| 0x2BF VSH_CODETRANSFER_END | | end of an upload | not read: the program is decoded at the next draw (`lib.rs:1036-1041`) | 1 |
| 0x2C0 VSH_FLOATUNIFORM_INDEX | 0-6 (3dbrew 0-7) | the first uniform, c0-c95 | `shader/mod.rs:195-199` | 0, then `0x8000005F` |
| | 31 | 1: four float32 words a vector, w z y x; 0: three float24 words (section 7.1) | `shader/mod.rs:198`, `258-290` | |
| 0x2C1-0x2C8 VSH_FLOATUNIFORM_DATA0-7 | | one FIFO; each finished vector moves the index on | `lib.rs:948-959` | 12 words, 4 words |
| 0x2CB VSH_CODETRANSFER_INDEX | 0-11 | where the next code word goes, 4096 words | `lib.rs:1347-1349`, `shader/mod.rs:15` | 0 |
| 0x2CC-0x2D3 VSH_CODETRANSFER_DATA0-7 | | code FIFO | `shader/mod.rs:128-136` | 3 words |
| 0x2D5 VSH_OPDESCS_INDEX | 0-6 here | where the next descriptor goes, 128 descriptors | `lib.rs:1351-1353`, `shader/mod.rs:16` | 0 |
| 0x2D6-0x2DD VSH_OPDESCS_DATA0-7 | | descriptor FIFO | `shader/mod.rs:138-146` | 1 word |
| 0x242 VSH_NUM_ATTR | 0-3 | attributes − 1 | immediate mode only (`lib.rs:1193`, `raster.rs:2272`) | 1 |
| 0x244 VSH_COM_MODE | 0 | 1: the geometry unit has its own program; 0: it gets copies of the vertex program and descriptors | `lib.rs:964`, `1222-1229` | 0 |
| 0x229 GEOSTAGE_CONFIG | 0-1 | 2 runs the geometry shader; keep 0 | `lib.rs:1038`, `raster.rs:591` | 0 |
| 0x24A / 0x251 VSH_OUTMAP_TOTAL1 / 2 | 0-3 | outputs − 1 | TOTAL1 is read for geometry shaders only (`raster.rs:764`) | 1 / 1 |
| 0x04F SH_OUTMAP_TOTAL | 0-2 | number of output map registers, not minus one | `raster.rs:437-441`; 0 → a default map (o0 position, o2 colour, o3 texcoord 0) | 2 |
| 0x050-0x056 SH_OUTMAP_O0-O6 | 0-4, 8-12, 16-20, 24-28 | the semantic of x, y, z, w: 0x00-0x03 position, 0x04-0x07 normal quaternion, 0x08-0x0B colour, 0x0C-0x0D texcoord 0 u v, 0x0E-0x0F texcoord 1, 0x10 texcoord 0 w, 0x12-0x14 view, 0x16-0x17 texcoord 2, 0x1F none | `raster.rs:452-472`. A map without a position semantic → the default map. 0x10 is not read. | `0x03020100`, `0x0B0A0908`, 5 × `0x1F1F1F1F` |
| 0x064 SH_OUTATTR_MODE, 0x06F SH_OUTATTR_CLOCK | | texcoord use, attribute clock (3dbrew) | not read | 0, `0b11` |

The program runs per vertex from the entry point until `end`, at most 0x10000
instructions (`shader/mod.rs:713-750`; the batch and JIT paths start at the
same entry, `shader/batch.rs:102`, `shader/jit.rs:292`). Temporaries start at
(0, 0, 0, 1) (`shader/mod.rs:36`).

Hand-assembling (format 1, `shader/isa.rs:147-170`): the opcode is in bits
26-31 (MOV 0x13, END 0x22), the destination in 21-25 (o0-o15 0x00-0x0F,
r0-r15 0x10-0x1F), the address register index in 19-20, source 1 in 12-18
(v0-v15 0x00-0x0F, r0-r15 0x10-0x1F, c0-c95 0x20-0x7F), source 2 in 7-11, and
the descriptor index in 0-6. An operand descriptor (`shader/isa.rs:248-262`)
holds the destination mask in bits 0-3, bit 3 being x; then, for sources 1,
2 and 3, a negate bit at 4, 13, 22 and a swizzle at 5-12, 14-21, 23-30. A
swizzle has 2 bits per component, x's in the top pair, so 0x1B is x y z w.
The example's words: `mov o0, v0` = `0x4C000000`, `mov o1, v1` = `0x4C201000`,
`end` = `0x88000000`, descriptor 0 = `0x0D86C36F`.

### 6.7 From a SHBIN to those registers, checked on nwgfx_DefaultShader

The shader binary was found by its magic `DVLB` at 0x104 of the decompressed
nwgfx_DefaultShader_LZ.bin. The fields are those of 3dbrew's SHBIN page and
libctru's `shbin.c` (`DVLB_ParseFile`, `DVLE_GenerateOutmap`).

| Field | Offset | Value | Becomes |
|---|---|---|---|
| DVLB: DVLE count | 0x108 | 4 | |
| DVLB: DVLE offsets | 0x10C | +0xBC0, +0x12D8, +0x19C8, +0x2090 (from the DVLB) | |
| DVLP: magic, version | 0x11C, 0x120 | `DVLP`, 0x302 | |
| DVLP: code offset, size in words | 0x124, 0x128 | +0x28, 481 (SHA-256 of the code `e79aaac9035ee4d8db2afa4a0a08865b7ddccc2bb0175c5ebe35173f594f006b`) | CODETRANSFER_INDEX 0, then all 481 words to CODETRANSFER_DATA |
| DVLP: descriptor offset, count | 0x12C, 0x130 | +0x7AC, 86 entries of 8 bytes | OPDESCS_INDEX 0, then the **first** word of each entry (libctru does the same; the second words seen are 3, 6, 7 and 15) |
| DVLE 0: type, main, endmain | 0xCCA, 0xCCC, 0xCD0 | 0 (vertex), 0x0, 0x7 | VSH_ENTRYPOINT `0x7FFF0000` |
| DVLE 0: inputs, outputs used | 0xCD4, 0xCD6 | 0x0FFF, 0x007F | |
| DVLE 0: output table | 0xE94, 8 entries | o0 position xyzw, o1 normal quaternion xyzw, o2 view xyz, o3 colour, o4 texcoord 0 xy + texcoord 0 w in z, o5 texcoord 1 xy, o6 texcoord 2 xy | VSH_OUTMAP_MASK 0x007F, SH_OUTMAP_TOTAL 7, O0-O6 `0x03020100`, `0x07060504`, `0x1F141312`, `0x0B0A0908`, `0x1F100D0C`, `0x1F1F0F0E`, `0x1F1F1716` |
| DVLE 0: constant table | 0xD04, 4 entries of 0x14 bytes | c93 (0, 1, 2, 3), c94 (0.125, 1/256, 0.5, 0.25), c95 (3, 4, 5, 6) as float24 in 32-bit slots (1.0 = 0x3F0000), b0 = 1 | uploaded by the host. The constants are not in the code. |
| DVLE 1 at 0x13DC | | main 0x1D2, 7 outputs (no texcoord 2) | ENTRYPOINT `0x7FFF01D2`, mask 0x003F, total 6 |
| DVLE 2 at 0x1ACC | | main 0x1D8, 6 outputs (no texcoord 1, 2) | mask 0x001F, total 5 |
| DVLE 3 at 0x2194 | | main 0x1DD, 4 outputs (position, quaternion, view, colour) | mask 0x000F, total 4 |

DVLE 0's uniform table (0xED4, 45 entries) names the registers in the SHBIN's
numbering: 0x00-0x0F v0-v15, 0x10-0x6F c0-c95, 0x70-0x73 i0-i3, 0x78-0x87
b0-b15.

* Inputs: aPosition v0, aNormal v1, aTangent v2, aColor v3, aTexCoord0-2
  v4-v6, aBoneIndex v7, aBoneWeight v8, aUserAttribute0-2 v9-v11. The attribute
  permutation sends each of the mesh's attributes to its register here.
* Float: WrldMtx c0-c2, NormMtx c3-c5, PosOffs c6, IrScale c7-c9, TexcMap c10,
  TexMtx0 c11-c13, TexMtx1 c14-c16, TexMtx2 c17-c18, TexTran c19, MatAmbi c20,
  MatDiff c21, HslGCol c22, HslSCol c23, HslSDir c24, UnivReg c25-c84,
  ProjMtx c86-c89, ViewMtx c90-c92.
* Integer: LightCt i0.
* Bool: IsSmoSk b1, IsRgdSk b2, IsUniSc b3, UseNorM b4, IsHemiL b5, IsHemiO
  b6, IsVertA b7, IsBoneW b8, UvMap0-2 b9-b11, IsVertL b12, IsTex1 b13,
  IsTex2 b14, IsQuate b15.

What values HOME gives them, which DVLE BannerBG's material uses, and what
each bool does belong to the shader and material notes. This one gives only
the register each name lands in.

### 6.8 Vertex attributes

| Register | Bits | Meaning | zakuro-gpu | Example |
|---|---|---|---|---|
| 0x200 ATTRIBBUFFERS_LOC | 0-27 | base physical address >> 3 | `raster.rs:2166` | `0x03080000` |
| 0x201 / 0x202 ATTRIBBUFFERS_FORMAT_LOW / HIGH | 4 bits an attribute, 0-7 in LOW, 8-11 in HIGH bits 0-15 | bits 0-1 type: 0 byte, 1 ubyte, 2 short, 3 float. Bits 2-3: components − 1. | `raster.rs:56-61` | LOW `0x0000000F` |
| | HIGH 16-27 | fixed attribute mask | `raster.rs:189` | |
| | HIGH 28-31 | attributes − 1 | not read; zakuro-gpu counts with 0x2B9 | HIGH `0x10020000` |
| 0x203 + 3*i* ATTRIBBUFFER*i*_OFFSET | 0-27 | offset of array *i* from the base | `raster.rs:45` | 0 |
| 0x204 + 3*i* CONFIG1 | 4 bits each | components 0-7: an attribute id 0-11, or 12-15 for 4, 8, 12, 16 bytes of padding | `raster.rs:46`, `119-125` | 0 |
| 0x205 + 3*i* CONFIG2 | 0-15 | components 8-11 | `raster.rs:46` | |
| | 16-23 | bytes a vertex | `raster.rs:48` | 16 |
| | 28-31 | number of components | `raster.rs:49` | 1 (`0x10100000` in all) |
| 0x232 FIXEDATTRIB_INDEX | 0-3 | 0-11 the fixed attribute the next data sets; 15 immediate-mode vertices | `lib.rs:1163-1166`, `1181-1199` | 1 |
| 0x233-0x235 FIXEDATTRIB_DATA0-2 | | three words, packed like float24 uniforms (section 7.1). The index moves on after each. | `lib.rs:1167-1188` | (0, 1, 0, 1) = `0x3F000000`, `0x00003F00`, 0 |

* Inside a loader's vertex, each attribute is aligned to its component size,
  and a padding component first aligns to 4 bytes (`raster.rs:117-131`). CGFX
  vertex buffers state each attribute's offset. Check that it equals this
  packing, and put padding components where it doesn't.
* Values are not normalized: a ubyte 255 arrives as 255.0, a short as its
  integer (`raster.rs:260-278`). Scaling is the shader's job (DefaultShader
  has IrScale for it).
* Components the array lacks fill from (0, 0, 0, 1). An attribute no array
  gives takes its fixed value if its bit is in the fixed mask, else
  (0, 0, 0, 1) (`raster.rs:203-213`). Fixed values are read at the draw.

### 6.9 Index buffer and draws

| Register | Bits | Meaning | zakuro-gpu | Example |
|---|---|---|---|---|
| 0x227 INDEXBUFFER_CONFIG | 0-27 | offset from the **attribute base** | `raster.rs:2173` | 0x100 |
| | 31 | 1 u16 indices, 0 u8 | `raster.rs:2172` | 1 (`0x80000100`) |
| 0x228 NUMVERTICES | 0-31 | vertices (indices) to draw; 0 or above 0x10000 draws nothing | `raster.rs:2160-2164` | 3 |
| 0x22A VERTEX_OFFSET | 0-31 | first vertex of DrawArrays | `raster.rs:2161`, `2249` | 0 |
| 0x22E DRAWARRAYS / 0x22F DRAWELEMENTS | any write | draw now, in order / through the index buffer | `lib.rs:1152` | DRAWELEMENTS 1 |
| 0x25E PRIMITIVE_CONFIG | 8-9 | 0 triangles, 1 strip, 2 fan, 3 geometry primitive (a list in zakuro-gpu) | `raster.rs:2382-2388`, `2510` | 3, with mask 0x2 |
| | 0-3 | outputs − 1 | not read | 1 |
| 0x25F RESTART_PRIMITIVE, 0x245 START_DRAW_FUNC0, 0x229 / 0x253 GEOSTAGE_CONFIG / CONFIG2 bit 8, 0x231 VTX_FUNC | | the console's ritual around an indexed draw: restart, drawing mode, element mode, trigger, configuration mode, clear the post-vertex cache (citro3d's `C3D_DrawElements`, 3dbrew) | stored only | as citro3d |

Strips alternate their winding the usual way (`raster.rs:2384`), which matters
under culling. Vertex shading is split over threads from 512 vertices up, and
filling from 4096 covered pixels up (`raster.rs:726`, `2041`).

### 6.10 What BannerBG's material blocks hold

The blocks were found by scanning the decompressed BannerBG for 4-byte aligned
words with a known register id in a header shape. The scan turns up 23
candidates. Two are not commands: at 0x00DA4 the blend block's first value
was read as a header, and at 0x0327C the scan hit the size word of the IMAG
section, which starts at 0x03278, and the data after it.
BannerBGmask gives the same 23 headers at the same offsets, with its own
values (its BLEND_FUNC, for one).

| Offset (parameter word) | Header | Register(s) |
|---|---|---|
| 0x00D78 | `0x00010040` | FACECULLING_CONFIG = 1, mask 0x1 |
| 0x00D84 | `0x00010107` | DEPTH_COLOR_MASK = 0x10, mask 0x1 |
| 0x00DA8 | `0x803F0100` | COLOR_OPERATION, BLEND_FUNC, LOGIC_OP, BLEND_COLOR (consecutive) |
| 0x00DC0, 0x00DC8 | `0x000D0105`, `0x000F0106` | STENCIL_TEST, STENCIL_OP |
| 0x00F50, 0x00F58 | `0x0001008E`, `0x809F0081` | TEXUNIT0_TYPE (mask 0x1); unit 0 BORDER_COLOR to ADDR6, 10 registers |
| 0x00FDC, 0x00FE4 | `0x00010096`, `0x809F0091` | TEXUNIT1_TYPE; unit 1 block |
| 0x01068, 0x01070 | `0x0001009E`, `0x809F0099` | TEXUNIT2_TYPE; unit 2 block |
| 0x01134 + 0x1C·*n* | `0x804F00C0` … `0x804F00F8` | TEV stage *n*, five registers |
| 0x011D8 | `0x000F0104` | FRAGOP_ALPHA_TEST |
| 0x011E0 | `0x000F00FD` | TEXENV_BUFFER_COLOR |
| 0x011E8 | `0x000200E0` | TEXENV_UPDATE_BUFFER, mask 0x2 |
| 0x011F0 | `0x000201C3` | LIGHTING_CONFIG0, mask 0x2 |

No framebuffer, viewport, shader, attribute or draw commands are in the file.
HOME's code makes those at run time, and Kabegami makes them itself as the
example does.

## 7. Data formats in memory

### 7.1 float24 and the packed vector ports

float1.7.16 has the sign in bit 23, the exponent biased by 63 in bits 16-22
and 16 mantissa bits. The f32 → f24 conversion is
`exponent24 = exponent32 − 64`, `mantissa >> 7`. Exponent 0 is zero (no
denormals) and 0x7F infinity (`shader/isa.rs:287-310`). Examples: 1.0 =
`0x3F0000`, 0.5 = `0x3E0000`, −1.0 = `0xBF0000`, 120.0 = `0x45E000`.

A vector (x, y, z, w) goes into the float24 uniform port and the fixed
attribute registers as three words, w first (`shader/mod.rs:266-275`,
`lib.rs:1172-1180`, 3dbrew):

| Word | Bits | Holds |
|---|---|---|
| 0 | 8-31, 0-7 | w; bits 16-23 of z |
| 1 | 16-31, 0-15 | bits 0-15 of z; bits 8-23 of y |
| 2 | 24-31, 0-23 | bits 0-7 of y; x |

For example (1, 2, 3, 4) packs to `0x41000040`, `0x80004000`, `0x003F0000`.
In float32 mode (index bit 31) a vector is four f32 words in the order w, z,
y, x. Both were verified by the example: c0-c3 and c95 read back exactly from
`gpu.vertex_shader.float_uniforms`. The SHBIN constant table stores float24
values one per 32-bit slot, so pack them as above without converting.

### 7.2 The tiled colour buffer

| Property | Value | Source |
|---|---|---|
| Tiles | 8x8 pixels, in rows of width / 8 tiles; tile *t* at byte *t* · 64 · bpp | `format.rs:153-161` |
| Inside a tile | Morton order. Pixel (x, y) is at bit-interleave x0 y0 x1 y1 x2 y2, so the first row is 0, 1, 4, 5, 16, 17, 20, 21. | `format.rs:145-150`; test `morton_matches_known_values` `format.rs:191` |
| Rows | buffer row 0 is window row height − 1. Window y counts up from the bottom, so memory starts with the top of the picture. | `raster.rs:1915-1916`; verified by the example |
| RGBA8 pixel | bytes A, B, G, R (the little-endian word 0xRRGGBBAA) | `format.rs:58`, `93-98` |
| RGB8 pixel | bytes B, G, R | `format.rs:59` |
| RGB565, RGB5A1, RGBA4 | little-endian u16, red in the top bits | `format.rs:60-90` |
| Depth | D16 u16, D24 3 bytes, D24S8 u32 with the stencil in byte 3, 0..1 scaled to the full range | `raster.rs:1642-1666` |

`zakuro_gpu::format::morton_offset(x, y, width, bytes_per_pixel)` gives a
pixel's byte offset and `ColorFormat::decode` its RGBA.

### 7.3 Colour format numbers

| Value | Colour buffer (0x117 bits 16-18) | Display transfer (bits 8-10, 12-14), LCD (format bits 0-2) |
|---|---|---|
| 0 | RGBA8 | RGBA8 |
| 1 | RGB8 | RGB8 |
| 2 | RGB5A1 | RGB565 |
| 3 | RGB565 | RGB5A1 |
| 4 | RGBA4 | RGBA4 |

`format.rs:15-33`; test `color_buffers_have_their_own_numbers` `format.rs:169`.

### 7.4 Fills

`memory_fill` repeats `value.to_le_bytes()[..n]` over `start..end`, with
n = 2, 3 or 4 (`lib.rs:316-331`). An RGBA8 clear to (R, G, B, A) is therefore
`u32::from_be_bytes([R, G, B, A])`, 0xRRGGBBAA. The GSP's fill control word
gives the width in bits 8-9: 0 → 16-bit, 1 → 24-bit, else 32-bit
(`core/services/gsp.rs:420-426`). 3dbrew says 3 is 24-bit as well.

### 7.5 Display transfer flags

`display_transfer(input, output, input_dimensions, output_dimensions, flags)`,
dimensions as width | height << 16 (`lib.rs:357-389`):

| Flag bits | Meaning in zakuro-gpu | 3dbrew |
|---|---|---|
| 0 | flip: the input's rows are written from the output's last row up | flip vertically |
| 1 | the input is linear (and the output tiled unless bit 5). Clear: tiled in, linear out. | linear to tiled |
| 2 | crop. With the flip set, the output starts (input width − output width) · (output height − 1) · bpp bytes later (`lib.rs:391-401`). | needed when the output is narrower |
| 3 | not read | TextureCopy mode |
| 5 | keep the layout: tiled to tiled, or linear to linear when bit 1 is set too | tiled to tiled |
| 8-10, 12-14 | input, output format (section 7.3) | same |
| 16 | not read | 32x32 tiles |
| 24-25 | 1 averages 2x1 blocks, 2 averages 2x2; the output size counts input pixels | box downscale |

The copied rectangle is the smaller of the two sizes. The rest of the output
is kept: zakuro-gpu reads it first (`lib.rs:445-449`).

## 8. Reading the pixels back

The example checked that these are all the same picture (assertions in
`main.rs`):

1. **Untile by hand.** For y in 0..height and x in 0..width, decode the pixel
   at `morton_offset(x, y, width, bpp)`. Row 0 is the top. This image is the
   one in clip space, +x right and +y up, with no flip.
2. **`display_transfer` with flags 0** (tiled → linear, RGBA8 → RGBA8) gives
   exactly those rows, top first. Decode each 4-byte pixel (A B G R).
3. **Flags 1** gives the rows bottom first (OpenGL's `glReadPixels` order).

The console draws the top screen on its side. devkitPro's 3ds-examples
`graphics/gpu/simple_tri` renders into a 240x400 target with
`Mtx_OrthoTilt(&projection, 0.0, 400.0, 0.0, 240.0, ...)`. citro3d's
`source/maths/mtx_orthotilt.c` sets `r[0].y = 2/(top − bottom)` and
`r[1].x = 2/(left − right)`, "rotated τ/4 radians counterclockwise", that is
clip x = screen y and clip y = −screen x; the example puts that turn in its
vertices. The same example then display-transfers with
`GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | ... IN_FORMAT(RGBA8) |
OUT_FORMAT(RGB8)`, flags `0x1000`: tiled to linear RGB8, no flip. The LCD
scans each 720-byte row as one screen column, from the left, starting at the
bottom (`core/lib.rs:1092-1134`; 3dbrew: "held normally, the first pixel
appears at the bottom-left"). So:

* upright screen pixel (sx, sy), 400x240 with (0, 0) at the top left, is LCD
  byte `sx · 720 + (239 − sy) · 3`;
* that is untiled buffer pixel (column 239 − sy, row sx), so the upright
  picture is the untiled 240x400 buffer **turned 90° counter-clockwise**:
  `screen[sy][sx] = buffer[sx][239 − sy]` (verified by the example, both
  ways).

Kabegami can draw the console's way, with HOME's camera and a 240x400 buffer,
then turn the picture. Or it can leave the quarter turn out of the projection
and draw into a 400x240 buffer, which reads back upright with no turn.
Whether HOME's camera and projection contain the turn belongs to the camera
note.

## 9. How the runtime drives it (zakuro-core)

* The GSP's GX command queue (`core/services/gsp.rs:509-546`, entries of 0x20
  bytes) calls the GPU (`gsp.rs:548-623`):
  * ProcessCommandList (0x01) takes the address & !7 and the size & !3 and
    calls `System::submit_command_list` → `Gpu::process_command_list`
    (`gsp.rs:566-571`, `core/lib.rs:850-857`).
  * MemoryFill (0x02) has two banks; the width comes from the control word
    (`gsp.rs:573-593`).
  * DisplayTransfer (0x03) and TextureCopy (0x04) map straight onto the
    methods (`gsp.rs:595-616`).
  * The immediate service calls SetCommandList, SetDisplayTransfer,
    SetTextureCopy and SetMemoryFill do the same (`gsp.rs:172-264`).
* Every address a title gives the GSP goes through `virtual_to_physical`
  first (`gsp.rs:626-639`). `GuestMemory::translate` maps the GPU's physical
  addresses back (`core/lib.rs:1162-1217`).
* SetBufferSwap → `Gpu::set_framebuffer` sets the LCD's framebuffer address,
  stride and format (`gsp.rs:154-169`). `System::read_screen_at` scans it out
  as in section 8, and zakuro-gpu never does that itself. A host program does
  the scan-out on its own, as the example does.
* `System.renderer` is a `Box<dyn Renderer>` holding a `SoftwareRenderer`
  (`core/lib.rs:163`, `269`). The Vulkan path (`enable_hardware_renderer`,
  feature `vulkan`) is not needed here.

## 10. The example

Location (scratch, not in the Kabegami repo):
`/tmp/claude-1000/-home-fearkov-Desktop-zakuro/9246e26f-7645-4fb9-b281-cd09c25c43f8/scratchpad/kabegami-explore/zakuro-gpu-recipe/example/`,
with `Cargo.toml`, `Cargo.lock` (copied from Zakuro's, so the build is
offline with the same versions) and `src/main.rs`.

What it does:

* builds a 216-word command list with registers from every group above
  (the texture units are only switched off);
* draws three u16-indexed vertices with DRAWELEMENTS. Attribute 0 is four
  floats from an array; attribute 1 is a fixed attribute that the TEV leaves
  unread;
* runs `mov o0, v0; mov o1, v1; end`, with TEV stage 0 replacing colour and
  alpha with the constant `0xFF2040E0` (RGBA E0 40 20 FF);
* clears with `memory_fill` to (20 30 40 FF) and uploads float24 and float32
  uniforms, an integer and a bool uniform, all checked back on the shader
  unit;
* reads the 240x400 buffer back three ways (section 8) and asserts they agree;
* asserts the triangle sits bottom-left on the upright screen: probe pixels,
  26970 covered pixels against the 27000 expected, no third colour;
* writes `buffer.png` (240x400, as the GPU holds it) and `screen.png`
  (400x240, as the screen shows it).

Build and run (cargo only inside a memory-capped scope; `CARGO_NET_OFFLINE`
keeps it on the cached crates):

```sh
cd .../kabegami-explore/zakuro-gpu-recipe/example
env CARGO_TARGET_DIR=.../kabegami-explore/zakuro-gpu-recipe/target CARGO_NET_OFFLINE=true \
  systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=0 cargo build --release -j 4
env CARGO_TARGET_DIR=... CARGO_NET_OFFLINE=true \
  systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=0 cargo run --release -j 4 -- <output dir>
# the same with --features jit for compiled shaders
```

Results, 2026-10-25, on this machine:

| Build | Build time from cold | Output | buffer.png SHA-256 | screen.png SHA-256 |
|---|---|---|---|---|
| default features off (interpreter) | 14.1 s | `216 command words, one draw in 1.03ms (interpreter), 26970 pixels of [E0, 40, 20, FF], background [20, 30, 40, FF]` | `677ee92ed6f594824a60ac8c2a71ef3bd3e018484fa5afafd029dc68dae7f619` | `60c23228bf63318d6f16955d9464dab081e424e3bef2d6a42d65bafe6c4b62a3` |
| `--features jit` | 49.1 s | the same, `1.14ms (jit)` | same | same |

zakuro-gpu warns about dead code without the `vulkan` feature (14 warnings).
They are harmless.

### 10.1 main.rs

```rust
//! zakuro-gpu as a library, outside the emulator: one triangle, drawn by the
//! software rasterizer from a hand-written pass-through vertex shader and a
//! constant-colour TEV stage into a tiled RGBA8 colour buffer, read back as
//! RGBA8 by untiling it and through display transfers, and saved as PNG.
//!
//! usage: zakuro-gpu-recipe [output directory, "." when left out]

use std::error::Error;
use std::path::Path;
use std::time::Instant;

use zakuro_gpu::format::{morton_offset, ColorFormat};
use zakuro_gpu::{Gpu, GpuMemory, SoftwareRenderer};

// -- where everything lives, as physical addresses in VRAM -------------------

const VRAM: u32 = 0x1800_0000;
const VRAM_SIZE: usize = 0x60_0000;
/// the colour buffer is 240x400 like the top screen's: the console draws the
/// screen on its side, 240 pixels a row, 400 rows.
const WIDTH: u32 = 240;
const HEIGHT: u32 = 400;
const COLOR: u32 = VRAM;
const DEPTH: u32 = VRAM + 0x10_0000;
/// what the display transfers write: linear RGBA8, the same flipped, and
/// the RGB8 framebuffer the top LCD scans.
const LINEAR: u32 = VRAM + 0x20_0000;
const FLIPPED: u32 = VRAM + 0x28_0000;
const LCD: u32 = VRAM + 0x30_0000;
/// the vertex array, the index buffer after it, the command list.
const VERTICES: u32 = VRAM + 0x40_0000;
const INDICES: u32 = VERTICES + 0x100;
const COMMANDS: u32 = VRAM + 0x50_0000;

// -- the PICA200 registers used, by their 3dbrew names -----------------------

const GPUREG_FINALIZE: u16 = 0x0010;
const GPUREG_FACECULLING_CONFIG: u16 = 0x0040;
const GPUREG_VIEWPORT_WIDTH: u16 = 0x0041;
const GPUREG_VIEWPORT_HEIGHT: u16 = 0x0043;
const GPUREG_DEPTHMAP_SCALE: u16 = 0x004D;
const GPUREG_DEPTHMAP_OFFSET: u16 = 0x004E;
const GPUREG_SH_OUTMAP_TOTAL: u16 = 0x004F;
const GPUREG_SH_OUTMAP_O0: u16 = 0x0050;
const GPUREG_SH_OUTATTR_MODE: u16 = 0x0064;
const GPUREG_SCISSORTEST_MODE: u16 = 0x0065;
const GPUREG_VIEWPORT_XY: u16 = 0x0068;
const GPUREG_DEPTHMAP_ENABLE: u16 = 0x006D;
const GPUREG_RENDERBUF_DIM: u16 = 0x006E;
const GPUREG_SH_OUTATTR_CLOCK: u16 = 0x006F;
const GPUREG_TEXUNIT_CONFIG: u16 = 0x0080;
/// the six TEV stages, five registers each: source, operand, combiner,
/// constant colour, scale.
const GPUREG_TEXENV: [u16; 6] = [0x00C0, 0x00C8, 0x00D0, 0x00D8, 0x00F0, 0x00F8];
const GPUREG_TEXENV_UPDATE_BUFFER: u16 = 0x00E0;
const GPUREG_TEXENV_BUFFER_COLOR: u16 = 0x00FD;
const GPUREG_COLOR_OPERATION: u16 = 0x0100;
const GPUREG_LOGIC_OP: u16 = 0x0102;
const GPUREG_FRAGOP_ALPHA_TEST: u16 = 0x0104;
const GPUREG_STENCIL_TEST: u16 = 0x0105;
const GPUREG_DEPTH_COLOR_MASK: u16 = 0x0107;
const GPUREG_FRAMEBUFFER_INVALIDATE: u16 = 0x0110;
const GPUREG_FRAMEBUFFER_FLUSH: u16 = 0x0111;
const GPUREG_COLORBUFFER_READ: u16 = 0x0112;
const GPUREG_COLORBUFFER_WRITE: u16 = 0x0113;
const GPUREG_DEPTHBUFFER_READ: u16 = 0x0114;
const GPUREG_DEPTHBUFFER_WRITE: u16 = 0x0115;
const GPUREG_DEPTHBUFFER_FORMAT: u16 = 0x0116;
const GPUREG_COLORBUFFER_FORMAT: u16 = 0x0117;
const GPUREG_FRAMEBUFFER_BLOCK32: u16 = 0x011B;
const GPUREG_DEPTHBUFFER_LOC: u16 = 0x011C;
const GPUREG_COLORBUFFER_LOC: u16 = 0x011D;
const GPUREG_FRAMEBUFFER_DIM: u16 = 0x011E;
const GPUREG_ATTRIBBUFFERS_LOC: u16 = 0x0200;
const GPUREG_ATTRIBBUFFERS_FORMAT_LOW: u16 = 0x0201;
const GPUREG_ATTRIBBUFFERS_FORMAT_HIGH: u16 = 0x0202;
/// twelve loaders from here, three registers each: offset, config 1 and 2.
const GPUREG_ATTRIBBUFFER0_OFFSET: u16 = 0x0203;
const GPUREG_INDEXBUFFER_CONFIG: u16 = 0x0227;
const GPUREG_NUMVERTICES: u16 = 0x0228;
const GPUREG_GEOSTAGE_CONFIG: u16 = 0x0229;
const GPUREG_VERTEX_OFFSET: u16 = 0x022A;
const GPUREG_DRAWELEMENTS: u16 = 0x022F;
const GPUREG_VTX_FUNC: u16 = 0x0231;
const GPUREG_FIXEDATTRIB_INDEX: u16 = 0x0232;
const GPUREG_VSH_NUM_ATTR: u16 = 0x0242;
const GPUREG_VSH_COM_MODE: u16 = 0x0244;
const GPUREG_START_DRAW_FUNC0: u16 = 0x0245;
const GPUREG_VSH_OUTMAP_TOTAL1: u16 = 0x024A;
const GPUREG_VSH_OUTMAP_TOTAL2: u16 = 0x0251;
const GPUREG_GEOSTAGE_CONFIG2: u16 = 0x0253;
const GPUREG_PRIMITIVE_CONFIG: u16 = 0x025E;
const GPUREG_RESTART_PRIMITIVE: u16 = 0x025F;
const GPUREG_VSH_BOOLUNIFORM: u16 = 0x02B0;
const GPUREG_VSH_INTUNIFORM_I0: u16 = 0x02B1;
const GPUREG_VSH_INPUTBUFFER_CONFIG: u16 = 0x02B9;
const GPUREG_VSH_ENTRYPOINT: u16 = 0x02BA;
const GPUREG_VSH_ATTRIBUTES_PERMUTATION_LOW: u16 = 0x02BB;
const GPUREG_VSH_ATTRIBUTES_PERMUTATION_HIGH: u16 = 0x02BC;
const GPUREG_VSH_OUTMAP_MASK: u16 = 0x02BD;
const GPUREG_VSH_CODETRANSFER_END: u16 = 0x02BF;
const GPUREG_VSH_FLOATUNIFORM_INDEX: u16 = 0x02C0;
const GPUREG_VSH_FLOATUNIFORM_DATA0: u16 = 0x02C1;
const GPUREG_VSH_CODETRANSFER_INDEX: u16 = 0x02CB;
const GPUREG_VSH_CODETRANSFER_DATA0: u16 = 0x02CC;
const GPUREG_VSH_OPDESCS_INDEX: u16 = 0x02D5;
const GPUREG_VSH_OPDESCS_DATA0: u16 = 0x02D6;

// -- the vertex shader, assembled by hand ------------------------------------

/// mov dst, src with operand descriptor 0. format 1: the opcode in bits
/// 26-31, the destination in 21-25 (o0-o15 are 0x00-0x0F), the first source
/// in 12-18 (v0-v15 0x00-0x0F, r0-r15 0x10-0x1F, c0-c95 0x20-0x7F), the
/// descriptor in 0-6.
const fn mov(dst: u32, src: u32) -> u32 {
    0x13 << 26 | dst << 21 | src << 12
}
const END: u32 = 0x22 << 26;
/// o0 = v0, the position, o1 = v1, the colour, and done.
const PROGRAM: [u32; 3] = [mov(0x00, 0x00), mov(0x01, 0x01), END];
/// operand descriptor 0: write x, y, z and w (bits 0-3, x the highest), each
/// source neither negated nor swizzled (x y z w, 0b00_01_10_11).
const XYZW: u32 = 0xF | 0x1B << 5 | 0x1B << 14 | 0x1B << 23;

/// uniforms the program never reads, sent to see how they land: c0-c3 in
/// float24, values float24 holds exactly, and c95 in float32.
const MATRIX: [[f32; 4]; 4] = [[1.0, 2.0, 3.0, 4.0], [-0.5, 0.25, 0.0, 8.0], [0.125, -1.0, 16.0, 0.0], [0.0, 0.0, -1.0, 1.0]];
const C95: [f32; 4] = [0.1, 0.2, 0.3, 0.4];

/// the colours, RGBA: the TEV's constant, what the buffer is cleared to,
/// and the vertex colour, a fixed attribute the TEV leaves unread.
const TEV_COLOR: [u8; 4] = [0xE0, 0x40, 0x20, 0xFF];
const CLEAR: [u8; 4] = [0x20, 0x30, 0x40, 0xFF];
const VERTEX_COLOR: [f32; 4] = [0.0, 1.0, 0.0, 1.0];

// -- guest memory --------------------------------------------------------------

/// a window of the PICA's physical address space, backed by a host buffer.
/// registers and the GX engines carry physical addresses, and the trait's
/// identity translate keeps them, so the window is addressed by them.
/// reads outside it give zeros, writes outside it are dropped.
struct HostMemory {
    base: u32,
    bytes: Vec<u8>,
}

impl HostMemory {
    fn range(&self, addr: u32, len: usize) -> Option<std::ops::Range<usize>> {
        let start = addr.checked_sub(self.base)? as usize;
        let end = start.checked_add(len)?;
        (end <= self.bytes.len()).then_some(start..end)
    }

    fn bytes(&self, addr: u32, len: usize) -> &[u8] {
        &self.bytes[self.range(addr, len).expect("inside the window")]
    }
}

impl GpuMemory for HostMemory {
    fn read(&mut self, addr: u32, out: &mut [u8]) {
        match self.range(addr, out.len()) {
            Some(range) => out.copy_from_slice(&self.bytes[range]),
            // across an end of the window, a byte at a time
            None => {
                for (i, byte) in out.iter_mut().enumerate() {
                    *byte = self.range(addr.wrapping_add(i as u32), 1).map_or(0, |at| self.bytes[at.start]);
                }
            }
        }
    }

    fn write(&mut self, addr: u32, data: &[u8]) {
        match self.range(addr, data.len()) {
            Some(range) => self.bytes[range].copy_from_slice(data),
            None => {
                for (i, &byte) in data.iter().enumerate() {
                    if let Some(at) = self.range(addr.wrapping_add(i as u32), 1) {
                        self.bytes[at.start] = byte;
                    }
                }
            }
        }
    }

    // the bytes in place, which the list reader, the fills and the vertex
    // fetch take instead of a copy
    fn slice(&mut self, addr: u32, len: usize) -> Option<&[u8]> {
        let range = self.range(addr, len)?;
        Some(&self.bytes[range])
    }

    fn slice_mut(&mut self, addr: u32, len: usize) -> Option<&mut [u8]> {
        let range = self.range(addr, len)?;
        Some(&mut self.bytes[range])
    }
}

// -- command lists ---------------------------------------------------------

/// a PICA200 command list. each command is a parameter word, then a header
/// (the register in bits 0-15, the byte mask in 16-19, how many more
/// parameters follow in 20-27, consecutive in 31), the other parameters,
/// and a word of padding after an odd number of them.
#[derive(Default)]
struct List(Vec<u32>);

impl List {
    /// one register, all four bytes.
    fn set(&mut self, register: u16, value: u32) -> &mut List {
        self.masked(register, 0xF, value)
    }

    /// the bytes of one register a mask names, bit 0 for bits 0-7 and on.
    fn masked(&mut self, register: u16, mask: u32, value: u32) -> &mut List {
        self.0.extend([value, register as u32 | (mask & 0xF) << 16]);
        self
    }

    /// values for the registers from register on, consecutive, or else all
    /// for register, a port such as the shader code or the uniform data.
    fn burst(&mut self, register: u16, consecutive: bool, values: &[u32]) -> &mut List {
        // a header carries at most 255 more parameters
        for (i, chunk) in values.chunks(256).enumerate() {
            let target = register as u32 + if consecutive { i as u32 * 256 } else { 0 };
            let header = target | 0xF << 16 | (chunk.len() as u32 - 1) << 20 | (consecutive as u32) << 31;
            self.0.extend([chunk[0], header]);
            self.0.extend(&chunk[1..]);
            if self.0.len() % 2 == 1 {
                self.0.push(0);
            }
        }
        self
    }

    /// ends the list as the console needs it, GPUREG_FINALIZE in its last
    /// eight bytes and its size a multiple of 16, and gives its bytes.
    fn finish(&mut self) -> Vec<u8> {
        if self.0.len() % 4 == 0 {
            // a write of no bytes, to come out at 16
            self.masked(0x0000, 0, 0);
        }
        self.set(GPUREG_FINALIZE, 0x1234_5678);
        self.0.iter().flat_map(|word| word.to_le_bytes()).collect()
    }
}

/// float1.7.16, the PICA's 24-bit float: the sign in bit 23, the exponent
/// biased by 63 in bits 16-22, the top 16 bits of the mantissa.
fn f24(value: f32) -> u32 {
    let bits = value.to_bits();
    let sign = bits >> 31 << 23;
    match ((bits >> 23) & 0xFF) as i32 - 64 {
        // zero, or too small for the exponent
        ..=0 => sign,
        // too big, infinity
        0x7F.. => sign | 0x7F << 16,
        exponent => sign | (exponent as u32) << 16 | (bits >> 7) & 0xFFFF,
    }
}

/// x, y, z and w as the three words the float24 ports take, the uniforms'
/// and the fixed attributes': w and the top of z first, x last.
fn pack24(vector: [f32; 4]) -> [u32; 3] {
    let [x, y, z, w] = vector.map(f24);
    [w << 8 | z >> 16, (z & 0xFFFF) << 16 | y >> 8, (y & 0xFF) << 24 | x]
}

// -- reading the pictures back ---------------------------------------------

/// a tiled buffer as RGBA8, rows top first: tiles of 8x8 pixels in rows,
/// each tile's pixels in Morton order. the rasterizer writes window row y,
/// which counts up from the bottom, to buffer row height - 1 - y, so the
/// first row in memory is the top of the picture.
fn untile(memory: &HostMemory, addr: u32, width: u32, height: u32, format: ColorFormat) -> Vec<u8> {
    let bpp = format.bytes_per_pixel();
    let bytes = memory.bytes(addr, (width * height) as usize * bpp);
    let mut rgba = Vec::with_capacity((width * height * 4) as usize);
    for y in 0..height {
        for x in 0..width {
            let at = morton_offset(x, y, width, bpp as u32) as usize;
            // an RGBA8 pixel is stored A, B, G, R, decode puts it in order
            rgba.extend(format.decode(&bytes[at..at + bpp]));
        }
    }
    rgba
}

/// a linear buffer as RGBA8.
fn decode(bytes: &[u8], format: ColorFormat) -> Vec<u8> {
    bytes.chunks_exact(format.bytes_per_pixel()).flat_map(|pixel| format.decode(pixel)).collect()
}

/// the top screen as one sees it, 400x240 upright, from the 240x400 RGB8
/// framebuffer the LCD scans: each 720-byte row is a column of the screen,
/// from the left, and starts at the bottom of it (what zakuro-core's
/// read_screen does).
fn scan_out(lcd: &[u8]) -> Vec<u8> {
    let (columns, rows, stride) = (HEIGHT as usize, WIDTH as usize, WIDTH as usize * 3);
    let mut rgba = Vec::with_capacity(columns * rows * 4);
    for y in 0..rows {
        for x in 0..columns {
            let at = x * stride + (rows - 1 - y) * 3;
            rgba.extend(ColorFormat::Rgb8.decode(&lcd[at..at + 3]));
        }
    }
    rgba
}

fn save_png(path: &Path, width: u32, height: u32, rgba: &[u8]) -> Result<(), Box<dyn Error>> {
    let mut encoder = png::Encoder::new(std::io::BufWriter::new(std::fs::File::create(path)?), width, height);
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header()?;
    writer.write_image_data(rgba)?;
    writer.finish()?;
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    let out = std::path::PathBuf::from(std::env::args().nth(1).unwrap_or_else(|| ".".into()));
    std::fs::create_dir_all(&out)?;

    let mut memory = HostMemory { base: VRAM, bytes: vec![0; VRAM_SIZE] };
    let mut gpu = Gpu::new();
    // Gpu rasterizes the draws itself, the renderer only hears of them
    let mut renderer = SoftwareRenderer::default();

    // the triangle as the screen shows it, x right and y up, its right angle
    // at the bottom left, turned a quarter for the GPU, which draws the
    // screen on its side: clip x = y, clip y = -x. the shader passes it on,
    // with z in [-w, 0] and w above 0, the volume the PICA draws.
    let corners = [[-0.75f32, -0.75], [0.75, -0.75], [-0.75, 0.75]];
    let vertices: Vec<u8> = corners.iter().flat_map(|&[x, y]| [y, -x, -0.5, 1.0]).flat_map(f32::to_le_bytes).collect();
    memory.write(VERTICES, &vertices);
    memory.write(INDICES, &[0u16, 1, 2].map(u16::to_le_bytes).concat());

    // the clear, a fill with a 32-bit pattern as memory holds it: an RGBA8
    // pixel is A B G R in memory, the little-endian word 0xRRGGBBAA
    gpu.memory_fill(&mut memory, COLOR, COLOR + WIDTH * HEIGHT * 4, u32::from_be_bytes(CLEAR), 4);

    let float_uniforms: Vec<u32> = MATRIX.iter().flat_map(|&row| pack24(row)).collect();
    let float32_uniform: Vec<u32> = C95.iter().rev().map(|c| c.to_bits()).collect();
    let mut list = List::default();
    list
        // the colour buffer, 240x400 RGBA8 in 8x8 tiles, the depth buffer
        // unused. the console wants the invalidate, flush, renderbuffer size
        // and block mode too, zakuro-gpu only stores them
        .set(GPUREG_FRAMEBUFFER_INVALIDATE, 1)
        .set(GPUREG_COLORBUFFER_FORMAT, 0 << 16 | 2) // RGBA8 in 16-18, 32-bit pixels in 0-1
        .set(GPUREG_COLORBUFFER_LOC, COLOR >> 3)
        .set(GPUREG_DEPTHBUFFER_FORMAT, 3) // D24S8
        .set(GPUREG_DEPTHBUFFER_LOC, DEPTH >> 3)
        .set(GPUREG_FRAMEBUFFER_DIM, WIDTH | (HEIGHT - 1) << 12 | 1 << 24)
        .set(GPUREG_RENDERBUF_DIM, WIDTH | (HEIGHT - 1) << 12 | 1 << 24)
        .set(GPUREG_FRAMEBUFFER_BLOCK32, 0)
        .set(GPUREG_COLORBUFFER_READ, 0xF)
        .set(GPUREG_COLORBUFFER_WRITE, 0xF)
        .set(GPUREG_DEPTHBUFFER_READ, 0)
        .set(GPUREG_DEPTHBUFFER_WRITE, 0)
        // the viewport over the whole buffer, half its size in float24, its
        // corner in two 10-bit fields. zakuro-gpu has no scissor, drawing
        // stays inside the viewport
        .set(GPUREG_VIEWPORT_WIDTH, f24(WIDTH as f32 / 2.0))
        .set(GPUREG_VIEWPORT_HEIGHT, f24(HEIGHT as f32 / 2.0))
        .set(GPUREG_VIEWPORT_XY, 0)
        .set(GPUREG_SCISSORTEST_MODE, 0)
        .set(GPUREG_FACECULLING_CONFIG, 0)
        .set(GPUREG_DEPTHMAP_ENABLE, 1)
        .set(GPUREG_DEPTHMAP_SCALE, f24(-1.0))
        .set(GPUREG_DEPTHMAP_OFFSET, f24(0.0))
        // the output merger: logic op COPY (0, what the registers start at,
        // is CLEAR), all four channels written, no alpha, stencil or depth test
        .set(GPUREG_COLOR_OPERATION, 0x00E4_0000)
        .set(GPUREG_LOGIC_OP, 3)
        .set(GPUREG_FRAGOP_ALPHA_TEST, 0)
        .set(GPUREG_STENCIL_TEST, 0)
        .set(GPUREG_DEPTH_COLOR_MASK, 0xF << 8)
        // no texture units; TEV stage 0 replaces colour and alpha with its
        // constant (source 0xE), the other five pass the previous one on (0xF)
        .set(GPUREG_TEXUNIT_CONFIG, 1 << 12)
        .burst(GPUREG_TEXENV[0], true, &[0x000E_000E, 0, 0, u32::from_le_bytes(TEV_COLOR), 0]);
    for &stage in &GPUREG_TEXENV[1..] {
        list.burst(stage, true, &[0x000F_000F, 0, 0, 0, 0]);
    }
    list.set(GPUREG_TEXENV_UPDATE_BUFFER, 0) // no fog, no combiner buffer
        .set(GPUREG_TEXENV_BUFFER_COLOR, 0)
        // the vertex shader: code and operand descriptors through their
        // ports, each from an index, then where it starts and what it reads
        // and writes. no geometry shader
        .set(GPUREG_GEOSTAGE_CONFIG, 0)
        .set(GPUREG_VSH_COM_MODE, 0)
        .set(GPUREG_VSH_CODETRANSFER_INDEX, 0)
        .burst(GPUREG_VSH_CODETRANSFER_DATA0, false, &PROGRAM)
        .set(GPUREG_VSH_CODETRANSFER_END, 1)
        .set(GPUREG_VSH_OPDESCS_INDEX, 0)
        .burst(GPUREG_VSH_OPDESCS_DATA0, false, &[XYZW])
        .set(GPUREG_VSH_ENTRYPOINT, 0x7FFF_0000) // from word 0
        .set(GPUREG_VSH_INPUTBUFFER_CONFIG, 0xA000_0000 | (2 - 1)) // two inputs
        .set(GPUREG_VSH_NUM_ATTR, 2 - 1)
        .set(GPUREG_VSH_ATTRIBUTES_PERMUTATION_LOW, 0x10) // attribute 0 into v0, 1 into v1
        .set(GPUREG_VSH_ATTRIBUTES_PERMUTATION_HIGH, 0)
        .set(GPUREG_VSH_OUTMAP_MASK, 0b11) // o0 and o1
        .set(GPUREG_VSH_OUTMAP_TOTAL1, 2 - 1)
        .set(GPUREG_VSH_OUTMAP_TOTAL2, 2 - 1)
        // what each output component carries: o0 the position x y z w
        // (semantics 0-3), o1 the colour r g b a (8-11), 0x1F for none
        .set(GPUREG_SH_OUTMAP_TOTAL, 2)
        .burst(GPUREG_SH_OUTMAP_O0, true, &[0x0302_0100, 0x0B0A_0908, 0x1F1F_1F1F, 0x1F1F_1F1F, 0x1F1F_1F1F, 0x1F1F_1F1F, 0x1F1F_1F1F])
        .set(GPUREG_SH_OUTATTR_MODE, 0)
        .set(GPUREG_SH_OUTATTR_CLOCK, 0b11) // position z, colour
        // uniforms, which this program does not read: from c0 in float24,
        // three words a vector, then c95 in float32 (bit 31), w z y x
        .set(GPUREG_VSH_FLOATUNIFORM_INDEX, 0)
        .burst(GPUREG_VSH_FLOATUNIFORM_DATA0, false, &float_uniforms)
        .set(GPUREG_VSH_FLOATUNIFORM_INDEX, 1 << 31 | 95)
        .burst(GPUREG_VSH_FLOATUNIFORM_DATA0, false, &float32_uniform)
        .set(GPUREG_VSH_INTUNIFORM_I0, 3 | 1 << 8 | 2 << 16) // i0: x 3, y 1, z 2
        .set(GPUREG_VSH_BOOLUNIFORM, 0x7FFF_0000 | 0b101) // b0 and b2
        // the vertex attributes: 0 from the array, four floats (type 3, four
        // components) that loader 0 reads 16 bytes a vertex at offset 0, and
        // 1 a fixed attribute (bit 16 + 1), two in all (bits 28-31)
        .set(GPUREG_ATTRIBBUFFERS_LOC, VERTICES >> 3)
        .set(GPUREG_ATTRIBBUFFERS_FORMAT_LOW, 3 | (4 - 1) << 2)
        .set(GPUREG_ATTRIBBUFFERS_FORMAT_HIGH, 1 << (16 + 1) | (2 - 1) << 28)
        .burst(GPUREG_ATTRIBBUFFER0_OFFSET, true, &[0, 0, 16 << 16 | 1 << 28])
        .burst(GPUREG_FIXEDATTRIB_INDEX, true, &[1, pack24(VERTEX_COLOR)[0], pack24(VERTEX_COLOR)[1], pack24(VERTEX_COLOR)[2]])
        // the draw, as citro3d's C3D_DrawElements sends it: triangles from
        // indices go as geometry primitives (mode 3) with the element mode
        // bits set, between entering and leaving drawing mode. zakuro-gpu
        // reads the mode, the index buffer, the count and the trigger
        .set(GPUREG_PRIMITIVE_CONFIG, 2 - 1) // outputs - 1
        .masked(GPUREG_PRIMITIVE_CONFIG, 0b0010, 3 << 8)
        .set(GPUREG_RESTART_PRIMITIVE, 1)
        .set(GPUREG_INDEXBUFFER_CONFIG, (INDICES - VERTICES) | 1 << 31) // u16 indices
        .set(GPUREG_NUMVERTICES, 3)
        .set(GPUREG_VERTEX_OFFSET, 0)
        .masked(GPUREG_GEOSTAGE_CONFIG, 0b0010, 1 << 8)
        .masked(GPUREG_GEOSTAGE_CONFIG2, 0b0010, 1 << 8)
        .masked(GPUREG_START_DRAW_FUNC0, 0b0001, 0)
        .set(GPUREG_DRAWELEMENTS, 1)
        .masked(GPUREG_START_DRAW_FUNC0, 0b0001, 1)
        .masked(GPUREG_GEOSTAGE_CONFIG, 0b0010, 0)
        .masked(GPUREG_GEOSTAGE_CONFIG2, 0b0010, 0)
        .set(GPUREG_VTX_FUNC, 1)
        .set(GPUREG_FRAMEBUFFER_FLUSH, 1);
    let commands = list.finish();
    memory.write(COMMANDS, &commands);

    let start = Instant::now();
    gpu.process_command_list(&mut memory, &mut renderer, COMMANDS, commands.len() as u32);
    let drawn = start.elapsed();
    assert_eq!((gpu.draw_calls, gpu.vertices_drawn, renderer.draws), (1, 3, 1), "one draw of three vertices");

    // what the ports made of the words, as the shader unit holds it
    let unit = &gpu.vertex_shader;
    assert_eq!(unit.program[..3], PROGRAM);
    assert_eq!(unit.descriptors[0], XYZW);
    assert_eq!(unit.entry_point, 0);
    assert_eq!(unit.float_uniforms[..4], MATRIX, "float24 uniforms land x y z w, c0 first");
    assert_eq!(unit.float_uniforms[95], C95, "float32 uniforms go w z y x");
    assert_eq!(unit.int_uniforms[0], [3, 1, 2, 0]);
    assert_eq!(unit.bool_uniforms, 0b101);

    // 1. the colour buffer untiled by hand
    let buffer = untile(&memory, COLOR, WIDTH, HEIGHT, ColorFormat::Rgba8);

    // 2. the transfer engine, tiled to linear (flag bit 1 clear), RGBA8 in
    // (bits 8-10) and out (bits 12-14): the same rows, top first
    let dimensions = WIDTH | HEIGHT << 16;
    gpu.display_transfer(&mut memory, COLOR, LINEAR, dimensions, dimensions, 0);
    let linear = decode(memory.bytes(LINEAR, (WIDTH * HEIGHT * 4) as usize), ColorFormat::Rgba8);
    assert!(linear == buffer, "a tiled-to-linear transfer is the buffer untiled");
    // flag bit 0 flips it, the bottom row first
    gpu.display_transfer(&mut memory, COLOR, FLIPPED, dimensions, dimensions, 1);
    let flipped = decode(memory.bytes(FLIPPED, (WIDTH * HEIGHT * 4) as usize), ColorFormat::Rgba8);
    let row = (WIDTH * 4) as usize;
    assert!(flipped.chunks(row).eq(buffer.chunks(row).rev()), "a flipped transfer is the buffer upside down");

    // 3. the top screen the way the console shows it: RGB8 out (1 in bits
    // 12-14), no flip, the LCD scanning the result a column at a time
    gpu.display_transfer(&mut memory, COLOR, LCD, dimensions, dimensions, 1 << 12);
    let screen = scan_out(memory.bytes(LCD, (WIDTH * HEIGHT * 3) as usize));
    // which is the untiled buffer turned a quarter turn counter-clockwise
    let mut turned = Vec::with_capacity(screen.len());
    for y in 0..WIDTH {
        for x in 0..HEIGHT {
            let at = ((x * WIDTH + (WIDTH - 1 - y)) * 4) as usize;
            turned.extend_from_slice(&buffer[at..at + 4]);
        }
    }
    assert!(screen == turned, "the screen is the buffer turned counter-clockwise");

    // the triangle where it was put: bottom left on the screen, its pixels
    // the TEV's constant, the rest the clear colour
    let pixel = |x: u32, y: u32| -> [u8; 4] {
        let at = ((y * HEIGHT + x) * 4) as usize;
        screen[at..at + 4].try_into().unwrap()
    };
    for (x, y, expected) in [(60, 200, TEV_COLOR), (150, 150, TEV_COLOR), (340, 40, CLEAR), (250, 90, CLEAR), (20, 20, CLEAR)] {
        assert_eq!(pixel(x, y), expected, "screen pixel ({x}, {y})");
    }
    let covered = screen.chunks(4).filter(|p| *p == TEV_COLOR).count();
    let others = screen.chunks(4).filter(|p| *p != TEV_COLOR && *p != CLEAR).count();
    // half of 300 by 180 pixels
    let area = 0.5 * (1.5 * 200.0) * (1.5 * 120.0);
    assert!((covered as f64 - area).abs() < area * 0.02 && others == 0, "{covered} pixels covered, {others} of other colours");

    save_png(&out.join("buffer.png"), WIDTH, HEIGHT, &buffer)?;
    save_png(&out.join("screen.png"), HEIGHT, WIDTH, &screen)?;
    println!(
        "{} command words, one draw in {drawn:.2?} ({}), {covered} pixels of {:02X?}, background {:02X?}",
        commands.len() / 4,
        if cfg!(feature = "jit") { "jit" } else { "interpreter" },
        TEV_COLOR,
        CLEAR,
    );
    println!("wrote {} and {}", out.join("buffer.png").display(), out.join("screen.png").display());
    Ok(())
}
```

### 10.2 Cargo.toml

```toml
[package]
name = "zakuro-gpu-recipe"
version = "0.1.0"
edition = "2021"
license = "MIT"
publish = false

[dependencies]
# without its default "jit" feature the vertex shaders are interpreted, and
# Cranelift stays out of the build
zakuro-gpu = { path = "/home/fearkov/Desktop/zakuro/crates/zakuro-gpu", default-features = false }
# already in zakuro-gpu's tree, for the pictures
png = "0.18.1"

[features]
# shaders compiled to host code, as the emulator runs them
jit = ["zakuro-gpu/jit"]

# a project of its own, not a member of a workspace above it
[workspace]
```

## 11. Gotchas

1. **LOGIC_OP starts at 0, which is CLEAR.** With blending off, every
   fragment then writes 0 (`blend.rs:225-230`). Set 0x102 = 3, or turn
   blending on (0x100 bit 8) with BLEND_FUNC one/zero.
2. **Nothing is written until COLORBUFFER_WRITE (0x113) is non-zero and
   DEPTH_COLOR_MASK (0x107) bits 8-11 are set.** Both start at 0.
3. The viewport registers hold **half** the size, in float24; 0 drops the draw.
4. There is **no scissor**. The viewport, cut to the buffer, bounds the draw.
5. **0x2B9 bits 0-3 decide how many attributes the permutation places.** At
   0, only attribute 0 reaches the shader. 0x202 bits 28-31 are not read.
6. **The output map counts enabled output registers.** Entry *i* is the
   *i*-th register set in 0x2BD, not o*i*. SH_OUTMAP_TOTAL is a count, not
   minus one; at 0, or with no position semantic, the default map applies.
7. Clip-space z has to be in [−w, 0] with w > 0, the PICA's range, not
   OpenGL's [−w, w].
8. **Face culling 1 drops counter-clockwise triangles.** BannerBG's material
   sets 1, so its triangles have to be clockwise in window space (y up).
9. **BannerBG's texture-unit command blocks (and BannerBGmask's) hold DIM 0,
   ADDR 0, TYPE 0**, and no TEXUNIT_CONFIG. Unpatched, every texture
   samples transparent black. Write the real values after the block.
10. **The TEV COLOR words in BannerBG are all `0xFF000000`.** The stage's
    constant colour comes from the material (section 6.4), so editing the
    wallpaper's colours means writing those registers, not patching the
    command words.
11. Colour buffer format 2 is RGB5A1 and 3 is RGB565. Transfers and screens
    number them the other way.
12. RGBA8 pixels are A B G R in memory, and the fill value is 0xRRGGBBAA.
13. Untiled rows are top first; the transfer flip makes them bottom first.
    The console's own picture also needs the quarter turn counter-clockwise.
14. `display_transfer` bit 5 with bit 1 set means linear to linear in
    zakuro-gpu. On 3dbrew, bit 5 means tiled to tiled whatever bit 1 says.
    Bit 3 (TextureCopy mode) and bit 16 (32x32 tiles) are ignored; call
    `texture_copy` for raw copies.
15. **Jumps never return, and ADDR1/SIZE1 persist.** A copied sub-buffer
    ending in JUMP1 continues wherever the last list pointed. Write SIZE1 = 0
    first.
16. A register id of 0x300 or more ends the list silently. That is
    zakuro-gpu's guard against a misparsed list.
17. Writing `gpu.internal` directly skips every side effect. Upload shaders,
    uniforms and fixed attributes through a list.
18. Vertex attribute values arrive unnormalized, and zakuro-gpu aligns each
    attribute to its component size inside a vertex.
19. No mipmaps, no LOD and no cube, shadow or projection textures: units
    sample level 0 as 2D. Texcoord 0's w (semantic 0x10) is not read.
20. The GPU state persists like the console's. A fresh `Gpu` per frame is
    possible but discards the texture cache and the decoded programs.
21. zakuro-gpu logs through `log`. A logger with
    `RUST_LOG=zakuro_gpu::raster=trace` shows each draw's setup and its
    "pixels written / failed depth / failed stencil / failed alpha" counts
    (`raster.rs:2428-2439`, `2683-2706`), the quickest way to see why a draw
    left nothing.

## 12. Unknowns

* The registers marked "not read" (0x042/0x044, 0x064, 0x06E, 0x06F, 0x110,
  0x111, 0x112, 0x114, 0x11B, 0x231, 0x245, 0x24A in the vertex-only path,
  0x251, 0x253, 0x25F, 0x2BF, 0x010, PRIMITIVE_CONFIG bits 0-3,
  FRAMEBUFFER_DIM bit 24, the PARAM bits beyond filter and wrap) follow 3dbrew
  and citro3d. zakuro-gpu ignores them, so nothing here checked them.
* FRAMEBUFFER_DIM bit 24 ("vertical flip" on 3dbrew) is not modelled. If HOME
  writes it as 0, the console may flip something zakuro-gpu does not.
  Unverified.
* Whether HOME renders the top screen into a 240x400 buffer with a quarter
  turn in its projection is not in the files (HOME's code makes the frame).
* The meaning of the word before each TEV block (0, 5, 1, 2, 3, 4) as the
  material constant selector is SPICA's reading, not checked against HOME's
  rendering. So is the claim that the runtime fills TEVn_COLOR, DIM, ADDR,
  TYPE and TEXUNIT_CONFIG.
* The DefaultShader was not run in zakuro-gpu. Which DVLE BannerBG uses, the
  uniform values (matrix layout, IrScale, the bools) and the second word of
  each operand descriptor entry are for the shader and material notes.
* zakuro-gpu's attribute alignment inside a vertex is not checked against
  hardware. Section 6.8 says how to stay clear of it.
* LIGHTING_CONFIG0 = 0x400 with mask 0x2 in BannerBG: zakuro-gpu's fragment
  lighting reads it only when a TEV stage reads the fragment colours, and
  this note did not follow that path.

Exploration scripts (scratch, `python3 -I`):
`kabegami-explore/zakuro-gpu-recipe/scripts/` holds `lz.py` (LZ11),
`scan_commands.py` (command headers in the CGFX files), `shbin_fields.py`
(DVLB/DVLP/DVLE fields), `textures.py` (TXOB fields) and `peek_texture.py`
(private texture decode, output not published).

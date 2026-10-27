# CGFX materials (MTOB) and textures (TXOB)

## 1. Purpose

Kabegami draws the HOME Menu's default top-screen wallpaper by feeding the
file's own PICA200 state to zakuro-gpu, and lets the user edit the cell
texture, the drift and the colours. This spec covers the parts of a CGFX file
that hold that state:

- the material, `MTOB`: colours, rasterization, depth, blend and stencil, the
  texture coordinators, the texture mappers, the shader reference, the
  fragment shader (lighting, LUT references, the six TEV stages, the alpha
  test) and the hash words;
- the textures, `TXOB`: image and cube textures and their pixel data in `IMAG`.

It decodes BannerBG's one material, `mt_BG`, field by field, and its four
textures. It also says what HOME Menu's code does with them at run time, as far
as a disassembly of HOME's own code shows.

### Files and sources

| What | Where | SHA-256 |
|---|---|---|
| BannerBG, as stored (LZ11, 7212 bytes) | `romfs/3D/BannerBG_LZ.bin`, EUR HOME Menu 11.17, title 0004003000009802 v29696 | `27d58c2113d2c2d46e3bcc36bb2ae56c35e19d9823488287b6759df998108711` |
| BannerBG, decompressed (86080 bytes) | same | `092c8682d0cfabf0a1823a8e3a2c12556515c437afba2aa6f6ac7fc4d5e34595` |
| HOME `.code`, as stored (1393164 bytes, ExeFS backward-LZ) | `exefs-code.bin` | `e77d417b8609f891aa5a9ba20e26a087afdb254ef6e7f27fd8a4162b24cd6adc` |
| HOME `.code`, decompressed (2367488 bytes) | same | `5f4d8d0e80d8e7d6fafcefa0630c5106473ade4658585cfb499a6ec3324d89a7` |

For comparison, the same decoder ran over every CGFX file in `romfs/3D/`:
53 materials in 15 models, and 87 textures. "Seen in HOME" below means across
those 53 materials or 87 textures.

Unless a row says otherwise:

- every offset is in the **decompressed** data;
- `+0x...` is relative to the start of the structure;
- `[0x...]` is the absolute offset in decompressed `BannerBG_LZ.bin`.

Each BannerBG value was read from the file by the scripts named in section 4.

Code sources:

- SPICA (Unlicense, commit 82acbd6), mainly `SPICA/Formats/CtrGfx/Model/Material/*`,
  `SPICA/Formats/CtrGfx/Texture/*`, `SPICA/Formats/Common/TextureTransform.cs`,
  `SPICA/PICA/*` and `SPICA/Serialization/BinaryDeserializer.cs`. SPICA's
  reader is reflection-driven, so its field order *is* its layout claim. Every
  claim taken from it was checked against the files.
- 3dbrew: CGFX, GPU/Internal_Registers and Home_Menu/Themes, fetched 2026-10-25.
- zakuro-gpu (MIT): `src/tev.rs` and `src/texture.rs`, for how the emulator
  that Kabegami uses interprets the registers.
- HOME Menu's own code, disassembled in memory with capstone.
  - Addresses are given as VA = offset in the decompressed `.code` + 0x00100000.
    That base is the usual 3DS text base; the exheader was not checked.
  - No HOME code is reproduced here, only what it reads and writes.

## 2. Byte layouts

### 2.0 Conventions

| Term | Meaning |
|---|---|
| `ptr` | s32 self-relative pointer: target = offset of the field + value; 0 = null. |
| `list` | u32 count, then a `ptr` to the elements (SPICA `LengthPos.BeforePtr`). |
| `str` | `ptr` to a NUL-terminated ASCII string in the string pool at the end of `DATA`. |
| `f32x4` colour | R, G, B, A floats in 0..1. |
| `u8x4` colour | one u32 holding R in bits 0-7, G in 8-15, B in 16-23, A in 24-31. That is the byte order of the PICA colour registers, so a u8x4 can be written to a register as it is. |
| type id | the u32 at +0 of a polymorphic object (SPICA `TypeChoice`), followed by the 4-byte magic. |

All values are little-endian. The CGFX header holds BOM bytes FF FE at
[0x4]–[0x5], read as the u16 0xFEFF.

**PICA command blocks.** Several fields are raw GPU command lists in the
3dbrew format (GPU/Internal_Registers, "Command Header"):

- Each command is one parameter word, then a header word:

  | Header bits | Meaning |
  |---|---|
  | 0–15 | register id |
  | 16–19 | byte-enable mask; bit 16 enables byte 0 of the parameter, and so on |
  | 20–27 | number of extra parameter words |
  | 31 | consecutive mode: each extra word goes to the next register id, not the same one |

- The extra parameter words follow the header.
- A command that ends on an odd word is padded with one zero word, so every
  command is 8-byte aligned. SPICA's `PICACommandReader` makes the same
  assumption.

The tables below list every block as a header, then the register writes it
performs, named per the 3dbrew register list.

### 2.1 Reaching the material

| Offset | Type | Meaning | BannerBG |
|---|---|---|---|
| DATA slot 0 | list | models DICT | 1 entry, `BannerBG` → CMDL at [0x1B4], type id 0x40000092 |
| CMDL +0xBC | u32 | material count | 1 at [0x270] |
| CMDL +0xC0 | ptr | material DICT | [0x274] → DICT at [0x2EC] (size 0x2C, 1 entry) |
| DICT +0x1C + 0x10·i | node | refbit u32, left u16, right u16, name `str`, value `ptr` | entry 0: name `mt_BG`, value → MTOB at [0xC68] |

HOME's code reaches the material the same way: it follows CMDL +0xC0 and looks
`mt_BG` up by name (VA 0x24FB8C and 0x24FC98). One routine instead takes the
first entry's value at DICT +0x28 (VA 0x24FE14).

`mt_BG` owns these objects, in file order:

| Object | Range | Size |
|---|---|---|
| MTOB fixed part | [0xC68]–[0xF40] | 0x2D8 |
| TextureMappers[0], then its TextureReference and Sampler | [0xF40], [0xF8C], [0xFAC] | 0x48 + 0x20 + 0x20 |
| TextureMappers[1] block | [0xFCC]... | same |
| TextureMappers[2] block | [0x1058]... | same |
| ShaderReference | [0x10E4] | 0x20 |
| FragmentShader | [0x1104] | 0xF4 |
| FragLightLUTs | [0x11F8] | 0x18 |

The next object (a shape, `SOBJ`) starts at [0x1210].

Names in this spec:

- Member names come from the NW4C animation binding paths stored in the file,
  where they exist: `MaterialColor.*`, `TextureCoordinators[i].*`,
  `TextureMappers[i].Texture`, `TextureMappers[i].Sampler.BorderColor` and
  `FragmentOperation.BlendOperation.BlendColor`.
- Everything else uses SPICA's names.

### 2.2 MTOB fixed part (0x2D8 bytes)

Absolute offsets are for `mt_BG` at [0xC68]. "HOME" is the spread across the
53 HOME materials.

| Off | Type | Meaning | mt_BG | HOME |
|---|---|---|---|---|
| +0x000 | u32 | type id, material | 0x08000000 | all |
| +0x004 | magic | `MTOB` | | |
| +0x008 | u32 | revision | 0x06000100 | all |
| +0x00C | str | name | `mt_BG` (→ [0x2BD7]) | |
| +0x010 | list | metadata DICT | 0 / null | 0 in all |
| +0x018 | u32 | Flags: bit0 FragmentLighting, bit1 VertexLighting, bit2 HemiSphereLighting, bit3 HemiSphereOcclusion, bit4 Fog, bit5 PolygonOffset (SPICA `GfxMaterialFlags`) | 0 | 0x1 ×49, 0 ×4 |
| +0x01C | u32 | TexCoordConfig 0..5 = Config0120, 0110, 0111, 0112, 0121, 0122 | 0 (Config0120) | 0 in all |
| +0x020 | u32 | TranslucencyKind, draw layer 0..3 (SPICA: Layer0..Layer3) | 0 | 0 ×25, 1 ×28 |
| +0x024 | f32x4 ×11 | MaterialColor floats: Emission, Ambient, Diffuse, Specular0, Specular1, Constant0..5. **Ambient's 4th float is not an alpha but "Scale"** (SPICA `Scale = AmbientF.W`). | section 2.3 | Scale 1.0 in all |
| +0x0D4 | u8x4 ×11 | the same 11 colours as u8 | section 2.3 | |
| +0x100 | u32 | CommandCache (run-time) | 0 | 0 in all |
| +0x104 | u32 | Rasterization.IsPolygonOffsetEnabled | 0 | 0 in all |
| +0x108 | u32 | Rasterization.FaceCulling: 0 FrontFace, 1 BackFace, 2 Always, 3 Never | 0 | 1 ×48, 3 ×3, 0 ×2 (both `mt_BG`s) |
| +0x10C | f32 | Rasterization.PolygonOffsetUnit | 0.0 | 0.0 in all |
| +0x110 | u32 ×2 | Rasterization.FaceCullingCommand | section 2.4 | |
| +0x118 | u32 | FragmentOperation.Depth.Flags: bit0 test enabled, bit1 write (mask) enabled | 0 | 1 ×28, 3 ×20, 0 ×5 |
| +0x11C | u32 ×4 | Depth.Commands | section 2.5 | |
| +0x12C | u32 | BlendOperation.Mode: 0 None, 1 Blend, 2 BlendSeparate, 3 LogicalOp | 0 | 1 ×35, 0 ×17, 2 ×1 |
| +0x130 | f32x4 | BlendOperation.BlendColor | (0, 0, 0, 1) | |
| +0x140 | u32 ×6 | Blend.Commands | section 2.5 | |
| +0x158 | u32 ×4 | Stencil.Commands | section 2.5 | |
| +0x168 | s32 | UsedTextureCoordsCount | 3 | 0..3; always equals the number of non-null mappers |
| +0x16C | 0x58 ×3 | TextureCoordinators[0..2] | section 2.6 | |
| +0x274 | ptr ×3 | TextureMappers[0..2] | → [0xF40], [0xFCC], [0x1058] | |
| +0x280 | ptr | ProceduralTextureMapper | null | null in all |
| +0x284 | ptr | ShaderReference | → [0x10E4] | |
| +0x288 | ptr | FragmentShader | → [0x1104] | |
| +0x28C | s32 | ShaderProgramDescIndex | 0 | 0 in all |
| +0x290 | list | ShaderParameters | 0 / null | 0 in all |
| +0x298 | s32 | LightSetIndex | 0 | 0 in all |
| +0x29C | s32 | FogIndex | 0 | 0 in all |
| +0x2A0 | u32 ×14 | hash words | section 2.10 | |

HOME's own code confirms these colour offsets: VA 0x24FB60 writes MTOB +0x90,
+0xA0, +0xA4..+0xB0 and +0xD0, and their u8 copies at +0xEC, +0xF0, +0xF4 and
+0xFC (section 3.3). It also confirms the mapper pointer at +0x278 (VA 0x24FD2C)
and the mapper array at +0x274 (VA 0x24FED8).

### 2.3 Material colours of mt_BG

| Colour | Float at | Float value | u8 at | u8 value (raw) | TEV stage using it |
|---|---|---|---|---|---|
| Emission | +0x24 [0xC8C] | (0, 0, 0, 0) | +0xD4 [0xD3C] | (0, 0, 0, 0) (0x00000000) | none |
| Ambient | +0x34 [0xC9C] | (1, 1, 1), Scale 1.0 | +0xD8 [0xD40] | (255, 255, 255, 0) (0x00FFFFFF) | none |
| Diffuse | +0x44 [0xCAC] | (1, 1, 1, 1) | +0xDC [0xD44] | (255, 255, 255, 255) (0xFFFFFFFF) | none |
| Specular0 | +0x54 [0xCBC] | (1, 1, 1, 0) | +0xE0 [0xD48] | (255, 255, 255, 0) (0x00FFFFFF) | none |
| Specular1 | +0x64 [0xCCC] | (0, 0, 0, 0) | +0xE4 [0xD4C] | (0, 0, 0, 0) (0x00000000) | none |
| Constant0 | +0x74 [0xCDC] | (0.40, 0.45, 0.50, 0.00) | +0xE8 [0xD50] | (102, 115, 128, 0) (0x00807366) | stage 0 |
| Constant1 | +0x84 [0xCEC] | (0.00, 0.10, 0.20, 0.25) | +0xEC [0xD54] | (0, 26, 51, 64) (0x40331A00) | stage 2 |
| Constant2 | +0x94 [0xCFC] | (0.00, 0.00, 0.00, 0.75) | +0xF0 [0xD58] | (0, 0, 0, 191) (0xBF000000) | stage 3 |
| Constant3 | +0xA4 [0xD0C] | (0.80, 0.80, 0.85, 1.00) | +0xF4 [0xD5C] | (204, 204, 217, 255) (0xFFD9CCCC) | stage 4 |
| Constant4 | +0xB4 [0xD1C] | (0, 0, 0, 1) | +0xF8 [0xD60] | (0, 0, 0, 255) (0xFF000000) | stage 5 |
| Constant5 | +0xC4 [0xD2C] | (0, 0, 0, 1) | +0xFC [0xD64] | (0, 0, 0, 255) (0xFF000000) | stage 1 |

The float and u8 forms agree everywhere except one field:

- In all 53 HOME materials, u8 = floor(clamp(f, 0, 1) × 255 + 0.5) for every
  channel except Ambient's alpha.
- That is exactly HOME's packer at VA 0x1F8A94: it clamps to 0..1, multiplies
  by 255, adds 0.5 and truncates, R first.
- Ambient's u8 alpha is independent: 0 in `mt_BG`, and different from pack(Scale)
  in 33 of 53 materials.
- An editor must write both forms of every colour.

### 2.4 Rasterization of mt_BG

| Word | Value | Decoded |
|---|---|---|
| +0x110 [0xD78] | 0x00000001 | parameter |
| +0x114 [0xD7C] | 0x00010040 | header: reg 0x040 `GPUREG_FACECULLING_CONFIG`, mask 0x1, 0 extra, single |

This writes 0x040 = 1, which 3dbrew calls "Front Face (Counter Clockwise)".
zakuro-gpu (`raster.rs`) culls counter-clockwise triangles for mode 1.

The FaceCulling enum maps to the register as SPICA's `ToPICAFaceCulling` says:

| Enum | Register value | HOME materials |
|---|---|---|
| 0 FrontFace | 1 | 2 |
| 1 BackFace | 2 | 48 |
| 3 Never | 0 | 3 |
| 2 Always | 1 (per SPICA) | not seen |

The polygon offset fields hold no command; both are 0 in all HOME materials.

### 2.5 Fragment operation of mt_BG

**Depth**, Commands at +0x11C [0xD84]:

| Word | Value | Decoded |
|---|---|---|
| w0 | 0x00000010 | parameter |
| w1 | 0x00010107 | reg 0x107 `GPUREG_DEPTH_COLOR_MASK`, mask 0x1 (byte 0 only), single |
| w2 | 0x01000000 | parameter |
| w3 | 0x00080126 | reg 0x126 `GPUREG_GAS_DELTAZ_DEPTH`, mask 0x8 (byte 3 only), single |

- 0x107 byte 0 = 0x10: depth test off (bit 0 = 0), depth function 1 = Always
  (bits 4–6).
- 0x107 bytes 1–3 are **not written**. These hold the R/G/B/A/depth write
  enables (bits 8–12), so they must come from elsewhere; see section 3.6.
- 0x126 byte 3 = 0x01: gas depth function 1 (Always). SPICA derives it from the
  depth function: 0 or 1 stays the same, 2–5 become 3, 6–7 become 2. HOME's
  Less (4) gives 3 in 51 materials.
- Depth.Flags = 0: no depth test, and the depth write flag is clear.

**Blend**, Commands at +0x140 [0xDA8]:

| Word | Value | Decoded |
|---|---|---|
| w0 | 0x00E40100 | 0x100 `GPUREG_COLOR_OPERATION`: fragment mode 0 (default), blend mode 1 (blend), bits 16–25 = 0x0E4, the fixed value 3dbrew lists |
| w1 | 0x803F0100 | header: reg 0x100, mask 0xF, 3 extra, consecutive (writes 0x100–0x103) |
| w2 | 0x01010000 | 0x101 `GPUREG_BLEND_FUNC`: RGB and alpha equation Add; RGB src One, dst Zero; alpha src One, dst Zero |
| w3 | 0x00000000 | 0x102 `GPUREG_LOGIC_OP`: Clear; unused, because blend mode = blend |
| w4 | 0xFF000000 | 0x103 `GPUREG_BLEND_COLOR`: (0, 0, 0, 255) = pack(BlendColor) |
| w5 | 0 | padding |

So Mode 0 ("None") is stored as blending with One/Zero, which is a plain
replace. The blend colour word equals pack(+0x130) in all 53 materials.

For comparison, BannerBGmask's `mt_BG` uses:

- Mode 2 (BlendSeparate) with BLEND_FUNC 0x01760000: RGB SrcAlpha/OneMinusSrcAlpha,
  alpha One/Zero.
- Depth.Flags 3, with DEPTH_COLOR_MASK byte 0x11.

**Stencil**, Commands at +0x158 [0xDC0]:

| Word | Value | Decoded |
|---|---|---|
| w0 | 0x00000000 | 0x105 `GPUREG_STENCIL_TEST`: test off, function Never, reference 0, mask 0x00 |
| w1 | 0x000D0105 | header: reg 0x105, mask 0xD (bytes 0, 2, 3; byte 1, the stencil write mask, is not written), single |
| w2 | 0x00000000 | 0x106 `GPUREG_STENCIL_OP`: fail, z-fail and z-pass all Keep |
| w3 | 0x000F0106 | header: reg 0x106, mask 0xF, single |

The register field meanings and the test-function numbering follow 3dbrew:
0 Never, 1 Always, 2 Equal, 3 NotEqual, 4 Less, 5 LessEqual, 6 Greater,
7 GreaterEqual.

### 2.6 Texture coordinators

Each coordinator is 0x58 bytes. The three start at MTOB +0x16C [0xDD4],
+0x1C4 [0xE2C] and +0x21C [0xE84].

| Off | Type | Meaning | [0] | [1] | [2] |
|---|---|---|---|---|---|
| +0x00 | s32 | SourceCoordIndex: vertex UV set | 0 | 0 | 0 |
| +0x04 | u32 | MappingType: 0 UvCoordinateMap, 1 CameraCubeEnvMap, 2 CameraSphereEnvMap, 3 ProjectionMap, 4 Shadow, 5 ShadowBox | 0 | 0 | 0 |
| +0x08 | s32 | ReferenceCameraIndex (−1 = none) | −1 | −1 | −1 |
| +0x0C | u32 | TransformType, the matrix mode: 0 DccMaya, 1 DccSoftImage, 2 Dcc3dsMax | 2 | 2 | 2 |
| +0x10 | f32 ×2 | Scale (Sx, Sy) | (1, 1) | (3, 6) [0xE3C] | (3, 6) [0xE94] |
| +0x18 | f32 | Rotate R, radians | 0 | 0 | 0 |
| +0x1C | f32 ×2 | Translate (Tx, Ty) | (0, 0) | (0, 0) [0xE48] | (−0.02, 0) [0xEA0] |
| +0x24 | u32 | Flags: run-time enabled/dirty | 0 | 0 | 0 |
| +0x28 | f32 ×12 | Transform, the precomputed 3x4 matrix, as 3 rows of 4 floats | rows (1, −0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0) | rows (3, −0, 0, −1), (0, 6, 0, −2.5), (0, 0, 1, 0) | rows (3, −0, 0, −0.94), (0, 6, 0, −2.5), (0, 0, 1, 0) |

Values seen in HOME:

- mapping 0 or 2;
- matrix mode 0 (72 used coordinators) or 2 (17);
- reference camera −1 or 0.

Unused coordinators (index ≥ UsedTextureCoordsCount) are zeros with an
identity matrix (70 of them).

**Matrix formulas** (SPICA `Formats/Common/TextureTransform.cs`,
`GetTransform`). Let c = cos R and s = sin R. The file rows are:

    row0 = ( Sx·c,  −Sx·s,  0,  M41 )
    row1 = ( Sy·s,   Sy·c,  0,  M42 )
    row2 = (  0,      0,    1,   0  )

and the transformed coordinate is u' = row0 · (u, v, 0, 1), v' = row1 · (u, v, 0, 1).
In SPICA's names the file order is M11 M21 M31 M41 / M12 M22 M32 M42 / M13 M23 M33 M43
(`Math3D/Matrix.cs`, `ReadMatrix3x4`).

| Mode | M41 | M42 |
|---|---|---|
| 0 DccMaya | Sx·(0.5·s − 0.5·c + 0.5 − Tx) | Sy·(−0.5·s − 0.5·c + 0.5 − Ty) |
| 1 DccSoftImage | Sx·(−c·Tx − s·Ty) | Sy·(s·Tx − c·Ty) |
| 2 Dcc3dsMax | Sx·c·(−Tx − 0.5) − Sx·s·(Ty − 0.5) + 0.5 | Sy·s·(−Tx − 0.5) + Sy·c·(Ty − 0.5) + 0.5 |

How far the formulas were checked against the stored Transform:

- **Dcc3dsMax:** checked on all 17 used coordinators in HOME, including all
  three of `mt_BG`'s. The match is exact (largest difference 3.7e-9), down to the
  −0.0 in row0[1]. None of the 17 has a rotation, so the sign convention of R in
  this mode is unverified.
- **DccMaya:** checked on all 72 used coordinators, including one with
  R = π/4 and T = (1.1, 0): Prize_Effect coordinator 2 in BannerFolder and in
  BannerPrize. Largest difference 2.8e-8.
- **DccSoftImage:** appears in no HOME file, so it is unverified.

For `mt_BG` coordinator 1 (3dsMax, S = (3, 6), R = 0) this works out to

    u' = 3u − 3·Tx − 1
    v' = 6v + 6·Ty − 2.5

- One cell repeat in u needs ΔTx = 1/3.
- +Tx moves the pattern the opposite way to +Ty.

The drift clip animates `Materials["mt_BG"].TextureCoordinators[1].Translate`;
see section 3.4.

### 2.7 Texture mappers, texture references, samplers

**TextureMapper** (0x48 bytes):

| Off | Type | Meaning | mt_BG [0] / [1] / [2] |
|---|---|---|---|
| +0x00 | u32 | type id, standard mapper | 0x80000000 |
| +0x04 | u32 | DynamicAlloc (run-time) | 0 |
| +0x08 | ptr | Texture → TextureReference | → [0xF8C] / [0x1018] / [0x10A4] |
| +0x0C | ptr | Sampler | → [0xFAC] / [0x1038] / [0x10C4] |
| +0x10 | u32 ×14 | Commands, a template (below) | |
| +0x48 | s32 | CommandsLength in bytes | 56 (0x38) in all 89 HOME mappers |

**TextureReference** (0x20 bytes):

| Off | Type | Meaning | mt_BG |
|---|---|---|---|
| +0x00 | u32 | type id, texture reference | 0x20000004 |
| +0x04 | magic | `TXOB` | |
| +0x08 | u32 | revision | 0x05000000 |
| +0x0C | str | name | empty string ([0x3201]) |
| +0x10 | list | metadata | 0 / null |
| +0x18 | str | Path: the TXOB's name in the same file's texture DICT | `BG_DmyApp_00` / `BG_64_00` / `BG_64_00` |
| +0x1C | ptr | TexturePtr: the bound TXOB, set at run time | 0 |

HOME's SetTexture (VA 0x1FDAC4) stores a *self-relative* pointer to the TXOB
in +0x1C, and readers resolve it the same way (VA 0x24FD58).

**Standard sampler** (0x20 bytes):

| Off | Type | Meaning | mt_BG (all three) |
|---|---|---|---|
| +0x00 | u32 | type id | 0x80000000 |
| +0x04 | ptr | Parent, back to the owning mapper | → the mapper ([0xFB0] = −0x70 → [0xF40]) |
| +0x08 | u32 | MinFilter: 0 Nearest, 1 Linear, 2 NearestMipmapNearest, 3 NearestMipmapLinear, 4 LinearMipmapNearest, 5 LinearMipmapLinear | 1 |
| +0x0C | f32x4 | BorderColor; the animatable float copy of BORDER_COLOR | (0, 0, 0, 1) |
| +0x1C | f32 | LODBias | 0.0 |

The fields agree with the commands across HOME:

- MinFilter 0 goes with PARAM mag 0, min 0, mip 0; 1 with 1/1/0; 5 with 1/1/1
  and 2-level textures.
- BORDER_COLOR = pack(BorderColor) in all 89 mappers.
- HOME's helper at VA 0x2E33AC takes a sampler, follows +0x04 to the mapper,
  and returns (mapper +0x48, mapper +0x10).

**Mapper Commands template.** `mt_BG` mapper 0 is at [0xF50]:

| Word | Value | Decoded |
|---|---|---|
| w0 | 0x00000000 | 0x08E `GPUREG_TEXUNIT0_TYPE`: format 0, a placeholder |
| w1 | 0x0001008E | header: reg 0x08E, mask 0x1, single |
| w2 | 0xFF000000 | 0x081 `GPUREG_TEXUNIT0_BORDER_COLOR` = (0, 0, 0, 255) |
| w3 | 0x809F0081 | header: reg 0x081, mask 0xF, **9 extra**, consecutive (writes 0x081–0x08A) |
| w4 | 0x00000000 | 0x082 `TEXUNIT0_DIM`: placeholder |
| w5 | 0x00001106 | 0x083 `TEXUNIT0_PARAM`: mag Linear (bit 1), min Linear (bit 2), wrap T = 1 ClampToBorder (bits 8–10), wrap S = 1 ClampToBorder (bits 12–14), mip Nearest (bit 24), type 2D (bits 28–30) |
| w6 | 0x00000000 | 0x084 `TEXUNIT0_LOD`: bias 0, max level 0, min level 0 |
| w7–w12 | 0 | 0x085–0x08A `TEXUNIT0_ADDR1..6`: placeholders |
| w13 | 0 | padding |

Mappers 1 and 2 have the same shape:

- Mapper 1: headers 0x00010096 and 0x809F0091, PARAM 0x00002206.
- Mapper 2: headers 0x0001009E and 0x809F0099, PARAM 0x00002206.
- PARAM 0x00002206 means Linear/Linear, wrap S and T = 2 Repeat, 2D.

As stored, the 10-register run is only right for unit 0. For unit 1 it runs
0x091–0x09A: it rewrites 0x096 `TEXUNIT1_TYPE` with 0 and touches 0x099–0x09A,
unit 2's BORDER_COLOR and DIM. Unit 2's run covers 0x099–0x0A2. The template
must not be sent as stored. The run-time patch in section 3.1 shortens it to 5
registers.

PARAM wrap values, per 3dbrew:

| Value | Wrap |
|---|---|
| 0 | ClampToEdge |
| 1 | ClampToBorder |
| 2 | Repeat |
| 3 | MirroredRepeat |

TYPE, DIM and ADDR are 0 in all 89 HOME mappers, and LOD is 0 even for
2-level textures.

### 2.8 Shader reference

ShaderReference, 0x20 bytes at [0x10E4]:

| Off | Type | Meaning | mt_BG |
|---|---|---|---|
| +0x00 | u32 | type id, shader reference | 0x80000001 |
| +0x04 | magic | `SHDR` | |
| +0x08 | u32 | revision | 0x05000000 |
| +0x0C | str | name | empty |
| +0x10 | list | metadata | 0 / null |
| +0x18 | str | Path: SHDR name | `DefaultShader` |
| +0x1C | u32 | ShaderPtr (run-time) | 0 |

`DefaultShader` is the only SHDR in `3D/nwgfx_DefaultShader_LZ.bin`:

- The SHDR is at [0xC0] of that file, type id 0x80000002, and embeds a `DVLB`.
- HOME's code holds the path string `3D/nwgfx_DefaultShader_LZ.bin` (VA 0x30657C).

`mt_BG` passes no shader parameters (ShaderParameters 0 / null), and its
ShaderProgramDescIndex is 0.

### 2.9 Fragment shader

FragmentShader, 0xF4 bytes at [0x1104]:

| Off | Type | Meaning | mt_BG |
|---|---|---|---|
| +0x00 | f32x4 | TexEnvBufferColor (float copy) | (0, 0, 0, 1) |
| +0x10 | u32 | Lighting.Flags (SPICA `GfxFragmentFlags`: bit0 ClampHighLight, bit1 LUT Dist0, bits 2/3 overlap in SPICA's enum, bit4 Reflection) | 0 |
| +0x14 | u32 | SPICA calls it Lighting.TranslucencyKind | 0 |
| +0x18 | u32 | FresnelSelector: 0 none, 1 primary, 2 secondary, 3 both (= `LIGHTING_CONFIG0` bits 2–3) | 0 |
| +0x1C | s32 | BumpTexture: texture unit | 0 |
| +0x20 | u32 | BumpMode: 0 not used, 1 bump, 2 tangent | 0 |
| +0x24 | u32 | IsBumpRenormalize | 0 |
| +0x28 | ptr | FragLightLUTs | → [0x11F8] |
| +0x2C + 0x1C·i | 0x1C ×6 | TextureEnvironments[i]: u32 Constant, then u32 ×6 Commands | below |
| +0xD4 | u32 ×2 | AlphaTest.Commands | below |
| +0xDC | u32 ×6 | Commands: buffer colour, buffer update, lighting | below |

Values seen in HOME:

- Lighting.Flags: 0, 0x2 or 0xA.
- +0x14: 0 or 3. All materials with a Fresnel LUT have 3. That fits
  `LIGHTING_CONFIG0`'s "light environment configuration" (config 3 offers
  D0, D1, FR, DA) better than a translucency kind; unverified.
- FresnelSelector: 0, 2 or 3; non-zero exactly when a Fresnel LUT is present.
- BumpTexture / BumpMode: 0/0 or 1/1.

**FragLightLUTs**, 0x18 bytes: six `ptr`s, in the order ReflecR, ReflecG,
ReflecB, Dist0, Dist1, Fresnel. All six are null in `mt_BG`.

When a pointer is set, it points to a GfxFragLightLUT:

| Off | Type | Meaning |
|---|---|---|
| +0x00 | u32 | Input, the PICA LUT input selector: 0 N·H, 1 V·H, 2 N·V, 3 L·N, 4 −L·P, 5 cos φ |
| +0x04 | u32 | Scale, the PICA LUT scale selector: 0 1x, 1 2x, 2 4x, 3 8x, 6 0.25x, 7 0.5x |
| +0x08 | ptr | LUT reference |

The LUT reference has type id 0x40000000 at +0x00. It then holds +0x04
SamplerName `str` (a LUTS name in the file's LUT DICT), +0x08 TableName `str`,
and +0x0C TargetLUT (run-time).

This layout was verified on BannerFolder `Prize_01`:

- Its Dist0 slot at +0xC points to [0x3B24] with Input 0, Scale 0.
- That points to a reference with sampler `LUT_Folder_00` and table `Prize_01`.

Across HOME only slot 3 (Dist0, 23 materials) and slot 5 (Fresnel, 4) occur.
The inputs and scales seen are (0, 0), (2, 0) and (0, 7).

**TEV stages of mt_BG.** Each stage's 6 command words are:

| Word | Content |
|---|---|
| w0 | SOURCE parameter |
| w1 | header 0x804F00xx: reg = the stage's base, mask 0xF, 4 extra, consecutive |
| w2 | OPERAND |
| w3 | COMBINER |
| w4 | COLOR |
| w5 | SCALE |

The header writes the stage's SOURCE, OPERAND, COMBINER, COLOR and SCALE
registers in a row. The stage base registers, per 3dbrew, are 0x0C0, 0x0C8,
0x0D0, 0x0D8, 0x0F0 and 0x0F8 (`GPUREG_TEXENVi_SOURCE`).

**w4 (`TEXENVi_COLOR`) is 0xFF000000 in every one of the 318 stages of the 53
HOME materials**, whatever constant the stage uses, so it is a placeholder. The
colour actually used is the material colour that the stage's Constant field
names (section 3.2). Constant values 0–5 mean Constant0–5; 6–10 mean Emission,
Ambient, Diffuse, Specular0, Specular1.

| Stage | Constant field at → colour | SOURCE | OPERAND | COMBINER | SCALE |
|---|---|---|---|---|---|
| 0 | +0x2C [0x1130] = 0 → Constant0 | 0x0E0403EE | 0x00060020 | 0x00010004 | 0 |
| 1 | +0x48 [0x114C] = 5 → Constant5 | 0x0EE50EFF | 0x00002020 | 0x00010001 | 0 |
| 2 | +0x64 [0x1168] = 1 → Constant1 | 0x0EF40F0E | 0x00002050 | 0x00090008 | 0 |
| 3 | +0x80 [0x1184] = 2 → Constant2 | 0x0EEE0FE0 | 0x00000224 | 0x00000008 | 0 |
| 4 | +0x9C [0x11A0] = 3 → Constant3 | 0x0EEE0FEE | 0x00000002 | 0x00000004 | 0 |
| 5 | +0xB8 [0x11BC] = 4 → Constant4 | 0x0EED0EDF | 0x00000200 | 0x00000004 | 0 |

The words are decoded with the 3dbrew field layout:

| Register | Bits |
|---|---|
| SOURCE | RGB sources 0–3, 4–7, 8–11; alpha sources 16–19, 20–23, 24–27 |
| OPERAND | RGB operands 0–3, 4–7, 8–11; alpha operands 12–14, 16–18, 20–22 |
| COMBINER | RGB mode 0–3; alpha mode 16–19 |
| SCALE | RGB 0–1; alpha 16–17 |

Notation: C = this stage's constant, P = primary colour (the vertex shader's
colour output), Tn = texture unit n, prev = the previous stage's output,
buf = the combiner buffer. The combine formulas are zakuro-gpu's
(`tev.rs`, `combine_rgb` and `combine_alpha`):

| Mode | Formula |
|---|---|
| Interpolate | s0·s2 + s1·(1 − s2) |
| MultiplyAdd | min(s0·s1 + s2, 1) |
| AddMultiply | min(s0 + s1, 1)·s2 |

| Stage | RGB: sources [operands] → mode | Alpha: sources [operands] → mode |
|---|---|---|
| 0 | C, C, T0 [rgb, aaa, rgb] → Interpolate = C.rgb·T0.rgb + C.a·(1 − T0.rgb) | T1, P, C [a, b, a] → Modulate = T1.a·P.b |
| 1 | prev, prev, C [rgb, aaa, rgb] → Modulate = prev.rgb·prev.a | T2, C, C [r, a, a] → Modulate = T2.r·C.a |
| 2 | C, P, prev [rgb, 1−rrr, rgb] → MultiplyAdd = C.rgb·(1 − P.r) + prev.rgb | T1, prev, C [r, a, a] → AddMultiply = min(T1.r + prev.a, 1)·C.a |
| 3 | P, C, prev [rrr, aaa, aaa] → MultiplyAdd = P.r·C.a + prev.a (grey) | C [a] → Replace = C.a |
| 4 | C, C, prev [aaa, rgb, rgb] → Interpolate = C.a·prev.rgb + C.rgb·(1 − prev.rgb) | C [a] → Replace = C.a |
| 5 | prev, buf, C [rgb, rgb, aaa] → Interpolate = prev.rgb·C.a + buf.rgb·(1 − C.a) | buf [a] → Replace = buf.a |

All scales are 1x.

**Buffer and lighting commands** at FragmentShader +0xDC [0x11E0]:

| Words | Value / header | Decoded |
|---|---|---|
| w0 / w1 | 0xFF000000 / 0x000F00FD | `GPUREG_TEXENV_BUFFER_COLOR` = (0, 0, 0, 255) = pack(+0x00) |
| w2 / w3 | 0x00000400 / 0x000200E0 | `GPUREG_TEXENV_UPDATE_BUFFER`, mask 0x2 (byte 1 only): bit 10 = "TexEnv 3 RGB buffer input" |
| w4 / w5 | 0x00000400 / 0x000201C3 | `GPUREG_LIGHTING_CONFIG0`, mask 0x2 (byte 1): bits 8–11 = 4, the fixed value 3dbrew lists; nothing else |

How the buffer works for `mt_BG`:

- Bit 10 makes the buffer take stage 2's RGB.
- In zakuro-gpu's model (`tev.rs`, `apply`) the buffer lags: stage 0 reads 0,
  stage 1 reads the buffer colour, and the update from stage i is visible from
  stage i + 2.
- So stage 5's buf.rgb is stage 2's RGB.
- No alpha bit is set, so buf.a stays the buffer colour's 255.
- The fog bits (byte 0) and z-flip (byte 2) of 0x0E0 are not written by the
  material.

In HOME the buffer colour word equals pack(+0x00) in all 53 materials, three of
which are not black. The update word takes 9 different values.

**Alpha test** at FragmentShader +0xD4 [0x11D8]:

| Word | Value | Decoded |
|---|---|---|
| w0 | 0x00000010 | parameter |
| w1 | 0x000F0104 | `GPUREG_FRAGOP_ALPHA_TEST`, mask 0xF |

This means test off, function 1 = Always, reference 0. The test is enabled in
5 HOME materials, all with function 6 (Greater), at references 153 and 97.

### 2.10 Hash words (MTOB +0x2A0, 14 × u32)

Every hash is SPICA's `HashBuffer`: MD5 of a byte string, folded to 32 bits by
XORing byte i into bit position (i mod 4)·8. A result of 0 becomes 1. The hash
of an empty string is 0x3B75655E.

All 53 HOME materials match the recipes below, given the authored blend
factors that the FragOpHash row explains. Recipes marked * differ from SPICA's
code.

| Off | Word | mt_BG | Hashed bytes |
|---|---|---|---|
| +0x2A0 [0xF08] | MaterialFlagsHash | 0xDA5DE5CD | u32 (Flags \| 0x20); bit 5 is always set |
| +0x2A4 | ShaderParamsHash | 0x3B75655E | empty (no parameters in HOME) |
| +0x2A8 | TextureCoordsHash | 0x6B17D4E9 | u32 TexCoordConfig; then for each coordinator i = 0..2: its +0x00..+0x0F, Scale (only if i < used), Rotate, Translate (only if i < used), one zero byte, the 48-byte Transform, then u32 TexCoordConfig again |
| +0x2AC | TextureSamplersHash* | 0x77FE8AF3 | u32 TexCoordConfig; then for each mapper: f32x4 border colour (u8/255 rounded to 0.01), u32 wrap U and u32 wrap V (PICA 0/1/2/3 → 2/3/0/1), f32 min LOD, f32 LOD bias, **u32 the sampler's stored MinFilter (+0x08)**, u32 mag filter. SPICA derives MinFilter from the PARAM bits and gets 0xC9C60906. |
| +0x2B0 | TextureMappersHash | 0 | always 0 in HOME |
| +0x2B4 | MaterialColorsHash | 0xCE15886F | RGB of Emission, RGB of Ambient, f32 Scale, Diffuse RGBA, RGB of Specular0, RGB of Specular1, RGBA of Constant0..5, all as f32 |
| +0x2B8 | RasterizationHash | 0x79F14F75 | u32 polygon offset enable, the 2 culling command words, f32 PolygonOffsetUnit |
| +0x2BC | FragLightHash* | 0xE0099C31 | the 5 u32 at FragmentShader +0x10..+0x23, then one byte for IsBumpRenormalize (21 bytes). SPICA appends a lighting-enable byte that is not there. |
| +0x2C0 | FragLightLUTHash | 0 | always 0 in HOME |
| +0x2C4 | FragLightLUTSampHash | 0x3B75655E | (Input u32, Scale u32) of each present LUT, in slot order |
| +0x2C8 | TextureEnvironmentHash | 0xAB938FDF | buffer colour f32x4; then for each stage: u32 Constant, the 3 RGB sources, 3 RGB operands, 3 alpha sources and 3 alpha operands as GL enums (SPICA `GfxTexEnv.GetGLSrc`/`GetGLOp`), and the 6 command words |
| +0x2CC | AlphaTestHash* | 0x640102F8 | u8 enable, u32 function as the NW4C enum (0 Never, 1 Always, 2 Less, 3 LessEqual, 4 Equal, 5 GreaterEqual, 6 Greater, 7 NotEqual), **f32 the authored reference**. The authored value matches round(ref/255, 2), so 128 hashes as 0.5. SPICA uses ref/255 and fails on 9 materials. |
| +0x2D0 | FragOpHash* | 0xF412E639 | u32 Depth.Flags, u32 depth function (NW4C enum), u32 0, u32 Mode, f32x4 blend colour, u32 logic op (NW4C enum; Copy = 1 unless Mode 3), then six u32 GL blend values (colour src, dst, equation, alpha src, dst, equation), then the 4 stencil words. The six values are the *authored* factors. For Mode 1 the alpha triple is One/Zero/Add (1, 0, 0x8006), not a repeat of the colour triple. For Mode 0 the stored command does not hold them; SrcAlpha/OneMinusSrcAlpha/Add + One/Zero/Add fits 16 of 17 materials. BannerAppletNews `mt_Body` hashes SrcAlpha/One/Add + One/Zero/Add. |
| +0x2D4 | UniqueId | 0 | always 0 in HOME |

### 2.11 TXOB textures

**Common header.** Each texture is listed in DATA slot 1 (the texture DICT).

| Off | Type | Meaning | Seen |
|---|---|---|---|
| +0x00 | u32 | type id: 0x20000011 image, 0x20000009 cube, 0x20000004 reference (only inside materials and anims) | 0x20000011 in all 87 |
| +0x04 | magic | `TXOB` | |
| +0x08 | u32 | revision | 0x05000000 |
| +0x0C | str | name | |
| +0x10 | list | metadata | 0 / null |
| +0x18 | s32 | Height | |
| +0x1C | s32 | Width | |
| +0x20 | u32 | GLFormat | see the format table |
| +0x24 | u32 | GLType | see the format table |
| +0x28 | s32 | MipmapSize: number of levels, including the base | 1 or 2 |
| +0x2C | u32 | TextureObj (run-time) | 0 |
| +0x30 | u32 | LocationFlag (run-time) | 0 |
| +0x34 | u32 | HwFormat, the PICA texture format | see the format table |

**Image texture** (0x20000011, 0x5C bytes in all 87): +0x38 is a `ptr` to the
image data object, which sits at TXOB +0x3C in all 87.

**Cube texture** (0x20000009): +0x38..+0x4C hold six `ptr`s, in the order
X+, X−, Y+, Y−, Z+, Z−. Each points to an image data object.

- Source: SPICA `GfxTextureCube`, and HOME's cube binder at VA 0x197098, which
  reads the six pointers at +0x38 + 4i.
- No HOME file has a cube texture, so this is unverified against data.

**Image data object** (0x20 bytes):

| Off | Type | Meaning |
|---|---|---|
| +0x00 | s32 | Height |
| +0x04 | s32 | Width |
| +0x08 | u32 | data length in bytes |
| +0x0C | ptr | data, inside `IMAG` |
| +0x10 | u32 | DynamicAlloc (run-time), 0 |
| +0x14 | s32 | BitsPerPixel |
| +0x18 | u32 | **LocationPtr**: an absolute address, 0 in the file. When non-zero, HOME's binders use it instead of the data pointer (VA 0x1972C8). It is presumably where the runtime has placed the data, for example in VRAM. |
| +0x1C | u32 | MemoryArea, 0 |

**Pixel data.**

- The data lives in the `IMAG` section that follows `DATA`. In BannerBG,
  `IMAG` is at [0x3278] with size 0x11DC8 including its 8-byte header, and ends
  at the file end (0x15040).
- Before the textures, `IMAG` also holds two buffers that the shape at [0x1210]
  points to: [0x3280], 0x1BC bytes, after a 0x1401 format word, presumably the
  indices; and [0x343C], 0x990 bytes, presumably the vertices. Those belong to
  the mesh spec.
- Every texture data offset is 0x80-aligned (all 87).
- Mip levels follow the base level, each half the size. Length =
  Σ over levels of w·h·bpp/8; this held for all 87, including the 2-level ones.
- Texels are in 8x8 tiles, Morton order inside a tile, tiles row by row.
- ETC1 and ETC1A4 store 4x4 blocks, four per 8x8 tile. ETC1A4 puts 8 bytes of
  4-bit alpha before each 8-byte colour block.
- Sources: 3dbrew CGFX/SMDH, and zakuro-gpu `texture.rs` (`sample_uncompressed`,
  `sample_etc1`).

**Formats.** HwFormat is the PICA format (3dbrew `GPUREG_TEXUNITi_TYPE`; SPICA
`PICATextureFormat`).

| HwFormat | PICA format | bpp | GLFormat / GLType seen | Seen | zakuro-gpu texel |
|---|---|---|---|---|---|
| 0 | RGBA8 | 32 | 0x6752 / 0x1401 | 1 | |
| 1 | RGB8 | 24 | not seen | | |
| 2 | RGBA5551 | 16 | not seen | | |
| 3 | RGB565 | 16 | 0x6754 / 0x8363 | 5 | |
| 4 | RGBA4 | 16 | not seen | | |
| 5 | LA8 | 16 | 0x6758 / 0x1401 | 5 | |
| 6 | HILO8 | 16 | not seen | | |
| 7 | L8 | 8 | 0x6757 / 0x1401 | 38 | (L, L, L, 255) |
| 8 | A8 | 8 | 0x6756 / 0x1401 | 2 | (0, 0, 0, A) |
| 9 | LA4 | 8 | 0x6758 / 0x6760 | 5 | |
| 10 | L4 | 4 | 0x6757 / 0x6761 | 13 | |
| 11 | A4 | 4 | 0x6756 / 0x6761 | 14 | (0, 0, 0, A·17) |
| 12 | ETC1 | 4 | 0x675A / 0 | 3 | |
| 13 | ETC1A4 | 8 | 0x675B / 0 | 1 | |

The GL values come in a pattern:

- GLFormat names the channel set: 0x6752 RGBA, 0x6754 RGB, 0x6756 alpha,
  0x6757 luminance, 0x6758 luminance+alpha, 0x675A ETC1, 0x675B ETC1A4.
- GLType names the packing: 0x1401 GL_UNSIGNED_BYTE, 0x8363
  GL_UNSIGNED_SHORT_5_6_5, 0x6760 two 4-bit values per byte, 0x6761 4 bits.
- The DMP extension names for these constants are not given here (unverified).
- The runtime binder reads only HwFormat, Height, Width and MipmapSize
  (section 3.1).

### 2.12 BannerBG's four textures

| Name | TXOB | Size (W×H) | HwFormat | GL format / type | Levels | Image data | Data offset, length | Data SHA-256 |
|---|---|---|---|---|---|---|---|---|
| `BG_64_00` | [0x15FC] | 64×64 (+0x1C [0x1618], +0x18 [0x1614]) | 7 L8 ([0x1630]) | 0x6757 / 0x1401 | 1 ([0x1624]) | [0x1638], bpp 8 | [0x3E00], 0x1000 | `c756cd1ad486b2181349276d08389b12fa765fd2f578f9bb9671b26cf9c8d003` |
| `BG_CapMask_00` | [0x1658] | 256×512 ([0x1674], [0x1670]) | 11 A4 ([0x168C]) | 0x6756 / 0x6761 | 1 | [0x1694], bpp 4 | [0x4E00], 0x10000 | `766941af7d632e5991da97b3e634c0ec8e007c1951a880d4fd6209dee80c8706` |
| `BG_DefBase_00` | [0x16B4] | 32×32 ([0x16D0], [0x16CC]) | 12 ETC1 ([0x16E8]) | 0x675A / 0 | 1 | [0x16F0], bpp 4 | [0x14E00], 0x200 | `2c33f20aee8e3f5f99712f7278e771fe999a81d35a16ed599799b7c4b0091acd` |
| `BG_DmyApp_00` | [0x1710] | 8×8 ([0x172C], [0x1728]) | 7 L8 ([0x1744]) | 0x6757 / 0x1401 | 1 | [0x174C], bpp 8 | [0x15000], 0x40 | `8667e718294e9e0df1d30600ba3eeb201f764aad2dad72748643e4a285e1d1f7` |

What references each texture in the file (every self-relative pointer to each
name was searched):

- **`BG_64_00`**:
  - TextureMappers[1] and [2];
  - texture-pattern references in BannerBG_AppPause, AppQuit, AppRestart
    ([0x2198], [0x2424], [0x2880]) and Loop ([0x2AF4]).
- **`BG_CapMask_00`**: only the texture-pattern lists of AppPause, AppQuit and
  AppRestart ([0x21B8], [0x2444], [0x28A0]). They target
  `TextureMappers[1].Texture`.
- **`BG_DmyApp_00`**: TextureMappers[0] only.
- **`BG_DefBase_00`**: nothing besides its DICT entry. HOME's code has no string
  for it either.

## 3. How the runtime uses it

### 3.1 Binding a texture to a mapper (nw::gfx inside HOME, verified by disassembly)

`SetTexture(mapper, texture)` at VA 0x1FDAC4:

1. It resolves a reference to the TXOB it names.
2. It stores a self-relative pointer to the TXOB in TextureReference +0x1C.
3. It dispatches on the TXOB type id to a routine that rewrites the mapper's own
   `Commands` and `CommandsLength` in place.

For an **image texture** (0x20000011), the routine at VA 0x197254 does this:

| Word | Change |
|---|---|
| CommandsLength (+0x48) | 0x20, so only w0–w7 are sent |
| w0 (TYPE) | format bits = HwFormat & 0xF |
| w1 (TYPE header) | mask set to 0xF |
| w3 (BORDER header) | extra count set to 4, so the run is BORDER_COLOR, DIM, PARAM, LOD, ADDR1 |
| w4 (DIM) | height (TXOB +0x18) in bits 0–10, width (+0x1C) in bits 16–26 |
| w5 (PARAM) | bits 4–5 cleared; set to 2 only for HwFormat 12 (ETC1, not ETC1A4) |
| w6 (LOD) | max level (bits 16–19) = MipmapSize − 1 |
| w7 (ADDR1) | (physical address of the data) >> 3, where the data address is LocationPtr if non-zero, else the RawBuffer pointer, converted by the virtual-to-physical routine at VA 0x145A28 |

The other binders:

- **Cube** (0x20000009, VA 0x197098): keeps CommandsLength 0x38 and the count
  of 9 (10 registers). It fills ADDR1 from face 0 as a 28-bit value and
  ADDR2..6 from faces 1–5 as 22-bit values (bits 0–21), as 3dbrew describes.
- **Type 0x20000021** (VA 0x197380): sets PARAM type bits 28–30 to 2 (Shadow2D)
  and a run of 11 registers through 0x08B `TEXUNIT0_SHADOW`, so it is presumably
  a shadow texture. No HOME file has one.

With the textures in the file, `mt_BG`'s mappers therefore become (ADDR
symbolic):

| Mapper | Texture | TYPE (header) | BORDER run header | DIM | PARAM | LOD |
|---|---|---|---|---|---|---|
| 0 | BG_DmyApp_00 | 7 (0x000F008E) | 0x804F0081 | 0x00080008 | 0x00001106 | 0 |
| 1 | BG_64_00 | 7 (0x000F0096) | 0x804F0091 | 0x00400040 | 0x00002206 | 0 |
| 2 | BG_64_00 | 7 (0x000F009E) | 0x804F0099 | 0x00400040 | 0x00002206 | 0 |

`patch_mapper.py` computes these.

### 3.2 TEV constant colours (inferred)

The runtime cannot use the TEXENVi_COLOR words as stored:

- They are 0xFF000000 in every HOME stage.
- HOME's theme code (section 3.3) changes only MaterialColor, in both forms,
  and never touches a combiner command.

So it must take each stage's COLOR from MaterialColor, using the stage's
Constant field (most likely the u8 form). The code that does this was not
found, so treat it as inferred.

For Kabegami, write `TEXENVi_COLOR` = the u8 colour named by stage i's Constant:

| Stage | Colour | Value |
|---|---|---|
| 0 | Constant0 | 0x00807366 |
| 1 | Constant5 | 0xFF000000 |
| 2 | Constant1 | 0x40331A00 |
| 3 | Constant2 | 0xBF000000 |
| 4 | Constant3 | 0xFFD9CCCC |
| 5 | Constant4 | 0xFF000000 |

The other command blocks (culling, depth, blend, stencil, combiners, buffer,
alpha test) hold real values. They are presumably sent as stored (unverified).

### 3.3 HOME's own use of mt_BG (verified by disassembly)

**Colours (VA 0x24FB60).** HOME looks `mt_BG` up by name and writes a
seven-float block S into its MaterialColor, repacking each colour with the
packer at VA 0x1F8A94:

| Colour | Written |
|---|---|
| Constant3 | (S[0], S[1], S[2], S[6]) |
| Constant2.a | S[3] |
| Constant1.a | S[4] |
| Constant5.a | S[5] |

The RGB of Constant1, Constant2 and Constant5 is left as it is.

**Saved defaults (VA 0x24FC74).** Before that, HOME copies the same seven
floats out of the loaded material, once, into a saved block. A flag at +0x114 of that
block records that the copy was made. It also saves the texture currently bound
to TextureMappers[1].

**Which values get applied.** The caller at VA 0x1D35F4 passes one of two
blocks, and a byte at +6 of a HOME state object picks which:

- the block at +0x1C of that state object, the theme's;
- a block at +4 of another object. It has exactly the saved-defaults layout,
  so it is presumably the saved defaults.

So:

- Edits Kabegami makes to these seven floats in the file become HOME's
  defaults.
- A theme's top-screen colour block overrides them while it is active.

Meaning, inferred from the TEV chain in section 3.5 and the 3dbrew theme page's
"background colour, gradient, square opacity":

| Value | Role |
|---|---|
| Constant3.rgb | background colour |
| Constant2.a | gradient strength |
| Constant1.a | square opacity |
| Constant5.a | weight of the unit-2 layer |
| Constant3.a | weight of the lightening term |

**Cell textures (VA 0x24FEAC).** For TextureMappers[1] and [2], HOME first
checks that the bound texture is **64×64**, reading TXOB +0x1C and +0x18. Only
then does it call SetTexture:

- on one path, with up to two textures from the theme state;
- on the other path, with the texture held at +0 of a block that has the
  saved-defaults "copied" flag at +0x114. That is presumably the texture first
  saved from mapper 1, and it is bound to *both* mappers.

So the cell texture should stay 64×64, and mapper 2 may be rebound to mapper
1's texture.

**App texture (VA 0x24FDB0).** HOME rebinds TextureMappers[0], the
`BG_DmyApp_00` placeholder, to a texture from its own state. It then clears
the wrap bits of that mapper's PARAM word (Commands w5, through VA 0x2E33AC),
which gives ClampToEdge.

That this texture is the suspended application's picture is inferred:

- the placeholder's name;
- the AppPause/AppQuit/AppRestart clips, which animate Constant4.a, the stage-5
  blend between the wallpaper and the stage-2 buffer.

### 3.4 Animation targets

The material animations bind by path strings in the file's string pool
([0x2C05]–[0x30D5]). The table lists the path strings referenced from inside
each clip's bytes; the clip format itself is not decoded here. The members they
reach:

| Clip | Targets |
|---|---|
| BannerBG_Loop | MaterialColor.Constant4; TextureCoordinators[1].Translate; TextureMappers[1].Texture |
| BannerBG_AppPause | Constant0, Constant1, Constant4; TextureCoordinators[0].Scale; [1].Scale; [1].Translate; TextureMappers[1].Texture |
| BannerBG_AppQuit | Constant4; TextureCoordinators[0].Scale; [0].Translate; [1].Scale; [1].Translate; TextureMappers[1].Texture |
| BannerBG_AppRestart | Constant0, Constant1, Constant4; TextureCoordinators[0].Scale; [1].Scale; TextureMappers[1].Texture |

The pool also lists binding paths for every MaterialColor, each sampler's
BorderColor, each coordinator's Scale, Rotate and Translate, and BlendColor.
The clip format belongs to the animation spec.

### 3.5 What mt_BG computes (derived from section 2.9)

This assumes the default L8 cells, where unit 1 reads (L1, L1, L1, 1) and
unit 2 reads (L2, L2, L2, 1), and Constant4.a = 1:

    squares = min(L1 + L2·C5.a, 1) · C1.a                      (stage 2 alpha)
    g       = min(P.r · C2.a + squares, 1)                       (stage 3, grey)
    out.rgb = C3.a·g + C3.rgb·(1 − g)                            (stage 4)
    out.a   = 1                                                  (buffer colour alpha)

When Constant4.a is below 1, stage 5 blends toward buf.rgb, which is stage 2's
RGB:

    buf.rgb = min(C1.rgb·(1 − P.r) + (C0.rgb·T0.rgb + C0.a·(1 − T0.rgb))·T1.a·P.b, 1)

In the file C0.a is 0, which leaves C0.rgb·T0.rgb·T1.a·P.b, but AppPause and
AppRestart animate Constant0.

P is the mesh's vertex colour, so the gradient is in the mesh; see the mesh
spec. An A8 or A4 cell (alpha only, black RGB) feeds stages 0 and 2
differently. That matters if Kabegami changes the cell's format.

### 3.6 State the material does not set

When Kabegami drives zakuro-gpu itself, it must also supply these; the values
NW4C uses are unverified:

- `GPUREG_TEXUNIT_CONFIG` (0x080): enable units 0–2, plus the TexCoordConfig
  bits. Config0120 presumably means unit 2 uses coordinate 2 and unit 3 uses
  coordinate 0 (bits 13 and 8–9).
- `DEPTH_COLOR_MASK` byte 1, the colour and depth write enables, and
  `STENCIL_TEST` byte 1.
- The fog bits of `TEXENV_UPDATE_BUFFER`, and lighting disabled (Flags bit 0
  is 0).
- The framebuffer access and the vertex shader uniforms, including the three
  texture matrices. SPICA's renderer (`SPICA.Rendering/Model.cs`) uploads
  TexMtx0 and TexMtx1 as 3 rows and TexMtx2 as 2 rows; the real uniform
  locations belong to the shader spec.

### 3.7 Editing in place

**Sizes.** Every field Kabegami edits is fixed-size, so nothing moves:

- colours;
- coordinator Scale, Rotate and Translate;
- combiner words;
- sampler and PARAM;
- texture pixels of the same format and size.

**Values to keep in sync** with each edit:

- the u8 and float forms of every colour;
- BlendColor with BLEND_COLOR;
- BorderColor with BORDER_COLOR;
- the buffer colour float with its command;
- the coordinator's Transform with its Scale, Rotate and Translate (section 2.6);
- the hash words (section 2.10).

Whether the runtime checks the hashes is unknown, but they can be recomputed.

**Size changes.** Changing a texture's size or format means re-laying out
`IMAG`, its pointers, the CGFX file size at header +0x0C, and the LZ11 wrapper.
Keep the cell at 64×64 (section 3.3).

## 4. Unknowns

- **TEV colour fill:** the code that fills `TEXENVi_COLOR` from MaterialColor
  was not found (section 3.2), so which form it reads (u8 or float) is
  unverified.
- **Command blocks:** whether the other command blocks go to the GPU exactly
  as stored is unverified.
- **Write enables:** who writes `DEPTH_COLOR_MASK` byte 1 and `STENCIL_TEST`
  byte 1 from Depth.Flags bit 1 is unverified.
- **Light Flags bits:** the meaning of Lighting.Flags bits 2 and 3. SPICA's
  enum gives bit 2 two names, and HOME has 0xA (bits 1 and 3) with a Dist0 LUT.
- **FragmentShader +0x14:** SPICA calls it TranslucencyKind; its meaning is
  unverified.
- **Ambient alpha:** the u8 Ambient alpha (0 in `mt_BG`).
- **TexCoordConfig:** only value 0 is seen; the other values' meaning is
  unverified.
- **Unseen modes:** DccSoftImage, 3dsMax with rotation, cube textures, and type
  0x20000021 have no sample in HOME.
- **Never populated:** ShaderParameters (SPICA: TODO), ProceduralTextureMapper,
  the metadata DICT, LightSetIndex and FogIndex usage.
- **Run-time fields:** CommandCache, DynamicAlloc, TextureObj, LocationFlag and
  MemoryArea are only ever 0 in files.
- **GL names:** the DMP names of the GLFormat and GLType constants.
- **HOME conditions:** the conditions that pick the theme block or the saved
  defaults (byte +6), and the theme-texture or restore path (byte +0x1F1).
- **Theme mapping:** the exact mapping of 3dbrew's theme bytes to S[0..6].
- **Pixel content:** what `BG_DefBase_00` is for, and what the A4 mask
  `BG_CapMask_00` shows.
- **Text base:** HOME's text base 0x00100000 is assumed, not read from the
  exheader.

Scripts (dev only, never copying the inputs; run with `python3 -I`) are in the
session scratchpad under `kabegami-explore/cgfx-material-texture/`:

| Script | Use |
|---|---|
| `decode_mtob.py` | field-by-field MTOB decode |
| `decode_txob.py` | TXOB decode, sizes, alignment, data hashes |
| `hash_check.py`, `hash_variants.py`, `fragop_variants.py`, `blend_variants.py`, `blend_hidden.py` | hash recipes |
| `texmtx_check.py` | texture matrix formulas |
| `field_stats.py` | spreads across the HOME materials |
| `strrefs.py` | who references a name |
| `patch_mapper.py` | section 3.1 table |
| `blz.py`, `disasm.py`, `callers.py` | HOME code, in memory |
| `pica.py`, `parse_regs.py` | 3dbrew register names |

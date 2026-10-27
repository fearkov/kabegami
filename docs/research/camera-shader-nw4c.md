# BannerCamera, DefaultShader and the NW4C draw conventions

HOME Menu EUR 11.17, title 0004003000009802 v29696.

## 1. Purpose

Kabegami draws `BannerBG` through zakuro-gpu. To match the console it needs
three things from HOME:

- the same projection and view matrices HOME builds from `BannerCamera`;
- the same DefaultShader program;
- the same values in that program's uniforms.

This spec has three parts:

1. Part 1 (section 3) decodes `3D/BannerCamera_LZ.bin`. It then shows, from
   HOME's own code, how NW4C turns the camera into matrices: the 3DS screen
   rotation, the stereo path and what `WScale` does to the depth map.
2. Part 2 (section 4) decodes `3D/nwgfx_DefaultShader_LZ.bin`: the SHDR
   object, its program descriptions, the DVLB and the four DVLEs.
3. Part 3 (section 5) covers the NW4C conventions: which program a material
   runs, how vertex attributes reach the shader inputs, and what HOME writes
   into every uniform at draw time. `mt_BG` is the worked example.

Neighbouring specs:

- `cgfx-model.md`: containers, DICT, CMDL/SOBJ, skeleton;
- `cgfx-material-texture.md`: MTOB, texture coordinators, TEV;
- `cgfx-animation.md`: the CANM clips;
- `zakuro-gpu-recipe.md`: registers and the library.

## 2. Sources and conventions

### 2.1 Files

All files are under `~/.cache/kabegami-dev/home-eur/`. Every `*_LZ.bin` is
LZ11. The offsets below are into the decompressed bytes.

| File | Compressed | SHA-256 (compressed) | Decompressed | SHA-256 (decompressed) |
|---|---|---|---|---|
| `romfs/3D/BannerCamera_LZ.bin` | 500 | `54df078dfb80a7fe55b391d10993353fe3b9ae2135a87f204b1ad8f641cc4ad9` | 1158 | `19d1009bc472a34626a10ec903d689fbd4b3b6951addc28beaa4f155db9bf897` |
| `romfs/3D/nwgfx_DefaultShader_LZ.bin` | 3607 | `e5240dd7d4e6dcd9fd362980a60b6fe093d9bfbde038c6182a948344808ce592` | 10896 | `cfe8d7421feceacdcd3c5f675e4a152f164107afb8abb903bc2b2c647bf65a48` |
| `romfs/3D/BannerBG_LZ.bin` | 7212 | `27d58c2113d2c2d46e3bcc36bb2ae56c35e19d9823488287b6759df998108711` | 86080 | `092c8682d0cfabf0a1823a8e3a2c12556515c437afba2aa6f6ac7fc4d5e34595` |
| `romfs/3D/BannerBGmask_LZ.bin` | 8354 | `d8c3cf350e2af35263f640a80144a40aa1d9a1b878f1b6eb872be0b4ec669541` | 23360 | `d9139c60d5d608704b26252699a275f034ba04640859e0a8b1b9037047e1d233` |
| `exefs-code.bin` (HOME `.code`, BLZ) | 1393164 | `e77d417b8609f891aa5a9ba20e26a087afdb254ef6e7f27fd8a4162b24cd6adc` | 2367488 | `5f4d8d0e80d8e7d6fafcefa0630c5106473ade4658585cfb499a6ec3324d89a7` |

`BannerFrame_LZ.bin`, `BannerFolder_LZ.bin`, `Textures_LZ.bin`, the other
CGFX files and the `shaders/*.shbin` files were used for comparison only
(sections 3.1, 4.7, 5.1).

### 2.2 HOME code

The `.code` is backward-LZ ("BLZ") compressed. It was decompressed in memory
and disassembled as ARM with capstone 5.0.7.

- Addresses are virtual addresses (VA) with the load base 0x00100000. Byte
  offset = VA − 0x100000 in the decompressed image.
- A claim tagged `[HOME VA …]` was read in that function's disassembly.
- Function names in *italics* are the likely NW4C/CTR-SDK names. They are
  **guesses**: the binary has no symbols.

### 2.3 Conventions

- Little-endian. `f32` is IEEE single.
- `ptr` is self-relative: target = address of the field + its signed value.
  0 = null.
- `+0x..` is relative to an object's first word, its type id. `@0x..` is an
  absolute offset in the decompressed file.
- Matrices are row-major, with column vectors: p' = M · p. A 3x4 is three
  rows of four floats, so the translation is column 3.
- Source tags:
  - `[file]` read from the bytes;
  - `[HOME VA …]` read in HOME's code;
  - `[SPICA …]` SPICA (Unlicense), clone in the session's `refs/SPICA`;
  - `[3dbrew]`;
  - `[libctru]` libctru `shbin.c` (zlib), used for its output-map rule only;
  - **unverified** / **guess** where nothing confirmed it.

### 2.4 Scripts

The scripts are in the session scratchpad
`kabegami-explore/camera-shader-nw4c/`. Run them with `python3 -I`.

| Script | What it does |
|---|---|
| `cgfxlib.py` | LZ11, pointers, DICT and DATA helpers |
| `decode_camera.py` | every CCAM field with its offset |
| `camera_matrices.py` | BannerCamera's matrices, computed in float32 the way HOME computes them |
| `sincos_table.py` | checks how HOME's sine table is built |
| `decode_shdr.py` | SHDR object and program descriptions |
| `decode_dvlb.py` | DVLB, DVLP, DVLEs; also takes a `.shbin` path |
| `outmap.py` | output-map registers per DVLE |
| `picadis.py` | PICA200 disassembler, for analysis only; its output is not in this spec |
| `decode_material.py`, `decode_model.py` | the shader-relevant MTOB/CMDL/SOBJ fields |
| `blz.py`, `disasm.py`, `code_refs.py`, `arm_xrefs.py`, `xref_uses.py`, `reg_track.py`, `grep_insn.py`, `code_strings.py` | HOME code tools |

## 3. Part 1: the camera (`BannerCamera_LZ.bin`)

### 3.1 Container

Header [file]: magic `CGFX`, BOM bytes `FF FE`, header size 0x14, revision
0x05000000, file size 1158, 1 block (DATA only, no IMAG). DATA is @0x14, size
0x472.

DATA holds **15** (count, ptr) slots at DATA+8+8·i, not 16. In all 18 CGFX
files of this RomFS the first DICT starts right after slot 14, @0x94 = 0x1C +
15·8 [file]. `cgfx-model.md` §4 found the same.

- SPICA's `Gfx` class declares 16 dictionaries: it adds `FogAnimations`
  before `Emitters` [SPICA `Formats/CtrGfx/Gfx.cs`].
- 3dbrew's CGFX page lists 16 slots, the last "Unknown" [3dbrew].
- Neither layout fits these files.

| Slot | Contents | Verified in HOME files |
|---|---|---|
| 0 | models | yes (CMDL, 15 files) |
| 1 | textures | yes (TXOB) |
| 2 | LUTs | yes (LUTS, 10 files) |
| 3 | materials | **unverified**, empty in every HOME file |
| 4 | shaders | yes (SHDR, `nwgfx_DefaultShader_LZ.bin` only) |
| 5 | cameras | yes (CCAM, `BannerCamera_LZ.bin` only) |
| 6 | lights | yes (CFLT, 11 files) |
| 7 | fogs | **unverified**, empty everywhere |
| 8 | environments | yes (CENV, 11 files) |
| 9 | skeletal animations | yes (CANM) |
| 10 | material animations | yes (CANM) |
| 11–14 | visibility, camera, light animations; emitters | **unverified**, empty everywhere; order from 3dbrew |

BannerCamera uses slot 5 only:

- count @0x44 = 1;
- DICT @0x94, size 0x2C, one entry;
- the entry names `BannerCamera` (string @0x3D0) and points to the object
  @0xC0.

HOME takes the first camera **by position**, not by name [HOME VA 0x24F4CC]:

- it reads the slot count at file +0x44;
- it reads the DICT pointer at +0x48;
- it reads the first entry's object pointer at DICT+0x28.

### 3.2 CCAM object (@0xC0, 0xC8 bytes)

The layout is SPICA's `GfxCamera`, which derives from `GfxNodeTransform`.
HOME's camera builder checks the type id and reads +0xB4, +0xB8, +0xBC, +0xC0
and +0xC4 [HOME VA 0x19BA98].

| Off | @ | Type | Meaning | BannerCamera |
|---|---|---|---|---|
| +0x00 | 0xC0 | u32 | type id, camera | 0x4000000A (HOME VA 0x19BAB4 tests it) |
| +0x04 | 0xC4 | char[4] | magic | `CCAM` |
| +0x08 | 0xC8 | u32 | revision | 0x06000000 |
| +0x0C | 0xCC | ptr | name | → @0x3D0 `BannerCamera` |
| +0x10 | 0xD0 | u32 | metadata count | 0 |
| +0x14 | 0xD4 | ptr | metadata DICT | null |
| +0x18 | 0xD8 | u32 | branch-visible bits (bit 0) [SPICA `GfxNode`] | 1 |
| +0x1C | 0xDC | u32 | IsBranchVisible (bool) | 1 |
| +0x20 | 0xE0 | u32 | child count | 0 |
| +0x24 | 0xE4 | ptr | children | null |
| +0x28 | 0xE8 | u32 | animation group count | 1 |
| +0x2C | 0xEC | ptr | animation group DICT | → @0x188 (section 3.5) |
| +0x30 | 0xF0 | f32×3 | TransformScale | (1, 1, 1) |
| +0x3C | 0xFC | f32×3 | TransformRotation, radians | (0, 0, 0) |
| +0x48 | 0x108 | f32×3 | TransformTranslation, the camera position | (0, 1, 44.786) (z bits 0x423324DD = 44.7859993) |
| +0x54 | 0x114 | f32×12 | LocalTransform 3x4 | identity, no translation |
| +0x84 | 0x144 | f32×12 | WorldTransform 3x4 | identity |
| +0xB4 | 0x174 | u32 | ViewType: 0 aim, 1 look-at, 2 rotate | 0 (aim) |
| +0xB8 | 0x178 | u32 | ProjectionType: 0 perspective, 1 frustum, 2 orthographic | 0 (perspective) |
| +0xBC | 0x17C | ptr | view object | → @0x3A4 |
| +0xC0 | 0x180 | ptr | projection object | → @0x3BC |
| +0xC4 | 0x184 | f32 | WScale | 0.0 |

The type switches are in HOME [HOME VA 0x19BAE8–0x19BBC8]:

| Field value | View updater created by | Projection updater created by |
|---|---|---|
| 0 | VA 0x18FB0C (aim) | VA 0x193BD0 (perspective) |
| 1 | VA 0x1905D4 (look-at) | VA 0x1929DC (frustum) |
| 2 | VA 0x18D66C (rotate) | VA 0x18FE80 (orthographic) |

The updates these produce match SPICA's enum order (section 3.7).

The file's two matrices are identity although the translation is (0, 1,
44.786). The exporter did not fill them. HOME takes the camera position from
the run-time world matrix (section 3.7). That this world matrix is rebuilt
from +0x30/+0x3C/+0x48 is **inferred**:

- the camera would otherwise sit on its target;
- the animation group's `Transform` member covers exactly +0x30..+0x53.

### 3.3 View object (@0x3A4, aim target, 0x18 bytes)

| Off | @ | Type | Meaning | BannerCamera |
|---|---|---|---|---|
| +0x00 | 0x3A4 | u32 | type id: 0x80000000 aim, 0x40000000 look-at, 0x20000000 rotate [SPICA `GfxCameraView`] | 0x80000000 |
| +0x04 | 0x3A8 | u32 | flags. Bit 0: inherit target rotation; bit 1: inherit target translation [SPICA; HOME VA 0x18FB80 tests the low 2 bits] | 0 |
| +0x08 | 0x3AC | f32×3 | target position | (0, 1, 0) |
| +0x14 | 0x3B8 | f32 | twist, radians | 0 |

The other two view types appear in no HOME file.

- **Look-at**: +0x00 = 0x40000000, +0x04 flags, +0x08 target, +0x14 up
  vector. Flag bit 0 inherits up rotation, bit 1 target rotation, bit 2
  target translation [SPICA `GfxCameraViewLookAt*`; HOME VA 0x190620 reads
  +0x04, +0x08, +0x14].
- **Rotate**: +0x00 = 0x20000000, +0x04 IsInheritingRotation (u32 bool),
  +0x08 rotation, three radians [SPICA `GfxCameraViewRotation`; HOME VA
  0x18D6B8 reads +0x04 bit 0 and +0x08].

### 3.4 Projection object (@0x3BC, perspective, 0x14 bytes)

| Off | @ | Type | Meaning | BannerCamera |
|---|---|---|---|---|
| +0x00 | 0x3BC | u32 | type id: 0x20000000 perspective, 0x40000000 orthographic, 0x80000000 frustum [SPICA `GfxCameraProjection`; HOME VA 0x10B7B8 tests 0x20000000] | 0x20000000 |
| +0x04 | 0x3C0 | f32 | near | 26.5 (0x41D40000) |
| +0x08 | 0x3C4 | f32 | far | 1000.0 (0x447A0000) |
| +0x0C | 0x3C8 | f32 | aspect, width / height | 1.6666666 (0x3FD55555, 5/3 = 400/240) |
| +0x10 | 0x3CC | f32 | fovy, radians, full vertical angle | 0.5235988 (0x3F060A92 = 30.0°) |

Frustum and orthographic objects share one layout. HOME's two updaters read
exactly these offsets [SPICA `GfxCameraProjectionOrthogonal`; HOME VA
0x192A44 and 0x18FEE8]:

| Off | Meaning |
|---|---|
| +0x00 | type id |
| +0x04 | near |
| +0x08 | far |
| +0x0C | aspect |
| +0x10 | height |
| +0x14 | centre x |
| +0x18 | centre y |

For the frustum, height and centre are at the near plane.

### 3.5 Animation group `CameraAnimation` (DICT @0x188, object @0x1B4)

| Field | @ | Value |
|---|---|---|
| type id | 0x1B4 | 0x80000000 |
| flags | 0x1B8 | 0 |
| name | 0x1BC | → `CameraAnimation` |
| member type | 0x1C0 | 5 |
| element count | 0x1C4 | 7 |
| element DICT | 0x1C8 | → @0x1E4 |
| BlendOperationTypes count | 0x1CC | 3 |
| BlendOperationTypes values | @0x1D8 | 8, 6, 2 |
| evaluation timing | 0x1D4 | 0 |

The element layout differs from SPICA's `GfxAnimGroupElement`. Within an
element:

- +0x00: type id, 0x00800000 transform, 0x00400000 view, 0x00200000
  projection;
- +0x04: name;
- +0x08, +0x0C: 0, meaning unknown;
- +0x10: the member's byte offset inside its target object;
- +0x14: the member's size in bytes;
- +0x18: blend-operation index.

The offsets were checked against sections 3.2–3.4 [file]:

| Element | @ | Target offset, size | Target | Blend index |
|---|---|---|---|---|
| `Transform` | 0x270 | 0x30, 36 | camera +0x30..+0x53 (S, R, T) | 0 |
| `ViewUpdater.TargetPosition` | 0x29C | 0x08, 12 | view +0x08 | 1 |
| `ViewUpdater.Twist` | 0x2C8 | 0x14, 4 | view +0x14 | 2 |
| `ProjectionUpdater.Near` | 0x2F4 | 0x04, 4 | projection +0x04 | 2 |
| `ProjectionUpdater.Far` | 0x320 | 0x08, 4 | projection +0x08 | 2 |
| `ProjectionUpdater.Fovy` | 0x34C | 0x10, 4 | projection +0x10 | 2 |
| `ProjectionUpdater.AspectRatio` | 0x378 | 0x0C, 4 | projection +0x0C | 2 |

No HOME CGFX has a camera animation: slot 12 is empty everywhere. These
members only say what a camera clip could drive.

### 3.6 What the camera sees (computed)

At the plane z = 0 the camera sees, with the eye at distance 44.786 and
fovy 30°:

- half-height 44.786 · tan 15° = 12.0004;
- half-width 20.0006 (via aspect 5/3);
- so x ∈ [−20.0006, 20.0006] and y ∈ [1 − 12.0004, 1 + 12.0004];
- that is 40 × 24 units on a 400 × 240 screen: **10 pixels per unit**.

That plane lands at depth −z/w = 0.41941 (section 3.7.6). The z = 0 placement
assumes `BG` is at rest, i.e. world identity; see `cgfx-model.md` §14.

### 3.7 How HOME builds the matrices

This is all from HOME 11.17's code, which links NW4C gfx and the CTR SDK
math. SPICA only covers the view formulas (section 3.7.8).

#### 3.7.1 Loading and the pivot

`3D/BannerCamera_LZ.bin` is loaded by VA 0x24F430 (path, scene-table index 1,
camera slot 0), called at VA 0x287F04.

1. *Camera::Create* (VA 0x19BA98) allocates the 0x2A4-byte camera. It builds
   one view updater and one projection updater (section 3.2) and passes
   WScale to the constructor (VA 0x11B990).

   | Run-time camera offset | Holds |
   |---|---|
   | +0x90 | world matrix |
   | +0x150 | view matrix (3x4) |
   | +0x180 | inverse view |
   | +0x1B0 | projection (4x4) |
   | +0x1F0 | 4x4 derived from the projection, likely its inverse (**unverified**) |
   | +0x230 | texture-projection matrix (3x4) |
   | +0x260 | view updater |
   | +0x268 | projection updater |
   | +0x29C | WScale |

2. The perspective updater (0x20 bytes, vtable VA 0x31FFF0, creator VA
   0x193BD0):

   | Offset | Holds |
   |---|---|
   | +0x08 | u8 **pivot direction**, created 0 |
   | +0x0C..+0x18 | four 0.5 for the texture projection |
   | +0x1C | pointer to the file's projection object |

3. Right after creation, HOME sets the pivot (VA 0x24F568–0x24F574): if the
   byte at VA 0x32E738 is non-zero, then `camera->[+0x268]->[+8] = 1`.
   - That byte is 1 in the data segment.
   - No store to it was found: every literal-pool load of 0x32E738 was traced
     forward, and all of them only read it or write other fields.
   - So **BannerCamera's projection updater runs with pivot 1**.

The camera is stored in two places:

- the scene table @VA 0x344B60 + 0xC;
- render-context camera slot 0 (`[[VA 0x32E7C4] + 0xAC][0]`).

#### 3.7.2 Every frame

A loop over the 32 camera slots (VA 0x1038F0–0x103930) calls *UpdateCameraMatrix*
(VA 0x10A334) on each camera. In that function:

- eye = translation column of the run-time world matrix (camera +0x9C, +0xAC,
  +0xBC);
- the parent's world matrix is passed on, identity when there is no parent;
- view updater → camera +0x150;
- +0x180 = inverse of the view (VA 0x22F0C4); identity if singular;
- projection updater → +0x1B0, with the texture projection to +0x230.

When the slot is BannerCamera's (`[VA 0x344B60]`), the loop also calls the
stereo update (section 3.7.5).

#### 3.7.3 View matrices

**Aim** (BannerCamera). With flags 0 the updater (VA 0x18FB58) calls
*MTX34LookAt(eye, twist, target)* (VA 0x1AD1C0):

    z = normalize(eye − target)
    v = normalize((z.z, 0, −z.x))          # level "right"
    u = z × v                              # "up"
    (s, c) = SinCosFIdx(twist · K)         # K = 256/2π, section 3.7.6
    x = c·v + s·u
    y = c·u − s·v
    V = [ x  −x·eye ]
        [ y  −y·eye ]
        [ z  −z·eye ]

- If eye − target has x = z = 0 (looking straight up or down), the function
  writes a fixed axis-aligned matrix instead.
- With inherit flags (bit 0 target rotation, bit 1 target translation), the
  target and the twist-derived up vector go through the parent's rotation,
  and the target gets eye added. A look-at follows (VA 0x18FB88–0x18FE18).

**Look-at** (VA 0x190620):

    z = normalize(eye − target')
    x = normalize(up' × z)
    y = z × x
    V = [x | −x·eye; y | −y·eye; z | −z·eye]

up' and target' have the inherit flags applied: bit 0 rotates up, bit 1
rotates the target, bit 2 adds eye to the target.

**Rotate** (VA 0x18D6B8, flag clear):

    V = [ Cᵀ | −Cᵀ·eye ]
    C = Ry(ry) · Rx(rx) · Rz(rz)

Sines and cosines come from SinCosFIdx. The camera looks down its own −z.

#### 3.7.4 Projection matrices

**Perspective** (*PerspectiveProjectionUpdater::Update*, VA 0x193C38, vtable
slot 3):

    (s, c) = SinCosFIdx(fovy · 0.5 · K)
    cot = c / s
    inv = 1 / (far − near)
    P = [ cot/aspect  0     0          0              ]
        [ 0           cot   0          0              ]
        [ 0           0     far·inv    (far·near)·inv ]
        [ 0           0     −1         0              ]

- At view z = −near, z/w = 0; at view z = −far, z/w = −1. NW4C's clip depth
  runs **0 at near to −1 at far**, not OpenGL's [−1, 1]. The PICA keeps
  −w ≤ z ≤ 0 (`zakuro-gpu-recipe.md` §6.2).
- The same update also writes the texture-projection matrix, with no pivot:

      [ 0.5·cot/aspect  0         −0.5  0 ]
      [ 0               0.5·cot   −0.5  0 ]
      [ 0               0         −1    0 ]

  It is used only by projection-mapped texture coordinators.

**Frustum** (VA 0x192A44), with h = height and (cx, cy) = centre:

    l = cx − h·aspect/2,  r = cx + h·aspect/2
    b = cy − h/2,         t = cy + h/2
    P = [ 2n/(r−l)  0         (r+l)/(r−l)  0         ]
        [ 0         2n/(t−b)  (t+b)/(t−b)  0         ]
        [ 0         0         f/(f−n)      f·n/(f−n) ]
        [ 0         0         −1           0         ]

This is the same matrix as *MTX44Frustum* (VA 0x22FD40).

**Orthographic** (VA 0x18FEE8), with the same l, r, b, t:

    P = [ 2/(r−l)  0        0        −(r+l)/(r−l) ]
        [ 0        2/(t−b)  0        −(t+b)/(t−b) ]
        [ 0        0        1/(f−n)  n/(f−n)      ]
        [ 0        0        0        1            ]

Near maps to z = 0 and far to −1.

**Pivot: the 3DS screen rotation.** All three updaters, and *MTX44Pivot* (VA
0x22FC84), apply the pivot byte the same way:

| Pivot | Rows 0 and 1 become | Effect |
|---|---|---|
| 0, 4 | unchanged | none |
| 1 | row0' = row1, row1' = −row0 | clip.x' = clip.y, clip.y' = −clip.x |
| 2 | row0' = −row0, row1' = −row1 | 180° |
| 3 | row0' = −row1, row1' = row0 | clip.x' = −clip.y, clip.y' = clip.x |

- The names are **guesses**: probably NW4C/nn::math `PivotDirection` (NONE,
  UPSIDE_TO_TOP, …). Why 4 does nothing is **unknown**.
- With pivot 1, clip.x = cot · y_view and clip.y = −(cot/aspect) · x_view.
  Screen-up maps to clip +x, the 240-pixel axis of the portrait top-screen
  buffer. Screen-right maps to clip −y, its 400-pixel axis.
- The rotation lives in the projection matrix. The view matrix stays upright.
- That NW4C titles show upright in Zakuro means this convention plus Zakuro's
  top-screen presentation gives an upright image. The pixel-level readback
  mapping was **not checked here**.

#### 3.7.5 The stereo path

At init, HOME creates a StereoCamera-like object (0x64 bytes) at `[VA
0x32E738 + 0x3C]` and two extra cameras at `+0x34` and `+0x38` (VA 0x10B414).

Each frame, VA 0x10B780 takes BannerCamera's view (+0x150) and rebuilds an
**unpivoted** perspective from the projection object, with the same float
steps. It then calls *nn::ulcd CalculateMatrices* (VA 0x10F764):

- depth level = run-time camera +0x7C plus `[0x32E738+8]`. When byte
  `0x32E738+3` is set, −6.0 replaces that offset.
  - What +0x7C holds is **unverified**. If it is the eye's z (44.786), the
    z = 0 wallpaper plane has zero parallax.
- factor = 1.0, or 0.62 in the −6.0 mode.
- The 3D slider is read from VA 0x1FF81080 and forced to 0 when the byte at
  0x1FF81084 is non-zero (VA 0x10F900).

CalculateMatrices works in four steps:

1. it recovers l, r, b, t, n, f from P (n = P23/P22, f = P23/(P22 − 1),
   r − l = 2n/P00, …);
2. it shifts each eye's frustum by the slider;
3. it builds each eye with *MTX44Frustum* followed by *MTX44Pivot(…, 1)*;
4. it writes the eye projection to that camera's +0x1B0 and the view to its
   +0x150.

With the slider at 0 the eye projections differ from P by one float32 ulp in
two entries (computed by `camera_matrices.py`):

- P11: 0x406ED9E0 vs 0x406ED9E1;
- P23: 0x41D9C55B vs 0x41D9C55C.

The shader unit's float registers are float24 [3dbrew]. Whether the float32
words are truncated or rounded to 16 mantissa bits, each pair gives the same
float24 value. zakuro-gpu keeps float32 uniforms as float32 (`shader/mod.rs`
`set_float_uniform`), so there the difference survives.

Which camera's matrices are active while `BannerBG` draws on the top screen
is **unverified**: the base camera or the left/right eye cameras (the likely
case for a 3D top screen).

#### 3.7.6 SinCosFIdx and the BannerCamera numbers

All angles go through *SinCosFIdx* (VA 0x22B4E4), not `sinf`/`tanf`:

1. fidx = radians · K, where K = 40.743664 (bits 0x4222F983, 256/2π rounded
   to float).
2. a = |fidx|. When a ≥ 65536 the function first calls VA 0x2281FC, probably
   an fmod (**unverified**).
3. i = trunc(a) & 0xFFFF and frac = a − i.
4. e = table[i & 0xFF].
5. sin = e.sin + frac·e.dsin and cos = e.cos + frac·e.dcos. Each multiply is
   rounded before the add (VFP `vmla` is not fused).
6. sin is negated when fidx < 0.

The table at VA 0x30C7D4 holds 256 × (sin, cos, dsin, dcos) float32, 4096
bytes, SHA-256
`f2433a2419e313eb7d18fc5f2682c7be465840bf0f120094ab2b00c79dcca7a8`.

- Entry i is float32(sin 2πi/256), float32(cos 2πi/256) and the two
  differences to entry i+1, each taken in double precision and then rounded
  to float32.
- Three entries are exact zeros instead of the ~1e-16 doubles give: entry 64
  cos, entry 128 sin, entry 192 cos.
- With that rule all 256 entries match bit for bit (`sincos_table.py`). The
  table can be regenerated; there is no need to copy it.

For fovy/2 = 15°, fidx = 10.666667, which gives:

| Value | Result |
|---|---|
| sin | 0.25880191 |
| cos | 0.96586114 |
| cot | 3.7320480 (bits 0x406ED9E0) |
| exact cot 15° | 3.7320507 (relative difference 7e-7) |

Matching the console bit for bit therefore needs this sine table.

BannerCamera, computed in float32 steps from the file values (`camera_matrices.py`):

| Matrix | Row 0 | Row 1 | Row 2 | Row 3 |
|---|---|---|---|---|
| ViewMtx (V) | (1, 0, 0, −0) | (−0, 1, 0, −1) | (0, 0, 1, −44.786) | |
| P before the pivot | (2.2392290, 0, 0, 0) | (0, 3.7320480, 0, 0) | (0, 0, 1.0272213, 27.221365) | (0, 0, −1, 0) |
| **ProjMtx** = P with pivot 1 | (0, 3.7320480, 0, 0) | (−2.2392290, −0, −0, −0) | (0, 0, 1.0272213, 27.221365) | (0, 0, −1, 0) |

Bit patterns:

| Value | Bits |
|---|---|
| 2.2392290 | 0x400F4F87 |
| 3.7320480 | 0x406ED9E0 |
| 1.0272213 | 0x3F837BFD |
| 27.221365 | 0x41D9C55B |

In ProjMtx row 1, −0 sits in columns 1–3. The updater writes negative zero
there.

Checks against the plane z = 0:

| World point | Clip x/w, y/w, z/w |
|---|---|
| (−20, −11, 0), bottom left | (−0.99997, +0.99997, −0.41941) |
| (20, 13, 0), top right | (+0.99997, −0.99997, −0.41941) |

#### 3.7.7 Upload and depth map

**Uniform upload.** When the active camera changes, VA 0x109E40 writes:

| Command | Data |
|---|---|
| `GPUREG_VSH_FLOATUNIFORM_INDEX` (0x2C0) = 0x80000000 \| 90 | 12 words of camera +0x150 → **ViewMtx c90–c92** |
| index = 0x80000000 \| 86 | 16 words of +0x1B0 → **ProjMtx c86–c89** |
| the same pair on the geometry-shader port (0x290/0x291) | the same matrices |

- Bit 31 selects float32 mode.
- Matrix row k goes to register base + k: x, y, z, w = columns 0–3.
- Each row's words go out as **w, z, y, x** (VA 0x221CD8 for 3x4, VA
  0x221D5C for 4x4).

**WScale and the depth map.** When the active camera changes (VA
0x2366FC), HOME stores in VA 0x33C1B0:

- 0 when WScale = 0;
- otherwise float24(−WScale).

HOME's material setup code then refills a five-word command block at VA
0x382400 (VA 0x192D60–0x193024). Code at VA 0x18CB9C and 0x191F20 also reads
VA 0x33C1B0 and was not decoded. The block holds, in order:
DEPTHMAP_ENABLE value, its header 0x0001006D, DEPTHMAP_SCALE value, header
0x8017004D (writes 0x4D and 0x4E), DEPTHMAP_OFFSET value.

| Register | WScale = 0 (z-buffer) | WScale ≠ 0 (w-buffer) |
|---|---|---|
| 0x06D DEPTHMAP_ENABLE | 1 | 0 |
| 0x04D DEPTHMAP_SCALE | float24(depthNear − depthFar), precomputed at VA 0x33C174 by the viewport code at VA 0x13A478–0x13A528. That code clamps both to 0..1 and stores them at VA 0x33C1B4 and 0x33C1B8, whose initial values are 0.0 and 1.0. That gives **−1.0** | float24(−WScale) |
| 0x04E DEPTHMAP_OFFSET | float24(depthNear − polygon term) | float24(0 − polygon term) |

The polygon term is used only when polygon offset is on, with:

- units at VA 0x33C1C0;
- the flag at byte VA 0x33C1BC.

It is (depthNear − depthFar) · units · k for the z-buffer, or −units · k for
the w-buffer. k depends on the depth-buffer format at VA 0x33C1AC:

- 1.5259e-05 for D16 (0x81A5);
- otherwise 128 · 5.96e-08.

BannerCamera has WScale = 0, and `mt_BG` has polygon offset off
(`cgfx-material-texture.md` §2.4). So:

- the depth buffer is a z-buffer, scale −1, offset 0;
- depth = −z/w ∈ [0, 1], near 0, far 1;
- the wallpaper plane sits at depth 0.41941.

#### 3.7.8 Where SPICA agrees and where it does not

SPICA.Rendering `Camera.cs` builds the aim view the same way: the
`(EyeDir.Z, 0, −EyeDir.X)` twist axis and the up vector from cos/sin of the
twist.

For the projection it differs. Do not take Kabegami's projection from SPICA:

- it uses OpenTK `CreatePerspectiveFieldOfView`: OpenGL depth, the window's
  aspect, no pivot;
- it reads frustum projections as orthographic;
- `WScale` is stored and never used.

### 3.8 Unknowns (camera)

- The pivot enum names, and why value 4 means "none".
- Which camera's matrices are on the GPU when BannerBG draws: base, or the
  stereo eye cameras.
- What run-time camera +0x7C holds (the stereo depth level).
- Animation-group element fields +0x08/+0x0C, and what "member type 5" and
  BlendOperationTypes (8, 6, 2) mean.
- Whether HOME moves BannerCamera at run time. Nothing past the loader and
  the update loop was searched.
- DATA slots 3, 7 and 11–14, which no HOME file uses.

## 4. Part 2: the shader (`nwgfx_DefaultShader_LZ.bin`)

### 4.1 Container

Header [file]: revision 0x05000000, size 10896, 1 block. DATA size 0x2A7C.

- Slot 4 (shaders): count @0x3C = 1, DICT @0x94.
- The entry names `DefaultShader` (string @0x29F8) and points to the object
  @0xC0.
- HOME's code references the path `3D/nwgfx_DefaultShader_LZ.bin` (VA
  0x30657C, loaded at VA 0x10358C).

### 4.2 SHDR object (@0xC0)

| Off | @ | Type | Meaning | Value |
|---|---|---|---|---|
| +0x00 | 0xC0 | u32 | type id, binary shader. A material's shader *reference* is 0x80000001 | 0x80000002 |
| +0x04 | 0xC4 | char[4] | magic | `SHDR` |
| +0x08 | 0xC8 | u32 | revision | 0x05000000 |
| +0x0C | 0xCC | ptr | name | → @0x29F8 `DefaultShader` |
| +0x10 | 0xD0 | u32 | metadata count | 0 |
| +0x14 | 0xD4 | ptr | metadata DICT | null |
| +0x18 | 0xD8 | u32 | DVLB size, bytes | 0x26A4 |
| +0x1C | 0xDC | ptr | DVLB | → @0x104 (ends at 0x27A8) |
| +0x20 | 0xE0 | u32 | count of the next list | 4 |
| +0x24 | 0xE4 | ptr | one GL shader kind per DVLE | → @0x27A8: 0x8B31 ×4 (= GL_VERTEX_SHADER) |
| +0x28 | 0xE8 | u32 | program description count | 4 |
| +0x2C | 0xEC | ptr | list of pointers to program descriptions | → @0x27B8 → @0x27D8, @0x2860, @0x28E8, @0x2970 (HOME reads this list at shader+0x2C, VA 0x1981D8) |
| +0x30 | 0xF0 | u32 | count | 4 |
| +0x34 | 0xF4 | ptr | four null pointers @0x27C8, purpose **unknown** | |
| +0x38 | 0xF8 | u32 | **unknown** | 0 |
| +0x3C | 0xFC | u32 | **unknown**, perhaps a null pointer | 0 |
| +0x40 | 0x100 | u32 | **unknown** | 0 |

The DVLB (0x26A4 bytes) has SHA-256
`c147c5d96e5d56a026f8fdcb2115fdb7817c726cfc76774c8e9624025602a505`.

### 4.3 Program descriptions (0x88 bytes each)

The four descriptions differ only at +0x0C.

| Off | Type | Meaning | Value (all four) | Source |
|---|---|---|---|---|
| +0x00 | u32 | flags. Bit 0 rigid skinning supported, bit 1 smooth skinning, bit 2 hemisphere lighting, bit 3 geometry shader | 7 | names [SPICA `GfxShaderDescFlags`]; HOME tests bit 2 before setting IsHemiL (VA 0x18CA84) |
| +0x04 | u32 | **unknown** | 0 | |
| +0x08 | u32 | **unknown** | 0 | |
| +0x0C | s32 | vertex shader (DVLE) index | 0, 1, 2, 3 for descriptions 0–3 | [file] |
| +0x10 | s32 | geometry shader index, −1 = none | −1 | [file] |
| +0x14 | u32 | count of a list | 0 | HOME passes the list to VA 0x194AD0 with the material, probably to bind material shader parameters (**guess**) |
| +0x18 | ptr | that list | null | |
| +0x1C | ptr ×12 | attribute symbol names | `aPosition`, `aNormal`, `aTangent`, `aColor`, `aTexCoord0`, `aTexCoord1`, `aTexCoord2`, `aBoneIndex`, `aBoneWeight`, `aUserAttribute0`, `aUserAttribute1`, `aUserAttribute2` | [file] |
| +0x4C | s8 ×22 | **input register per attribute usage**, −1 = not bound. Indexed by usage: 0 Position … 8 BoneWeight, 9–20 UserAttribute0–11, 21 Interleave | 0 → v0 … 11 → v11; usages 12–21 → −1 | HOME reads `desc[0x4C + usage]` and skips attributes < 0 (VA 0x198404) |
| +0x62 | u8 ×2 | padding | 0 | |
| +0x64 | s32 | likely the maximum bone count (20 × 3 = 60 UnivReg registers) | 20 | **guess** |
| +0x68 | s32 | likely the maximum vertex-light count | 9 | HOME requires it > 0 before setting IsVertL (VA 0x18CA48); the meaning is a **guess** |
| +0x6C | s32 | likely UnivReg's register count | 60 | **guess** |
| +0x70 | u32 ×5 | **unknown** | 0 | |
| +0x84 | ptr | back-pointer to the owning SHDR | → @0xC0 | [file] |

### 4.4 DVLB and DVLP

| Field | @ | Value |
|---|---|---|
| DVLB magic, DVLE count | 0x104, 0x108 | `DVLB`, 4 |
| DVLE offsets (from the DVLB) | 0x10C–0x118 | 0xBC0, 0x12D8, 0x19C8, 0x2090 |
| DVLP magic | 0x11C | `DVLP` |
| DVLP version (u16), bytes 6–7 | 0x120 | 0x0302, 0, 0 |
| code offset (from the DVLP), size | 0x124, 0x128 | +0x28 (@0x144), 481 words = 1924 bytes. SHA-256 `e79aaac9035ee4d8db2afa4a0a08865b7ddccc2bb0175c5ebe35173f594f006b` |
| operand-descriptor offset, count | 0x12C, 0x130 | +0x7AC (@0x8C8), 86 entries × 8 bytes. SHA-256 `287843de4a5d38406b9467a7c805fb05201b8fc189b1a94ed6e52a14ddc26a36` |
| unknown table offset, count | 0x134, 0x138 | +0xA5C, 0 |
| file-name table offset, size | 0x13C, 0x140 | +0xA5C (@0xB78), 329 bytes |

The file-name table holds the shader sources' names: `gfx_DefaultShader0.vsh`,
`TransformMatrix.vsh`, `CalcQuaternion.vsh`, `CalcColor.vsh`,
`CalcVertexLighting.vsh`, `CalcHemiSphereLighting.vsh`, `CalcTexcoord0.vsh`,
`GetTexcoordSource.vsh`, `GenTexcoordReflection.vsh`,
`GenTexcoordSphereReflection.vsh`, `CalcTexcoord1.vsh`, `CalcTexcoord2.vsh`,
`gfx_DefaultShader1.vsh`, `gfx_DefaultShader2.vsh`, `gfx_DefaultShader3.vsh`.

### 4.5 The four DVLEs

All four share the 481-word code blob. Each DVLE is just an entry point.

| | DVLE 0 | DVLE 1 | DVLE 2 | DVLE 3 |
|---|---|---|---|---|
| DVLE @ | 0xCC4 | 0x13DC | 0x1ACC | 0x2194 |
| version, type (+0x04, +0x06) | 0x0302, 0 (vertex) | 0x0302, 0 | 0x0302, 0 | 0x0302, 0 |
| merge-outmaps flag (+0x07) | 0 | 0 | 0 | 0 |
| main, endmain (words, +0x08, +0x0C) | 0x000, 0x007 | 0x1D2, 0x1D8 | 0x1D8, 0x1DD | 0x1DD, 0x1E1 |
| input mask (+0x10) | 0x0FFF | 0x0FFF | 0x0FFF | 0x0FFF |
| output mask (+0x12) | 0x007F | 0x003F | 0x001F | 0x000F |
| GS fields (+0x14..+0x17) | 0 | 0 | 0 | 0 |
| constant table (+0x18 off, +0x1C n) | +0x40, 4 | +0x40, 4 | +0x40, 4 | +0x40, 4 |
| label table (+0x20, +0x24) | +0x90, 20 | +0x90, 19 | +0x90, 18 | +0x90, 14 |
| output table (+0x28, +0x2C) | +0x1D0, 8 | +0x1C0, 7 | +0x1B0, 6 | +0x170, 4 |
| uniform table (+0x30, +0x34) | +0x210, 45 | +0x1F8, 45 | +0x1E0, 45 | +0x190, 45 |
| symbol table (+0x38 off, +0x3C size) | +0x378, 927 | +0x360, 910 | +0x348, 893 | +0x2F8, 795 |
| `main` calls | transform, colour, texcoord 0, 1, 2 | transform, colour, texcoord 0, 1 | transform, colour, texcoord 0 | transform, colour |
| source (by code order, **inferred**) | `gfx_DefaultShader0.vsh` | `…1.vsh` | `…2.vsh` | `…3.vsh` |

DVLEs carry no name. HOME and NW4C refer to them by index (section 5.1).

**Constants**: the same four in each DVLE. Values are float24 in 32-bit
slots, 1.0 = 0x3F0000.

| Register | Value | Used for |
|---|---|---|
| c93 | (0, 1, 2, 3) | zeros, ones, ×3 for matrix rows |
| c94 | (0.125, 0.00390625, 0.5, 0.25) | 1/256 as a threshold in the quaternion code, 0.5 in env mapping |
| c95 | (3, 4, 5, 6) | mapping codes 3 and 4; 6 = registers per vertex light |
| b0 | true | an unconditional `jmpu` |

**Outputs** (output table, 8-byte entries):

- Entry layout: u16 type, u16 register, u16 mask, u16 unknown.
- The unknown u16 is 0x0038 in every DefaultShader and `cfl.shbin` entry,
  and 0 in HOME's lyt/font shaders.
- DVLE 0 has all eight entries; DVLEs 1–3 drop from the end.

| Register | Mask | Semantic | DVLEs |
|---|---|---|---|
| o0 | xyzw | position | 0–3 |
| o1 | xyzw | normal quaternion | 0–3 |
| o2 | xyzw | view vector (the map uses xyz) | 0–3 |
| o3 | xyzw | colour | 0–3 |
| o4 | xy | texcoord 0 | 0–2 |
| o4 | z | texcoord 0 w | 0–2 |
| o5 | xy | texcoord 1 | 0–1 |
| o6 | xy | texcoord 2 | 0 |

Output-map values from libctru's `DVLE_GenerateOutmap` rule (`outmap.py`).
That NW4C writes the same values is **unverified**; `zakuro-gpu-recipe.md`
§6.7 derives the same.

| DVLE | OUTMAP_TOTAL | O0–O6 | OUTATTR_MODE | OUTATTR_CLOCK |
|---|---|---|---|---|
| 0 | 7 | 0x03020100 0x07060504 0x1F141312 0x0B0A0908 0x1F100D0C 0x1F1F0F0E 0x1F1F1716 | 1 | 0x01010703 |
| 1 | 6 | the same without O6 | 1 | 0x01010303 |
| 2 | 5 | the same without O5, O6 | 1 | 0x01010103 |
| 3 | 4 | O0–O3 | 0 | 0x01000003 |

**Uniform symbol table**: the same 45 entries, in the same order, in all four
DVLEs.

- Entry layout: u32 symbol offset, u16 first register, u16 last register.
- Register numbering: 0x00–0x0F v, 0x10–0x6F c, 0x70–0x73 i, 0x78–0x87 b
  [3dbrew SHBIN].
- The names are the source's, truncated to 7 characters for uniforms. Inputs
  keep a declared-component suffix such as `.xyz`.

| Name | Registers | Raw | | Name | Registers | Raw |
|---|---|---|---|---|---|---|
| `aPosition.xyz` | v0 | 0x00 | | `TexMtx2` | c17–c18 | 0x21–0x22 |
| `aNormal.xyz` | v1 | 0x01 | | `TexTran` | c19 | 0x23 |
| `aTangent.xyz` | v2 | 0x02 | | `MatAmbi` | c20 | 0x24 |
| `aColor` | v3 | 0x03 | | `MatDiff` | c21 | 0x25 |
| `aTexCoord0.xy` | v4 | 0x04 | | `HslGCol` | c22 | 0x26 |
| `aTexCoord1.xy` | v5 | 0x05 | | `HslSCol` | c23 | 0x27 |
| `aTexCoord2.xy` | v6 | 0x06 | | `HslSDir` | c24 | 0x28 |
| `aBoneIndex` | v7 | 0x07 | | `UnivReg` | c25–c84 (60) | 0x29–0x64 |
| `aBoneWeight` | v8 | 0x08 | | `ProjMtx` | c86–c89 | 0x66–0x69 |
| `aUserAttribute0.xyz` | v9 | 0x09 | | `ViewMtx` | c90–c92 | 0x6A–0x6C |
| `aUserAttribute1.xyz` | v10 | 0x0A | | `IsSmoSk` | b1 | 0x79 |
| `aUserAttribute2.xyz` | v11 | 0x0B | | `IsRgdSk` | b2 | 0x7A |
| `WrldMtx` | c0–c2 | 0x10–0x12 | | `IsUniSc` | b3 | 0x7B |
| `NormMtx` | c3–c5 | 0x13–0x15 | | `UseNorM` | b4 | 0x7C |
| `PosOffs.xyz` | c6 | 0x16 | | `IsHemiL` | b5 | 0x7D |
| `IrScale` | c7–c9 | 0x17–0x19 | | `IsHemiO` | b6 | 0x7E |
| `TexcMap.xyz` | c10 | 0x1A | | `IsVertA` | b7 | 0x7F |
| `TexMtx0` | c11–c13 | 0x1B–0x1D | | `IsBoneW` | b8 | 0x80 |
| `TexMtx1` | c14–c16 | 0x1E–0x20 | | `UvMap0`, `UvMap1`, `UvMap2` | b9, b10, b11 | 0x81–0x83 |
| `IsVertL` | b12 | 0x84 | | `IsTex1`, `IsTex2` | b13, b14 | 0x85, 0x86 |
| `IsQuate` | b15 | 0x87 | | `LightCt` | i0 | 0x70 |

- c85 is not declared.
- c93–c95 and b0 are the constants above.
- `IsUniSc` (b3) and `aUserAttribute0–2` (v9–v11) are declared, but the code
  never reads them.

**Labels**:

- Entry layout: u16 id, u16 (always 1), u32 word, u32 size (−1 for `main` and
  `endmain`), u32 symbol.
- DVLE 0 has 20 labels: `main`, `endmain`, and these 18 routines.

| Routine | Word | Size |
|---|---|---|
| `l_internal_blend_vertex_p` | 0x007 | 5 |
| `l_internal_blend_vertex_pn` | 0x00C | 10 |
| `l_internal_blend_vertex_pnt` | 0x016 | 14 |
| `l_transform_matrix` | 0x024 | 175 |
| `l_internal_calc_quaternion_from_normal` | 0x0D3 | 17 |
| `…_from_normal_end` | 0x0E3 | 1 |
| `l_internal_calc_quaternion_from_tangent` | 0x0E4 | 76 |
| `l_full_quaternion_calc_fallback` | 0x109 | 39 |
| `l_full_quaternion_calc_end` | 0x12F | 1 |
| `l_calc_color` | 0x130 | 17 |
| `l_calc_vertex_lighting` | 0x141 | 46 |
| `l_calc_hemisphere_lighting` | 0x16F | 10 |
| `l_calc_texcoord0` | 0x179 | 27 |
| `l_get_texcoord_source` | 0x194 | 8 |
| `l_gen_texcoord_reflection` | 0x19C | 7 |
| `l_gen_texcoord_sphere_reflection` | 0x1A3 | 4 |
| `l_calc_texcoord1` | 0x1A7 | 26 |
| `l_calc_texcoord2` | 0x1C1 | 17 |

DVLEs 1–3 drop the texcoord labels their `main` does not call.

### 4.6 What the program computes

Read from a disassembly (`picadis.py`). No code is reproduced here.
`IrScale` is written as S0 = c7, S1 = c8, S2 = c9.

**Position and normals** (`l_transform_matrix`):

    p = S0.x · aPosition + PosOffs,  p.w = 1
    n = S0.y · aNormal
    t = S0.z · aTangent

Normals count as present when S0.y ≠ 0, tangents when S0.z ≠ 0.

Then one of three paths runs:

- **Smooth** (`IsSmoSk`): up to four bones, j = 0..3.
  - Bone j's matrix starts at register a_j = 3 · S2.x · aBoneIndex[j]. With
    `UseNorM`, its normal matrix is at 3 · (S2.x · aBoneIndex[j] + S2.z),
    i.e. after the bone matrices.
  - Weight w_j = S1.w · aBoneWeight[j]. Bone 3 is used only with `IsBoneW`.
    Bones 2 and 3 are skipped when their raw weight is 0.
  - p_m = Σ w_j · UnivReg[a_j..a_j+2] · p. Normals and tangents are blended
    the same way through the 3x3 part.
  - p_w = WrldMtx · p_m.
- **Rigid** (`IsRgdSk`):
  - a = 3 · (S2.x · aBoneIndex.x), plus 3·S2.z for the normal matrix with
    `UseNorM`.
  - p_w = WrldMtx · (UnivReg[a..a+2] · p).
  - normals: UnivReg[a′] 3x3 · n.
- **Neither**: p_w = UnivReg[0..2] · p; normals through the same 3x3.

All three paths end the same way:

    p_v = ViewMtx · p_w
    n_v = NormMtx(3x3) · n'
    o0 = ProjMtx · p_v
    o2 = −p_v

- Without normals, o1 = 0.
- With normals and `IsQuate`, o1 = a quaternion built from the view-space
  normal, and from the tangent too when tangents exist. Its exact component
  convention was **not checked** against the fragment lighting.
- With normals but no `IsQuate`, o1 = (1, 0, 0, 0) in (x, y, z, w) order.

**Colour** (`l_calc_color`):

1. r = (0, 0, 0, MatDiff.w).
2. If S0.w ≠ 0 (vertex colour present): vc = S0.w · aColor, then r.rgb =
   MatAmbi.w · vc.rgb. With `IsVertA`, r.a = MatDiff.w · vc.a.
3. `IsVertL` adds vertex lighting over LightCt + 1 lights. Each light is six
   registers counting down from UnivReg[54..59] (c79–c84): ambient, diffuse,
   position or direction (w = 0 directional), spot direction, distance
   attenuation, spot exponent and cutoff.
4. `IsHemiL` adds hemisphere lighting:
   - f = dot(HslSDir.xyz, n_v) · HslSDir.w + HslSDir.w;
   - colour += mix(HslGCol, HslSCol, f) · MatDiff;
   - with `IsHemiO`, the mix is first multiplied by r.a.
5. With no vertex colour and no lighting, r = MatDiff.
6. o3 = max(r, 0).

**Texture coordinates**: TexcMap.x/y/z hold one mapping code per coordinator.

- 0, 1, 2: the UV set: aTexCoord0, 1, 2 scaled by S1.x, S1.y, S1.z.
- 3: cube environment map.
- 4: sphere environment map.
- Anything else, without the UvMap bool: projection map.

Per coordinator:

- **Coordinator 0**:
  - with `UvMap0`: o4 = (TexMtx0[0]·(u,v,1,1), TexMtx0[1]·(u,v,1,1), 0, 0),
    using the rows' x, y, w, z components, so u' = m00·u + m01·v + m02 + m03.
    With a UV matrix m02 = 0, this is the usual affine map.
  - otherwise one of three:
    - cube (code 3): reflection vector times the three rows of TexMtx0, into
      o4.xyz;
    - sphere (code 4): n·0.5 + 0.5, times rows 0 and 1;
    - projection: TexMtx0 · p_w, then xy += TexTran.xy · z, with z as
      texcoord0 w.
- **Coordinator 1**:
  - with `UvMap1`: UV as above, through TexMtx1;
  - else with `IsTex1`: sphere env, or projection with a divide by z plus
    TexTran.zw;
  - else o5 = 0.
- **Coordinator 2**:
  - with `UvMap2`: UV through TexMtx2's two rows;
  - else with `IsTex2`: sphere env;
  - else o6 = 0.

### 4.7 Standalone `.shbin` files

None of these is the DefaultShader; BannerBG uses none of them. The files
are not compressed. Each holds one vertex DVLE.

| File | Size | Program | Uniforms |
|---|---|---|---|
| `cfl.shbin` | 692 | `VShader.vsh` (Mii) | `uProjMtx` c0–3, `uPosMtx` c4–6, `uNrmMtx` c7–9, `uTexMtx` c10–11 |
| `nwfont_RectDrawerShader.shbin` | 964 | `font_RectDrawerShader.vsh` | input `vVtxData` |
| `nwfont_TextWriterShader.shbin` | 484 | `font_TextWriterShader.vsh` | `uProjection` c0–3, `uModelView` c4–6 |
| `nwlyt_ConstColorShader.shbin` | 328 | `lyt_ConstColorShader.vsh` | `uProjection` c0–3, `uModelView` c4–7 |
| `nwlyt_PaneShader.shbin` | 1060 | `lyt_PaneShader.vsh` | `uProjection`, `uModelView`, `uTransform`, `uTexMtx0–2`, `uColor`, `uFrameSpec`, `uVertexColor`, `uVertexTexCoord0–2`, `uRcpTexSize0` |

| File | SHA-256 |
|---|---|
| `cfl.shbin` | `7abb80a751e51d7f409912cf23501b89cd885a2ec3013b2b3a2eecd401426f6f` |
| `nwfont_RectDrawerShader.shbin` | `0ca7566cab8f8559a954f1390d02d346deb26309728db5a9f1fec41e037bc1ea` |
| `nwfont_TextWriterShader.shbin` | `87e9a661a499dfe18818310d6758855eba5d56892fe266c155b18e3e0f4d881e` |
| `nwlyt_ConstColorShader.shbin` | `6e6d5058aa64c8201fbd487e635eec0de3549bb3b5fedd1eaa1aa284ad3ad330` |
| `nwlyt_PaneShader.shbin` | `4d41713c0d4cf1b3e89cd4db3da65cea2fd766f2345500ed0866e743ed207fbb` |

The lyt and font files have DVLE version 0x1002. `cfl.shbin` has 0x0302, like
DefaultShader.

### 4.8 Unknowns (shader)

- SHDR +0x30..+0x40, and description +0x04, +0x08, +0x70..+0x83.
- The meaning of description +0x64, +0x68, +0x6C beyond what section 4.3
  says.
- The output-entry u16 0x0038, the label u16 1, and the DVLP table at +0xA5C
  (count 0).
- Which source file each DVLE came from is inferred from code order, not
  stored.

## 5. Part 3: NW4C conventions at draw time

### 5.1 Which program a material runs

The chain is verified in HOME's code (VA 0x1981B8–0x1981F4, also VA
0x19D5FC–0x19D63C and VA 0x194F68–0x194FB8):

1. The material's +0x284 points to its ShaderReference (type 0x80000001,
   path `DefaultShader`).
2. VA 0x20AFA0 resolves the reference to the SHDR.
3. desc = SHDR[+0x2C] list[`material[+0x28C]`], where material +0x28C is
   ShaderProgramDescIndex.
4. desc +0x0C names the DVLE.

`mt_BG` has ShaderProgramDescIndex **0** (MTOB @0xC68 + 0x28C = @0xEF4) and no
shader parameters (+0x290 = 0, null). That gives description 0 → **DVLE 0**,
entry word 0, with all three texcoord outputs. This fits `mt_BG`'s
UsedTextureCoordsCount = 3 (+0x168).

Every material in every HOME CGFX file has index 0. That is 53 materials
with 0–3 texture coordinates, all on `DefaultShader` (`decode_material.py`
over the RomFS).

- HOME's material copy (VA 0x19C8E8) takes the index unchanged, and no other
  write to +0x28C was found in material code. DVLEs 1–3 are therefore never
  selected by HOME's files.
- DVLE 0 handles fewer coordinators through `IsTex1`, `IsTex2` and
  `UvMap0–2`: unused texcoord outputs are 0.
- The step from desc +0x0C to `GPUREG_VSH_ENTRYPOINT` was not traced. The
  value 0 and DVLE 0's main = 0 agree.

### 5.2 Vertex attributes → inputs, and defaults

**Binding.** HOME's mesh setup (VA 0x198300–0x1985CC) walks the shape's
vertex buffers. For each attribute it reads the usage at attribute +0x04 and
looks up `desc[0x4C + usage]`. A negative value drops the attribute.

| Usage | Attribute | Input | IrScale component that gets its scale |
|---|---|---|---|
| 0 | Position | v0 | c7.x |
| 1 | Normal | v1 | c7.y |
| 2 | Tangent | v2 | c7.z |
| 3 | Color | v3 | c7.w |
| 4 | TexCoord0 | v4 | c8.x |
| 5 | TexCoord1 | v5 | c8.y |
| 6 | TexCoord2 | v6 | c8.z |
| 7 | BoneIndex | v7 | none (c9.x is always 1.0, see section 5.3) |
| 8 | BoneWeight | v8 | c8.w |
| 9–11 | UserAttribute0–2 | v9–v11 | none |
| 12–21 | UserAttribute3–11, Interleave | not bound | |

The scales live in the mesh's `AttrScaleCommands` (mesh +0x30, 12 words). It
is a ready command:

- 0x2C0 = 0x80000007;
- then 0x2C1 × 8 words in w, z, y, x order for c7 and then c8;
- HOME copies it into each draw (VA 0x111A68).

How each scale is set:

- An attribute gets its file Scale (attribute +0x2C).
- A fixed attribute (vertex-buffer type 0x80000000) gets 1.0; its value goes
  to the GPU as a fixed attribute.
- **All scales start at 0** (VA 0x198320–0x19835C). An attribute the shape
  lacks keeps scale 0, and the shader reads S0.y/z/w = 0 as "no normals /
  tangents / colour".

The same pass sets two mesh flags (mesh +0x2C):

- bit 0: the Color attribute has 4 components;
- bit 1: BoneWeight has 4 components.

The draw turns them into `IsVertA` (b7) and `IsBoneW` (b8) (VA
0x111A84–0x111AB0).

The GPU's attribute loader does not normalise. A ubyte colour arrives as
0–255 and becomes 0–1 through c7.w = 1/255. That is why IrScale exists.

**Defaults for inputs nothing feeds.**

- HOME's per-mesh *deactivate* command (VA 0x19F3A0) does two things:
  - it writes the identity permutation `0x76543210`/`0xBA98` to 0x2BB/0x2BC;
  - it turns the attribute slots into fixed attributes with value **(0, 0,
    0, 1)**, FIXEDATTRIB data words 0x3F000000, 0, 0 (VA 0x19F4E8–0x19F504).
- So an input left unfed by the next mesh most likely reads (0, 0, 0, 1)
  (**inferred**: the matching *activate* commands were not decoded).
- zakuro-gpu gives (0, 0, 0, 0) to a register no attribute index maps to, and
  (0, 0, 0, 1) to a mapped one with no data (`raster.rs` `InputPlan::new`).
- The only consequence for BannerBG is v7.x = 0, which is what HOME's rigid
  path needs (section 5.5) in all of these cases.

### 5.3 Float and integer uniforms

| Register | Name | What HOME writes | Where it comes from |
|---|---|---|---|
| c0–c2 | WrldMtx | the model's world matrix (model +0x90) for a skeletal model whose skeleton has flag bit 0 set (skeleton +0x28). Otherwise **identity** | VA 0x11160C–0x11176C |
| c3–c5 | NormMtx | a 3x4 stored in the run-time model at +0x1C4. What it holds is **unverified**; the shader uses it as the view-space normal matrix | VA 0x111770 |
| c6 | PosOffs | shape PositionOffset (shape +0x20), w = 0 | VA 0x1114A4 |
| c7, c8 | IrScale[0], [1] | attribute scales (section 5.2) | mesh AttrScaleCommands |
| c9 | IrScale[2] | (1.0, 1.0, bone count of the current palette, 1.0). .x scales bone indices; .z offsets the normal-matrix palette when UseNorM is on | VA 0x11B154–0x11B180 |
| c10 | TexcMap | (code0, code1, code2, 0). UV: SourceCoordIndex as a float. Cube env: 3. Sphere env: 4. Projection or shadow: left 0 | VA 0x19E888, 0x19EB7C, 0x19EBCC, 0x19ECE0 |
| c11–c13 | TexMtx0 | rows 0–2 of coordinator 0's 3x4 matrix | VA 0x19EBE4–0x19EC9C |
| c14–c16 | TexMtx1 | rows 0–2 of coordinator 1 | the same; index 0x8000000B + 3k |
| c17–c18 | TexMtx2 | rows 0–1 of coordinator 2 (two rows only) | the same |
| c19 | TexTran | projection-map translation, written only when a coordinator is a projection or shadow map (mapping 3 or 4) | VA 0x19ECBC; the flag is set at VA 0x19E920 and 0x19EB74 |
| c20 | MatAmbi | material AmbientF (rgb, w = vertex-colour scale) | VA 0x19DD6C, color block +0x10 |
| c21 | MatDiff | material DiffuseF (rgba) | VA 0x19DD58, color block +0x20 |
| c22–c24 | HslGCol, HslSCol, HslSDir | hemisphere light ground colour, sky colour, sky direction (w = blend factor) | upload around VA 0x130000–0x130130; the contents are a **guess** from the shader math |
| c25–c84 | UnivReg | bone palette: bone k's matrix at c25+3k (section 5.4). When the model is not skeletal, or the sub-mesh has no bone list, c25–c27 = model world matrix (model +0x90). Vertex lights fill from the top (c79–c84 down) | VA 0x11AEC4, VA 0x111568–0x1115A0 |
| c86–c89 | ProjMtx | camera projection (+0x1B0), with the 3DS pivot | VA 0x109E40 |
| c90–c92 | ViewMtx | camera view (+0x150) | VA 0x109E40 |
| c93–c95 | constants | from the DVLE | |
| i0 | LightCt | vertex-light loop count | **not traced** |

**Texture matrices.** The coordinator matrix is recomputed from Scale,
Rotate and Translate whenever the coordinator's dirty byte (+0x24) is set
(test at VA 0x19E7BC, matrix by VA 0x19DAA4, then the byte is cleared).
Setting that byte is presumably the animation binding's job (**not
traced**). The matrix is:

    row0 = ( Sx·c,  −Sx·s,  0,  t0 )
    row1 = ( Sy·s,   Sy·c,  0,  t1 )
    row2 = (  0,      0,    1,  0  )

with (s, c) from SinCosFIdx(R·K). HOME's code verifies the translation terms
for all three modes:

| Mode | t0 | t1 |
|---|---|---|
| 0 Maya | Sx·(0.5s − 0.5c + 0.5 − Tx) | Sy·(−0.5s − 0.5c + 0.5 − Ty) |
| 1 SoftImage | Sx·(−c·Tx − s·Ty) | Sy·(s·Tx − c·Ty) |
| 2 3ds Max | Sx·c·(−Tx − 0.5) − Sx·s·(Ty − 0.5) + 0.5 | Sy·s·(−Tx − 0.5) + Sy·c·(Ty − 0.5) + 0.5 |

**One correction to SPICA:** for **SoftImage** HOME flips the rotation part:
row0 = (Sx·c, +Sx·s, …) and row1 = (−Sy·s, Sy·c, …). SPICA uses the Maya/Max
signs for every mode. This refines `cgfx-material-texture.md` §2.6, which had
SoftImage as unverified. No HOME file uses SoftImage.

### 5.4 Bool uniforms (`GPUREG_VSH_BOOLUNIFORM`)

HOME keeps a shadow of the bool register (render state +0x14) and sends it
before each sub-mesh (VA 0x11B124).

| Bit | Name | Set when | Source |
|---|---|---|---|
| b0 | (constant) | always true | DVLE |
| b1 | IsSmoSk | the sub-mesh's skinning is Smooth (2) | VA 0x11AEE0–0x11AF14 |
| b2 | IsRgdSk | the sub-mesh has a bone list and skinning is **not** Smooth. Skinning None counts as rigid; cleared when there is no bone list | VA 0x11AEE0–0x11AF14, VA 0x111574 |
| b3 | IsUniSc | never read by the code; value **unknown** | |
| b4 | UseNorM | cleared by the set-model function (VA 0x1117A4); no setter found | |
| b5 | IsHemiL | description flag bit 2, a non-null pointer at the light state +0x04 (presumably the hemisphere light), and material flag bit 2 | VA 0x18CA80–0x18CAE0 |
| b6 | IsHemiO | with b5: material flag bit 3 | VA 0x18CAD8 |
| b7 | IsVertA | the Color attribute has 4 components | VA 0x111A8C |
| b8 | IsBoneW | BoneWeight has 4 components | VA 0x111AA4 |
| b9–b11 | UvMap0–2 | coordinator k is a UV map | VA 0x19E8A0 |
| b12 | IsVertL | description +0x68 > 0, a count at the light state +0xA0 > 0 (presumably vertex lights), and material flag bit 1. When +0x68 ≤ 0 the bit is left as it was | VA 0x18CA48–0x18CA7C |
| b13 | IsTex1 | UsedTextureCoordsCount > 1 | VA 0x19E750–0x19E75C |
| b14 | IsTex2 | UsedTextureCoordsCount > 2 | VA 0x19E764–0x19E770 |
| b15 | IsQuate | material flag bit 0 (fragment lighting) and a non-zero count at the light state +0xA4 (presumably fragment lights). The same code writes the fragment-lighting enable registers 0x08F and 0x1C6 | VA 0x18C9C8–0x18CA1C |

The material flag bits follow SPICA's `GfxMaterialFlags`: 0 fragment
lighting, 1 vertex lighting, 2 hemisphere lighting, 3 hemisphere occlusion,
4 fog.

**Bone palette** (VA 0x11AEC4):

- palette entry i = the sub-mesh's i-th bone index;
- its matrix goes to c25+3i..c27+3i;
- for non-smooth sub-meshes, the matrix is the bone's world matrix (skeleton
  instance vfunc +0x1C);
- for smooth sub-meshes, it is the skinning matrix (vfunc +0x24) when the
  bone's flag bit 9 is set, else the world matrix;
- the palette size is stored for c9.z.

### 5.5 Worked example: BannerBG's draw

Inputs from the files [file]:

- one mesh, shape 0, material `mt_BG`;
- the sub-mesh has bone list [1] (`BG`) and skinning 0;
- one interleaved buffer, 24 bytes per vertex:
  - Position float×3 @0;
  - Color ubyte×4, scale 1/255, @12;
  - TexCoord0 float×2 @16;
- the skeleton (@0x13D4) has flags 0x2, so bit 0 is clear;
- `mt_BG`: Flags 0, three UV coordinators with source 0, AmbientF = DiffuseF
  = (1, 1, 1, 1). Coordinator details are in `cgfx-material-texture.md`
  §2.3, §2.6.

The resulting state follows the HOME functions above:

| State | Value |
|---|---|
| program | DVLE 0 |
| attribute slots | Position → v0, Color → v3, TexCoord0 → v4. With the slots in buffer order the permutation is `0x00000430` and there are 3 attributes. This is built from the input map; HOME's own activate command was not decoded |
| c6 PosOffs | (0, 0, 0, 0) |
| c7 IrScale[0] | (1, 0, 0, 1/255) |
| c8 IrScale[1] | (1, 0, 0, 0) |
| c9 IrScale[2] | (1, 1, 1, 1): one bone in the palette |
| bools | b0, **b2 (rigid)**, b7 (4-component colour), b9, b10, b11 (UV), b13, b14 set; all others clear |
| c25–c27 UnivReg[0..2] | world matrix of bone `BG` (index 1). At rest, after SceneIn, it is identity (`cgfx-model.md` §14) |
| c0–c2 WrldMtx | identity (skeleton bit 0 clear) |
| c86–c92 | ProjMtx and ViewMtx of section 3.7.6 |
| c10 TexcMap | (0, 0, 0, 0) |
| c11–c18 | coordinator matrices: identity; S(3, 6) with T animated; S(3, 6) with T = (−0.02, 0) |
| c20 MatAmbi, c21 MatDiff | (1, 1, 1, 1) each |

The vertex results:

    o0 = ProjMtx · ViewMtx · I · Bone_BG · (aPosition, 1)      # rigid path, a = 0
    o3 = aColor / 255                                         # MatAmbi.w = 1, MatDiff.a = 1
    o4 = (uv, 0, 0)                                           # TexMtx0 = identity
    o5.xy = TexMtx1 · (uv, 1, 1)
    o6.xy = TexMtx2 · (uv, 1, 1)
    o1 = 0                                                    # no normals
    o2 = −p_view

**Where this differs from SPICA.** SPICA's `H3DMesh.UpdateBoolUniforms`
leaves `IsRgdSk` false for skinning 0 (`cgfx-model.md` §15). HOME sets it,
and adds `WrldMtx` = identity times the palette entry. The vertex results are
the same:

- dp4 with identity rows is exact in float;
- v7.x reads 0 in every case listed in section 5.2.

Kabegami can take either path. HOME's path is rigid with v7.x = 0.

### 5.6 Notes for Kabegami's renderer

**Projection.** Build ProjMtx with the pivot 1 rows, NW4C's depth (z/w 0 at
near, −1 at far) and SinCosFIdx; regenerate the table by the rule in section
3.7.6. Then set:

- DEPTHMAP_ENABLE 1;
- DEPTHMAP_SCALE −1.0;
- DEPTHMAP_OFFSET 0;
- a 240 × 400 viewport for the top screen (the viewport itself was **not
  traced** in HOME).

**Uploads.** Send uniforms in float32 mode, in w, z, y, x word order. Upload
DVLE 0's constants c93–c95 as well; they are not in the code. Keep b0 set in
the bool register.

**Stereo.** For a single 2D picture, the base matrices are enough. For 3D
output, rebuild the eyes with the frustum path. With the slider at 0, they
differ only by an ulp.

### 5.7 Unknowns and guesses (Part 3)

- What NormMtx (run-time model +0x1C4) holds, and where `UseNorM` and
  `IsUniSc` would be set.
- LightCt's upload, and how vertex lights are laid out in UnivReg beyond the
  six-register stride the shader shows.
- The exact *activate* command stream (permutation, fixed attributes) HOME
  builds per mesh. The rigid path's v7 default rests on the deactivate
  command and the zakuro behaviour.
- The hemisphere uniforms' CPU side.
- Whether `TexTran` is written at all for UV-only materials. The code writes
  it only for projection or shadow maps; otherwise it keeps its last value.
  The shader reads it only on those paths.

## 6. Appendix: HOME 11.17 functions used (VA)

| VA | Role (name a **guess**) |
|---|---|
| 0x287E18 | loads `3D/BannerBG_LZ.bin`, `BannerBGmask`, `BannerFrame`, `launcher_LZ.bin`, `BannerCamera` (call at 0x287F04) |
| 0x24F430 | camera file loader: first camera of DATA slot 5 → scene table, render-context slot; sets pivot 1 |
| 0x19BA98 | *Camera::Create*: type switches, allocates 0x2A4 bytes, passes WScale |
| 0x11B990 | *Camera* constructor (matrix members, updaters, WScale at +0x29C) |
| 0x18FB0C / 0x18FB58 | aim view updater: create / update |
| 0x1905D4 / 0x190620 | look-at view updater: create / update |
| 0x18D66C / 0x18D6B8 | rotate view updater: create / update |
| 0x193BD0 / 0x193C38 | perspective projection updater: create / update (with pivot) |
| 0x1929DC / 0x192A44 | frustum projection updater |
| 0x18FE80 / 0x18FEE8 | orthographic projection updater |
| 0x1AD1C0 | *MTX34LookAt*(eye, twist, target) |
| 0x22FD40 | *MTX44Frustum* |
| 0x22FC84 | *MTX44Pivot* |
| 0x22B4E4 | *SinCosFIdx*, table at 0x30C7D4 |
| 0x10A334 | *Camera::UpdateCameraMatrix* |
| 0x1038F0 | per-frame loop over 32 camera slots |
| 0x10B414 / 0x10B780 / 0x10F764 | stereo camera setup / per-frame / *CalculateMatrices* |
| 0x109E40 | uploads ViewMtx and ProjMtx on camera change |
| 0x2366FC | active-camera switch: WScale → VA 0x33C1B0 |
| 0x192D60–0x193024 | depth-map command block (0x6D, 0x4D, 0x4E) |
| 0x13A478–0x13A528 | depth range part of the viewport setup (clamp, store, near − far → VA 0x33C174) |
| 0x11160C | set model: WrldMtx, NormMtx, clears UseNorM |
| 0x111484 | draw shape: PosOffs, per-sub-mesh palette or world matrix |
| 0x11AEC4 | bone palette upload, IsSmoSk/IsRgdSk |
| 0x11B0F0 | per sub-mesh: bool register, c9, draw |
| 0x111A20 | mesh activate: enable commands, AttrScaleCommands, IsVertA/IsBoneW |
| 0x198300 | mesh setup: attribute scales from the shape, program-description input map |
| 0x19F3A0 | mesh deactivate commands (fixed attributes (0, 0, 0, 1)) |
| 0x18C9C8 | lighting bools (IsQuate, IsVertL, IsHemiL, IsHemiO) |
| 0x19DD3C | MatDiff, MatAmbi upload |
| 0x19E6B8 | texture coordinators: bools, TexMtx0–2, TexTran, TexcMap |
| 0x19DAA4 | texture SRT matrix (Maya, SoftImage, 3ds Max) |
| 0x1981B8, 0x194F68 | material → SHDR → program description lookup |

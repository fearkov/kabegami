# CGFX containers and the BannerBG model

## Purpose

Kabegami draws the HOME Menu's default top-screen wallpaper by feeding the
console's own PICA200 data to Zakuro's GPU emulation. The wallpaper is the
model `BannerBG` in `3D/BannerBG_LZ.bin` of the HOME Menu RomFS. This spec
covers what a reader needs to get that model's geometry to the GPU:

- the CGFX container: header, blocks, the DATA block's dictionary slots,
  self-relative pointers, the DICT (Patricia tree) structure, the common
  object header;
- the model (CMDL): transform, meshes, materials dictionary, shapes,
  mesh-node visibility, skeleton and bones;
- meshes, shapes, bounding boxes, sub-meshes, primitives, index streams and
  vertex buffers;
- BannerBG decoded completely, whether it is skinned or bone-attached, and
  what its two skeletal animations move.

Materials, textures, animation curves, the camera and the shader are only
touched where the geometry depends on them. Their own specs cover them.

## Sources and conventions

Files (the user's dump of the EUR HOME Menu 11.17, title 0004003000009802
v29696). All of them are LZ11-compressed. Every offset in this document is in
the **decompressed** data.

| File | Raw size, SHA-256 | Decompressed size, SHA-256 |
|---|---|---|
| `3D/BannerBG_LZ.bin` | 7,212, `27d58c2113d2c2d46e3bcc36bb2ae56c35e19d9823488287b6759df998108711` | 86,080, `092c8682d0cfabf0a1823a8e3a2c12556515c437afba2aa6f6ac7fc4d5e34595` |
| `3D/BannerCamera_LZ.bin` | 500, `54df078dfb80a7fe55b391d10993353fe3b9ae2135a87f204b1ad8f641cc4ad9` | 1,158, `19d1009bc472a34626a10ec903d689fbd4b3b6951addc28beaa4f155db9bf897` |
| `3D/BannerBGmask_LZ.bin` | 8,354, `d8c3cf350e2af35263f640a80144a40aa1d9a1b878f1b6eb872be0b4ec669541` | 23,360, `d9139c60d5d608704b26252699a275f034ba04640859e0a8b1b9037047e1d233` |
| `3D/BannerFrame_LZ.bin` | 16,839, `794bfb7f116c5686e44bc5701b4ea715c32f169e76306a272926d5d979a7e9ba` | 27,620, `9e5399fbe4f2ca16ba20ec4fec91b2e1ceeaef95587a7919504aa47ec6dbc77e` |
| `3D/BannerFolder_LZ.bin` | 45,339, `6414be9d3752a1cd541f97ad47ae67ad852bbe6e041956ab542ea6831b978ecd` | 118,720, `28f62936179fe1e0787080e62ac7b4d5d3e8e279cb4bbccb9c8fd2a47c799305` |
| `3D/BannerDS_LZ.bin` | 24,229, `c1a8e2fd074f5e28f4ee077c0953dd6f081e950449efe6b0f8e386f99c797cd4` | 94,848, `f0e583c848536c677557f07a31ba6463d6170e4776d4296ab4a3603820951baa` |
| `3D/nwgfx_DefaultShader_LZ.bin` | 3,607, `e5240dd7d4e6dcd9fd362980a60b6fe093d9bfbde038c6182a948344808ce592` | 10,896, `cfe8d7421feceacdcd3c5f675e4a152f164107afb8abb903bc2b2c647bf65a48` |
| `3D/Textures_LZ.bin` | 2,268, `094cd9dc3f3cf21ffb5da338ecd43b2356c0cf34f687c301e43e476e2192f3c7` | 4,992, `fe0c22fbe68a05f3c332d76b50514bd9641c8f2cd269ae407f8996024ce9455e` |
| `exefs-code.bin` (HOME's `.code`) | 1,393,164, `e77d417b8609f891aa5a9ba20e26a087afdb254ef6e7f27fd8a4162b24cd6adc` | BLZ, 2,367,488, `5f4d8d0e80d8e7d6fafcefa0630c5106473ade4658585cfb499a6ec3324d89a7` |

Notation:

- `@0x1234` is an absolute offset in the decompressed file (BannerBG unless
  another file is named). `+0x12` is an offset from the start of the
  structure being described.
- Everything is little-endian. `f32` is an IEEE single. `ptr` is a
  self-relative pointer (see [Pointers](#5-pointers-lists-strings-booleans)).
- "Verified" means read from the files with a script, usually over all 18
  CGFX files in `3D/` (15 files with a model, `Textures_LZ.bin`, the camera
  and the shader).
  **Unverified** marks what no HOME file exercises, or what comes only from a
  reference.

References: SPICA (Unlicense; `SPICA/Formats/CtrGfx`, `SPICA.Rendering`) for
field names and serializer rules; 3dbrew `CGFX` and `GPU/Internal_Registers`;
the archived florian.nouwt.com CGFX page (facts only); Zakuro's
`crates/zakuro-gpu` (`src/registers.rs`, `src/raster.rs`) for how the PICA200
consumes the result. SPICA's serializer rules matter for reading its classes:
fields are laid out in declaration order with the base class first; a class
carrying `[TypeChoice]` starts with a `u32` type word; a reference field is a
self-relative pointer; a list is a `u32` count followed by a pointer
(`LengthPos.BeforePtr`, `PointerType.SelfRelative` in `Gfx.cs`).

Names used here, with their aliases elsewhere:

| This spec | SPICA class | 3dbrew / Nouwt |
|---|---|---|
| model | `GfxModel`, `GfxModelSkeletal` | CMDL |
| mesh | `GfxMesh` | SOBJ "mesh", "objects" |
| shape | `GfxShape` | SOBJ "vertex info" |
| sub-mesh (primitive set) | `GfxSubMesh` | face group |
| primitive | `GfxFace` | "Unknown2" |
| index stream | `GfxFaceDescriptor` | face array descriptor |
| vertex buffer | `GfxVertexBuffer` and subclasses | vertex group |
| attribute | `GfxAttribute` | vertex component declaration |
| skeleton | `GfxSkeleton` | "skeleton info SOBJ" |

## 1. LZ11 wrapper

| Offset | Type | Meaning | BannerBG |
|---|---|---|---|
| raw +0x00 | u8 | `0x11` | `0x11` |
| raw +0x01 | u24 | decompressed size (when 0, a `u32` size follows at +0x04) | `0x015040` (86,080) |

The compressed stream ends exactly at the end of the file (7,212 bytes
consumed; no trailer). The decompressed size equals the CGFX header's file
size.

## 2. CGFX header

| Offset | Type | Meaning | BannerBG |
|---|---|---|---|
| @0x00 | char[4] | magic | `CGFX` |
| @0x04 | u16 | byte-order mark: bytes `FF FE` (0xFEFF read little-endian) = little-endian | 0xFEFF |
| @0x06 | u16 | header size, where the first block starts | 0x14 |
| @0x08 | u32 | revision | 0x05000000 (all 18 files) |
| @0x0C | u32 | file size | 0x15040 |
| @0x10 | u32 | block count | 2 |

## 3. Blocks

Blocks follow the header back to back. Each starts with an 8-byte header and
its size includes that header.

| Offset | Type | Meaning | BannerBG DATA | BannerBG IMAG |
|---|---|---|---|---|
| +0x00 | char[4] | magic | `DATA` @0x14 | `IMAG` @0x3278 |
| +0x04 | u32 | size including this header | 0x3264, ends @0x3278 | 0x11DC8, ends @0x15040 = file size |

- `DATA` holds every structure and, at its end, the string pool.
- `IMAG` holds raw GPU data only: index streams, vertex buffers and texture
  images. DATA structures point into it with ordinary self-relative pointers.
- Files with neither geometry nor textures have one block:
  `BannerCamera_LZ.bin` (DATA size 0x472, ends @0x486) and
  `nwgfx_DefaultShader_LZ.bin`.
- In the 15 files that carry textures, IMAG's contents start on a 128-byte
  boundary (BannerBG @0x3280). The string pool is padded with zeros to get
  there (BannerBG: the last string, `DefaultShader`, ends @0x320F; zeros
  from @0x3210 up to @0x3278). `BannerFrame_LZ.bin` has
  geometry but no texture, and its IMAG contents start @0xECC, so the
  padding probably serves the textures.

## 4. DATA block: the 15 dictionary slots

| Offset | Type | Meaning |
|---|---|---|
| DATA+0x00 | char[4] | `DATA` |
| DATA+0x04 | u32 | block size |
| DATA+0x08 + 8·i | u32 | entry count of slot *i* (0 = unused) |
| DATA+0x0C + 8·i | ptr | DICT of slot *i* (0 when unused) |

There are exactly **15** slots. In all 18 files the first DICT starts at
DATA+0x80 = 8 + 15·8 (BannerBG @0x94). SPICA's `Gfx` class declares 16
dictionaries, with `FogAnimations` at 14 and `Emitters` at 15. That does not
match this revision: SPICA's 16th read lands on the first DICT's header.

| Slot | Count @ / ptr @ (BannerBG) | Contents | BannerBG | Verified by |
|---|---|---|---|---|
| 0 | @0x1C / @0x20 | models | 1 → DICT @0x94: `BannerBG` @0x1B4 | CMDL objects in 15 files |
| 1 | @0x24 / @0x28 | textures | 4 → DICT @0xC0: `BG_64_00` @0x15FC, `BG_CapMask_00` @0x1658, `BG_DefBase_00` @0x16B4, `BG_DmyApp_00` @0x1710 | TXOB (type 0x20000011), 87 objects in 15 files |
| 2 | @0x2C / @0x30 | lookup tables | 0 | LUTS (0x04000000), 12 objects in 10 files |
| 3 | @0x34 / @0x38 | materials (outside models) | 0 | **unverified**, unused everywhere |
| 4 | @0x3C / @0x40 | shaders | 0 | SHDR (0x80000002) in `nwgfx_DefaultShader_LZ.bin` |
| 5 | @0x44 / @0x48 | cameras | 0 | CCAM (0x4000000A) `BannerCamera` in `BannerCamera_LZ.bin` |
| 6 | @0x4C / @0x50 | lights | 0 | CFLT (0x400000A2), 11 files |
| 7 | @0x54 / @0x58 | fogs | 0 | **unverified**, unused everywhere |
| 8 | @0x5C / @0x60 | environments | 0 | CENV (0x00800000), 11 files |
| 9 | @0x64 / @0x68 | skeletal animations | 2 → DICT @0x11C: `BannerBG_SceneIn` @0x176C, `BannerBG_SceneOut` @0x19B0 | every CANM here names target group `SkeletalAnimation` (16 objects, 13 files) |
| 10 | @0x6C / @0x70 | material animations | 4 → DICT @0x158: `BannerBG_AppPause` @0x1C6C, `BannerBG_AppQuit` @0x21DC, `BannerBG_AppRestart` @0x2468, `BannerBG_Loop` @0x298C | every CANM here names `MaterialAnimation` (21 objects, 13 files) |
| 11 | @0x74 / @0x78 | visibility animations | 0 | **unverified** (3dbrew/Nouwt order) |
| 12 | @0x7C / @0x80 | camera animations | 0 | **unverified** |
| 13 | @0x84 / @0x88 | light animations | 0 | **unverified** |
| 14 | @0x8C / @0x90 | emitters | 0 | **unverified** |

## 5. Pointers, lists, strings, booleans

**Self-relative pointers.** A pointer is an `s32` added to the address of the
pointer field itself. 0 means null. Pointers can be negative, and they cross
from DATA into IMAG the same way. Examples:

| Field | Raw value | Target |
|---|---|---|
| models DICT ptr @0x20 | 0x00000074 | @0x94 |
| mesh owner-model ptr @0xC08 | 0xFFFFF5AC | @0x1B4 (backwards) |
| bone `BG` parent ptr @0x152C | 0xFFFFFF10 | @0x143C |
| index-stream buffer ptr @0x12DC | 0x00001FA4 | @0x3280 (IMAG contents) |

**Lists** are a `u32` count followed by a pointer:

- lists of objects point to an array of *count* self-relative pointers, each
  relative to its own array slot. Examples: the mesh list @0x298 holds 0x950
  → @0xBE8. Shapes, sub-meshes, primitives, index streams, vertex buffers and
  attributes work the same way.
- lists of plain values point to the values inline. Examples: sub-mesh bone
  indices (`s32[]`), primitive buffer objects (`u32[]`), fixed-attribute
  vectors (`f32[]`).
- a dictionary inside an object is the same count + pointer pair, with the
  pointer aimed at a DICT.

**Strings** are NUL-terminated ASCII, held in a pool at the end of DATA
(BannerBG @0x2AFC–@0x3277: 59 strings, then zero padding). Strings are
shared: the models DICT node and the CMDL both point to `BannerBG` @0x2AFC,
and in all 85 bones the bone's name and its DICT node point to the same
string. An empty name is a pointer to an empty string (@0x3201 in BannerBG,
shared by the mesh, the shape and the skeleton), never null.

**Booleans.** SPICA's `bool` fields are 4 bytes. The file also has 1-byte
flags: the mesh's and the index stream's `visible`.

## 6. DICT

### Layout

| Offset | Type | Meaning | Models DICT @0x94 |
|---|---|---|---|
| +0x00 | char[4] | `DICT` | `DICT` |
| +0x04 | u32 | total size = 0xC + 0x10·(count+1) | 0x2C |
| +0x08 | u32 | entry count, the root not included | 1 |
| +0x0C | node[count+1] | node 0 is the root, nodes 1..count are the entries | |

Node, 0x10 bytes:

| Offset | Type | Meaning | Root @0xA0 | Entry @0xB0 |
|---|---|---|---|---|
| +0x00 | u32 | reference bit | 0xFFFFFFFF | 0x3E |
| +0x04 | u16 | left child index | 1 | 0 |
| +0x06 | u16 | right child index | 0 | 1 |
| +0x08 | ptr | name | null | `BannerBG` @0x2AFC |
| +0x0C | ptr | value (the object) | null | @0x1B4 |

Entry *k* (0-based) is node *k*+1. Indices stored elsewhere count entries in
that order: mesh material indices, bone indices (in all 14 skeletons the bone
DICT order equals each bone's own `index`). In these files the entries happen
to be in ASCII order.

All 251 DICTs in the 18 files were checked: the size formula holds; every
root is (0xFFFFFFFF, right 0, no name, no value); reference bits range from
14 to 0x23E.

### Looking a name up

The tree is a Patricia (crit-bit) tree over the name's bits. A name is read
as a bit string: bit *b* is bit (*b* & 7), counted from the least significant
bit, of byte (*b* >> 3), counted from the start of the name. Bits past the
end of the name are 0.

```
bit(name, b) = (b >> 3) < len(name) ? (name[b >> 3] >> (b & 7)) & 1 : 0

find(dict, name):
    parent = node[0]                    # root, ref bit 0xFFFFFFFF
    i      = parent.left
    while node[i].refbit < parent.refbit:      # going down
        parent = node[i]
        i = bit(name, parent.refbit) ? parent.right : parent.left
    # an upward link was followed: node[i] is the only candidate
    return node[i].name == name ? i - 1 : not found   # entry index
```

This finds every one of the 1,848 names in the 251 DICTs, and nothing else
for probe names that are absent.

Worked example, the BannerBG textures DICT @0xC0 (nodes: 1 `BG_64_00` bit
0x3D, 2 `BG_CapMask_00` bit 0x65, 3 `BG_DefBase_00` bit 0x4B, 4
`BG_DmyApp_00` bit 0x5D; root.left = 2):

| Name | Walk | Result |
|---|---|---|
| `BG_DmyApp_00` | node 2 tests bit 0x65 (byte 12, past the end) = 0 → left 4; node 4 tests bit 0x5D (byte 11 `0`, bit 5) = 1 → right 4; 0x5D is not below 0x5D, stop | node 4, names match → entry 3 |
| `BG_64_00` | 2: bit 0x65 = 0 → 4; 4: bit 0x5D (past the end) = 0 → left 1; 1: bit 0x3D (byte 7 `0`, bit 5) = 1 → right 1, stop | node 1 → entry 0 |
| `BG_Missing` | same path to node 1 | `BG_64_00` ≠ `BG_Missing` → not found |

SPICA's `PatriciaTree.Traverse` adds one more stop condition, refbit > 0.
That only matters for a node whose bit is 0; no such node exists in these
files.

### Building a tree (for writing)

SPICA's `GfxDictionary.RebuildTree` / `PatriciaTree.Insert` rebuilt all 251
trees from their names, inserted in entry order, with identical reference
bits and child indices:

```
nodes = [root(refbit 0xFFFFFFFF, left 0, right 0, no name)]
top   = 8 * (longest name length) - 1
for name in names:                              # entry order
    i, _ = descend(name, stop = 0)
    b = top
    while bit(node[i].name, b) == bit(name, b): b -= 1   # bit(none, b) = 0
    child, parent = descend(name, stop = b)
    new = len(nodes)
    if bit(name, b): left, right = child, new
    else:            left, right = new, child
    if bit(name, parent.refbit): parent.right = new
    else:                        parent.left  = new
    nodes.append(node(refbit b, left, right, name))

descend(name, stop):          # returns (index reached, its parent)
    parent = node[0]; i = parent.left
    while parent.refbit > node[i].refbit > stop:
        parent = node[i]
        i = bit(name, parent.refbit) ? parent.right : parent.left
    return i, parent
```

## 7. Common object header

Most objects start like this (SPICA `GfxObject`: type word, then
`GfxRevHeader`, name, metadata):

| Offset | Type | Meaning | BannerBG CMDL @0x1B4 |
|---|---|---|---|
| +0x00 | u32 | type flags | 0x40000092 |
| +0x04 | char[4] | magic | `CMDL` |
| +0x08 | u32 | revision | 0x07000000 |
| +0x0C | ptr | name | `BannerBG` |
| +0x10 | u32 | user data count | 0 |
| +0x14 | ptr | user data DICT | null |

Type flags, magic and revision seen in the 18 files:

| Type flags | Magic | Revision | Object |
|---|---|---|---|
| 0x40000092 | CMDL | 0x07000000 | model with skeleton (14 files, BannerBG) |
| 0x40000012 | CMDL | 0x07000000 | model without skeleton (`BannerFrame`) |
| 0x01000000 | SOBJ | 0 | mesh |
| 0x10000001 | SOBJ | 0 | shape |
| 0x02000000 | SOBJ | 0 | skeleton (no `CSKL` magic occurs in any file) |
| 0x08000000 | MTOB | 0x06000100 | material |
| 0x20000011 | TXOB | 0x05000000 | image texture |
| 0x04000000 | LUTS | 0x04000000 | lookup-table set |
| 0x80000002 | SHDR | 0x05000000 | shader |
| 0x4000000A | CCAM | 0x06000000 | camera |
| 0x400000A2 | CFLT | 0x06000000 | fragment light |
| 0x00800000 | CENV | 0x01000000 | environment |
| none | CANM @+0x00, revision @+0x04 | 0x05000000 | animation: no type word, the magic comes first |

Bit 7 of a CMDL's type flags marks a skeletal model. Verified: only then does
the skeleton pointer at +0xE0 exist (BannerFrame's 0x40000012 CMDL is 0xE0
bytes long and its mesh list starts right after, @0x1A0 = 0xC0 + 0xE0). Other
bit meanings are **unverified**. Structures without the header: bones, the
bounding box, sub-meshes, primitives, index streams, vertex buffers,
attributes, animation groups and their elements, DICTs. Several of these
still start with a type word (OBB 0x80000000, vertex buffers 0x40000001,
0x40000002 and 0x80000000, animation group 0x80000000).

**User data.** No object in the 18 files has any (all counts are 0, bones
included). Per SPICA `GfxMetaData` (**unverified**), it is a DICT of entries
with this layout: type word (0x10000000 string, 0x20000000 integer,
0x40000000 color, 0x80000000 float); name pointer; `u32` type (0 single,
1 integer, 2 string, 3 vector3, 4 color); for strings, a `u32` format
(0 ASCII, 1 UTF-8, 2 UTF-16LE, 3 UTF-16BE); then a count + pointer list of
values.

## 8. Model (CMDL)

BannerBG's CMDL starts @0x1B4. Field names follow SPICA (`GfxObject` →
`GfxNode` → `GfxNodeTransform` → `GfxModel` → `GfxModelSkeletal`).

| Offset | @ | Type | Meaning | BannerBG |
|---|---|---|---|---|
| +0x00 | 0x1B4 | u32 | type flags | 0x40000092 |
| +0x04 | 0x1B8 | char[4] | magic | `CMDL` |
| +0x08 | 0x1BC | u32 | revision | 0x07000000 |
| +0x0C | 0x1C0 | ptr | name | `BannerBG` |
| +0x10 | 0x1C4 | u32 | user data count | 0 |
| +0x14 | 0x1C8 | ptr | user data | null |
| +0x18 | 0x1CC | u32 | branch-visible bits (SPICA `BranchVisible`, bit 0) | 1 (1 in every CMDL, CCAM, CFLT) |
| +0x1C | 0x1D0 | u32 bool | is branch visible (SPICA `_IsBranchVisible`) | 1 |
| +0x20 | 0x1D4 | u32 | child count | 0 (0 everywhere; **unverified** beyond that) |
| +0x24 | 0x1D8 | ptr | children (pointer array) | null |
| +0x28 | 0x1DC | u32 | animation group count | 3 |
| +0x2C | 0x1E0 | ptr | animation group DICT | @0x2A0 |
| +0x30 | 0x1E4 | f32[3] | scale | (1, 1, 1) |
| +0x3C | 0x1F0 | f32[3] | rotation, radians | (0, 0, 0) |
| +0x48 | 0x1FC | f32[3] | translation | (0, 0, 0) |
| +0x54 | 0x208 | f32[3][4] | local matrix | identity |
| +0x84 | 0x238 | f32[3][4] | world matrix | identity |
| +0xB4 | 0x268 | u32 | mesh count | 1 |
| +0xB8 | 0x26C | ptr | mesh pointer array | @0x298 → mesh @0xBE8 |
| +0xBC | 0x270 | u32 | material count | 1 |
| +0xC0 | 0x274 | ptr | material DICT | @0x2EC |
| +0xC4 | 0x278 | u32 | shape count | 1 |
| +0xC8 | 0x27C | ptr | shape pointer array | @0x29C → shape @0x1210 |
| +0xCC | 0x280 | u32 | mesh-node visibility count | 0 |
| +0xD0 | 0x284 | ptr | mesh-node visibility DICT | null |
| +0xD4 | 0x288 | u32 | flags: bit 0 visible; bit 1 non-uniform scalable (SPICA, **unverified**) | 0x1 |
| +0xD8 | 0x28C | u32 | face culling (SPICA types it `PICAFaceCulling`; never used by SPICA; **unverified**) | 0 |
| +0xDC | 0x290 | s32 | layer id (**unverified**) | 0 |
| +0xE0 | 0x294 | ptr | skeleton, only when type bit 7 is set | @0x13D4 |

The CMDL is 0xE4 bytes long with a skeleton and 0xE0 without one. The
meaning of flags bit 0 is backed by the file itself: the
`VisibilityAnimation` group's model element `IsVisible` (@0x440) gives member
offset 0xD4 (@0x450) with size 1. All 15 CMDLs have an identity transform,
flags 0x1, face culling 0 and layer 0.

**Matrices** (`f32[3][4]`) are stored as 3 rows of 4 floats, row-major, in
column-vector form: row *r* = (R[r][0], R[r][1], R[r][2], T[r]). Verified by
the bones (section 11): bone `root` has translation z 44.786 at row 2,
column 3 (@0x14AC). The 3×3 matrices inside the bounding boxes use the same
order.

### 8.1 Animation groups (CMDL +0x28)

The animation group DICT @0x2A0 has 3 entries. These are the members that
animations of each kind may bind to.

| Group | @ | +0x04 flags | +0x0C member type | elements (+0x10 count, +0x14 DICT) | blend ops (+0x18 count, +0x1C `s32[]`) | +0x20 evaluation timing |
|---|---|---|---|---|---|---|
| `SkeletalAnimation` | 0x318 | 1 | 1 | 2: `root`, `BG` | [8] | 1 |
| `VisibilityAnimation` | 0x3DC | 0 | 3 | 2: `IsVisible` (model), `Meshes[0].IsVisible` | [0] | 0 |
| `MaterialAnimation` | 0x49C | 0 | 2 | 27 members of `mt_BG` (colors, texture mappers, samplers, blend color, texture coordinators) | [3, 7, 5, 2] | 1 |

Group header: +0x00 type 0x80000000, +0x08 name pointer; size 0x24. This
matches SPICA's `GfxAnimGroup`, with evaluation timing 0 = before world
update and 1 = after scene cull.

The **elements do not match SPICA's `GfxAnimGroupElement`**: the files carry
three more words. The common part is 0x28 bytes, then come the
kind-specific fields:

| Offset | Meaning | `root` @0x37C | `IsVisible` @0x440 | `Meshes[0].IsVisible` @0x46C |
|---|---|---|---|---|
| +0x00 | type (SPICA: 0x40000000 bone, 0x10000000 model, 0x01000000 mesh, 0x08000000 material color, …) | 0x40000000 | 0x10000000 | 0x01000000 |
| +0x04 | path name pointer | `root` | `IsVisible` | `Meshes[0].IsVisible` |
| +0x08 | object name pointer | `root` | null | `0` |
| +0x0C | member name pointer | null | null | null |
| +0x10 | member offset in the target object | 0 | 0xD4 | 0x24 |
| +0x14 | member size | 0 | 1 | 1 |
| +0x18 | blend-operation index (the 27 material elements use 0–3, matching the group's 4 blend ops) | 0 | 0 | 0 |
| +0x1C | object type (SPICA `GfxAnimGroupObjType`: 0 bone, 1 material color, 2 sampler, 3 mapper, 4 blend operation, 5 texture coordinator, 6 model, 7 mesh; all seen) | 0 | 6 | 7 |
| +0x20, +0x24 | **unverified** | 0, 0 | 1, 0 | 0, 0 |
| +0x28… | bone: bone name, object type (0x30 bytes); model: object type (0x2C); mesh: mesh index, object type (0x30) | `root`, 0 | 6 | 0, 7 |

The member offsets confirm two fields of this spec: the model's flags at CMDL
+0xD4 and the mesh's `visible` byte at +0x24. The animation spec covers
binding in full.

### 8.2 Materials dictionary (CMDL +0xBC)

DICT @0x2EC, 1 entry: `mt_BG` (reference bit 0x26) → MTOB @0xC68 (type
0x08000000, revision 0x06000100, no user data). A mesh's material index
counts entries of this DICT. Material contents are out of scope here.

### 8.3 Mesh-node visibility (CMDL +0xCC)

No HOME model has any: the count is 0 in all 15 CMDLs, and every mesh's
mesh-node index is -1. Per SPICA (`GfxMeshNodeVisibility`, **unverified**),
it is a DICT of 8-byte entries: name pointer + `u32` bool `IsVisible`.

## 9. Mesh (SOBJ, type 0x01000000)

BannerBG's only mesh is @0xBE8. Size 0x80: the MTOB follows @0xC68.

| Offset | @ | Type | Meaning | BannerBG |
|---|---|---|---|---|
| +0x00 | 0xBE8 | u32 | type flags | 0x01000000 |
| +0x04 | 0xBEC | char[4] | magic | `SOBJ` |
| +0x08 | 0xBF0 | u32 | revision | 0 |
| +0x0C | 0xBF4 | ptr | name | "" (@0x3201) |
| +0x10 | 0xBF8 | u32 | user data count | 0 |
| +0x14 | 0xBFC | ptr | user data | null |
| +0x18 | 0xC00 | s32 | shape index (into CMDL +0xC8) | 0 |
| +0x1C | 0xC04 | s32 | material index (entry of CMDL +0xC0) | 0 → `mt_BG` |
| +0x20 | 0xC08 | ptr | owning model | @0x1B4 (true for all 56 meshes) |
| +0x24 | 0xC0C | u8 | visible | 1 (`BannerDS` mesh 4 `Text_old1` has 0) |
| +0x25 | 0xC0D | u8 | render priority | 0 (values 0–255 elsewhere) |
| +0x26 | 0xC0E | s16 | mesh-node index, -1 = none | -1 |
| +0x28 | 0xC10 | s32 | primitive index (SPICA `PrimitiveIndex`; **unverified**) | 0 |
| +0x2C | 0xC14 | u32 | runtime flags | 0 |
| +0x30 | 0xC18 | u32[12] | runtime: attribute-scale commands (SPICA) | 0 |
| +0x60 | 0xC48 | u32 ×4 | runtime: enable/disable command pointer + length (SPICA) | 0 |
| +0x70 | 0xC58 | ptr | mesh-node name | `BG` |
| +0x74 | 0xC5C | u64 | runtime: render key cache | 0 |
| +0x7C | 0xC64 | u32 | runtime: command allocator | 0 |

The runtime fields (+0x2C..+0x6F, +0x74..+0x7F) are zero in all 56 meshes.
The mesh-node name is the name of the node the mesh was exported under. In
50 of 55 skeletal meshes it equals the name of the sub-mesh's bone. In
BannerDef it is empty, and in BannerFrame (no skeleton) it is the model's
name.

## 10. Shape (SOBJ, type 0x10000001)

BannerBG's only shape is @0x1210. Size 0x44.

| Offset | @ | Type | Meaning | BannerBG |
|---|---|---|---|---|
| +0x00 | 0x1210 | u32 | type flags | 0x10000001 |
| +0x04 | 0x1214 | char[4] | magic | `SOBJ` |
| +0x08 | 0x1218 | u32 | revision | 0 |
| +0x0C | 0x121C | ptr | name | "" |
| +0x10 | 0x1220 | u32 | user data count | 0 |
| +0x14 | 0x1224 | ptr | user data | null |
| +0x18 | 0x1228 | u32 | flags | 0 (0 in all 56 shapes) |
| +0x1C | 0x122C | ptr | bounding box | @0x125C |
| +0x20 | 0x1230 | f32[3] | position offset, added to scaled positions | (0, 0, 0) (all shapes) |
| +0x2C | 0x123C | u32 | sub-mesh count | 1 |
| +0x30 | 0x1240 | ptr | sub-mesh pointer array | @0x1254 → @0x129C |
| +0x34 | 0x1244 | u32 | runtime base address | 0 |
| +0x38 | 0x1248 | u32 | vertex buffer count | 1 |
| +0x3C | 0x124C | ptr | vertex buffer pointer array | @0x1258 → @0x12FC |
| +0x40 | 0x1250 | ptr | blend shape (SPICA `GfxBlendShape`; **unverified**) | null (null in all shapes) |

### 10.1 Bounding box (OBB)

@0x125C, 0x40 bytes. **SPICA's `GfxBoundingBox` lacks the type word** and so
reads every field 4 bytes early.

| Offset | @ | Type | Meaning | BannerBG |
|---|---|---|---|---|
| +0x00 | 0x125C | u32 | type: 0x80000000 = oriented box (all 56 shapes) | 0x80000000 |
| +0x04 | 0x1260 | f32[3] | center | (3.92955e-06, 1, -12.7051) |
| +0x10 | 0x126C | f32[3][3] | orientation, rows in file order; its **columns** are the box axes | identity |
| +0x34 | 0x1290 | f32[3] | size: full extents, not half | (56.458, 40, 14.5898) |

Verified two ways. BannerBG's 102 positions span exactly this box: their AABB
has center (0, 1, -12.70509) and size (56.458, 40, 14.58982). And in all 37
shapes whose orientation is not identity, every vertex lies within
center ± size/2 along the columns (worst excess 1e-4), while reading the rows
as axes fails by up to 5 units.

### 10.2 Sub-mesh (primitive set)

@0x129C, 0x14 bytes.

| Offset | @ | Type | Meaning | BannerBG |
|---|---|---|---|---|
| +0x00 | 0x129C | u32 | bone index count | 1 |
| +0x04 | 0x12A0 | ptr | `s32[]` bone indices (into the skeleton) | @0x12B0: [1] = `BG` |
| +0x08 | 0x12A4 | u32 | skinning: 0 none, 1 rigid, 2 smooth (SPICA `GfxSubMeshSkinning`) | 0 (0 in all 56) |
| +0x0C | 0x12A8 | u32 | primitive count | 1 |
| +0x10 | 0x12AC | ptr | primitive pointer array | @0x12B4 → @0x12B8 |

BannerFrame's model has no skeleton, and its sub-mesh has 0 bone indices.

### 10.3 Primitive

@0x12B8, **0x10 bytes**. In all 56 primitives the descriptor pointer array
starts right at +0x10. So SPICA's `GfxFace` fields `Flags` and `CommandAlloc`
are not in these files; SPICA reads them out of the arrays that follow.

| Offset | @ | Type | Meaning | BannerBG |
|---|---|---|---|---|
| +0x00 | 0x12B8 | u32 | index stream count | 1 |
| +0x04 | 0x12BC | ptr | index stream pointer array | @0x12C8 → @0x12D0 |
| +0x08 | 0x12C0 | u32 | buffer object count | 1 |
| +0x0C | 0x12C4 | ptr | `u32[]` runtime buffer objects | @0x12CC: [0] |

### 10.4 Index stream

@0x12D0, 0x2C bytes.

| Offset | @ | Type | Meaning | BannerBG |
|---|---|---|---|---|
| +0x00 | 0x12D0 | u32 | index format, GL enum: 0x1401 `u8`, 0x1403 `u16` | 0x1401 (`u8`) |
| +0x04 | 0x12D4 | u8 | primitive mode: 0 triangles; 1 strip, 2 fan, 3 geometry per SPICA's `PICAPrimitiveMode`, **unverified** | 0 |
| +0x05 | 0x12D5 | u8 | visible | 1 |
| +0x06 | 0x12D6 | u8[2] | padding (SPICA `[Padding(4)]`) | 0 |
| +0x08 | 0x12D8 | u32 | index buffer size in bytes | 444 |
| +0x0C | 0x12DC | ptr | index buffer, in IMAG | @0x3280 (IMAG+0) |
| +0x10 | 0x12E0 | u32 | runtime buffer object | 0 |
| +0x14 | 0x12E4 | u32 | runtime location flag | 0 |
| +0x18 | 0x12E8 | u32 | runtime command cache pointer | 0 |
| +0x1C | 0x12EC | u32 | runtime command cache size | 0 |
| +0x20 | 0x12F0 | u32 | runtime location address | 0 |
| +0x24 | 0x12F4 | u32 | runtime memory area | 0 |
| +0x28 | 0x12F8 | u32 | bounding volume (SPICA) | 0 |

Across the 56 index streams: all are mode 0 and visible. 45 are `u8` and 11
are `u16`, and the exporter picked `u8` exactly when the shape has at most
256 vertices. Every count is a multiple of 3, every index is in range, and
every runtime field is zero.

### 10.5 Vertex buffers

Each entry of the shape's vertex buffer array is one of these kinds:

| Type word | Kind (+0x08) | Seen | Meaning |
|---|---|---|---|
| 0x40000002 | 2 interleaved | every shape | one raw buffer in IMAG, stride, list of attributes |
| 0x80000000 | 1 fixed | 33 (24 normal, 8 color, 1 texcoord1) | one constant value for the whole shape |
| 0x40000001 | 0 none | only inside interleaved buffers (175) | attribute description |

A top-level 0x40000001 with its own raw buffer (SPICA's non-interleaved
`GfxAttribute`, raw buffer at +0x14/+0x18) does not occur in any HOME file:
**unverified**.

**Interleaved buffer**, BannerBG @0x12FC, 0x30 bytes:

| Offset | @ | Type | Meaning | BannerBG |
|---|---|---|---|---|
| +0x00 | 0x12FC | u32 | type | 0x40000002 |
| +0x04 | 0x1300 | u32 | usage | 21 (interleave) |
| +0x08 | 0x1304 | u32 | kind | 2 |
| +0x0C | 0x1308 | u32 | runtime buffer object | 0 |
| +0x10 | 0x130C | u32 | runtime location flag | 0 |
| +0x14 | 0x1310 | u32 | raw buffer size in bytes | 2448 |
| +0x18 | 0x1314 | ptr | raw buffer, in IMAG | @0x343C (IMAG+0x1BC) |
| +0x1C | 0x1318 | u32 | runtime location address | 0 |
| +0x20 | 0x131C | u32 | runtime memory area | 0 |
| +0x24 | 0x1320 | s32 | stride in bytes | 24 |
| +0x28 | 0x1324 | u32 | attribute count | 3 |
| +0x2C | 0x1328 | ptr | attribute pointer array | @0x132C → @0x1338, @0x136C, @0x13A0 |

**Attribute** inside an interleaved buffer, 0x34 bytes:

| Offset | Type | Meaning | @0x1338 | @0x136C | @0x13A0 |
|---|---|---|---|---|---|
| +0x00 | u32 | type | 0x40000001 | 0x40000001 | 0x40000001 |
| +0x04 | u32 | usage | 0 position | 3 color | 4 texcoord0 |
| +0x08 | u32 | kind | 0 | 0 | 0 |
| +0x0C..+0x23 | u32 ×6 | buffer object, location flag, raw size, raw pointer, location address, memory area (own buffer only; 0 here) | 0 | 0 | 0 |
| +0x24 | u32 | format, GL enum | 0x1406 float | 0x1401 u8 | 0x1406 float |
| +0x28 | u32 | component count | 3 | 4 | 2 |
| +0x2C | f32 | scale applied to every component | 1.0 | 0.003921569 (0x3B808081 = 1/255) | 1.0 |
| +0x30 | u32 | byte offset inside the vertex | 0 | 12 | 16 |

3dbrew reads +0x24 as a one-byte "data type 0..6". Those are the low bytes of
the GL enums 0x1400 (byte), 0x1401 (unsigned byte), 0x1402 (short) and
0x1406 (float); +0x25 is the 0x14. Across all 175 attributes the offsets pack
the vertex with no gaps, each attribute is aligned to its component size, and
the total equals the stride.

**Fixed attribute**, from `BannerDS_LZ.bin` @0x3DB4 since BannerBG has none.
0x20 bytes, with the values placed right after:

| Offset | Type | Meaning | BannerDS @0x3DB4 |
|---|---|---|---|
| +0x00 | u32 | type | 0x80000000 |
| +0x04 | u32 | usage | 1 normal |
| +0x08 | u32 | kind | 1 |
| +0x0C | u32 | format | 0x1406 |
| +0x10 | u32 | component count | 3 |
| +0x14 | f32 | scale | 0.0 (0 in all 33 fixed attributes) |
| +0x18 | u32 | value count | 3 |
| +0x1C | ptr | `f32[]` value | @0x3DD4: (0, 0, 1) |

**Usage values** (SPICA `PICAAttributeName`): 0 position, 1 normal,
2 tangent, 3 color, 4 texcoord0, 5 texcoord1, 6 texcoord2, 7 bone index,
8 bone weight, 9–20 user attributes 0–11, 21 interleave. Seen in HOME:
0, 1, 3, 4, 5 and 21.

**Formats** (+0x24 of an attribute, +0x0C of a fixed one) and their PICA200
attribute types (3dbrew `GPUREG_ATTRIBBUFFERS_FORMAT_LOW`): 0x1400 byte → 0,
0x1401 unsigned byte → 1, 0x1402 short → 2, 0x1406 float → 3. 0x1403 is
used only for indices; 0x140C fixed is listed by SPICA but never seen.

## 11. Skeleton and bones

BannerBG's skeleton is @0x13D4, 0x2C bytes long. Its magic is `SOBJ`, not
`CSKL`, and the same holds in all 14 skeletons.

| Offset | @ | Type | Meaning | BannerBG |
|---|---|---|---|---|
| +0x00 | 0x13D4 | u32 | type flags | 0x02000000 |
| +0x04 | 0x13D8 | char[4] | magic | `SOBJ` |
| +0x08 | 0x13DC | u32 | revision | 0 |
| +0x0C | 0x13E0 | ptr | name | "" |
| +0x10 | 0x13E4 | u32 | user data count | 0 |
| +0x14 | 0x13E8 | ptr | user data | null |
| +0x18 | 0x13EC | u32 | bone count | 2 |
| +0x1C | 0x13F0 | ptr | bone DICT | @0x1400 |
| +0x20 | 0x13F4 | ptr | root bone | @0x143C (bone 0 in all 14 skeletons) |
| +0x24 | 0x13F8 | u32 | scaling rule: 0 standard, 1 Maya, 2 Softimage (SPICA) | 0 (all) |
| +0x28 | 0x13FC | u32 | flags; SPICA: bit 1 = translation animation enabled | 0x2 (all) |

**Bone**, 0xE0 bytes (3dbrew's "skeleton data" entry):

| Offset | Type | Meaning | `root` @0x143C | `BG` @0x151C |
|---|---|---|---|---|
| +0x00 | ptr | name | `root` | `BG` |
| +0x04 | u32 | flags (below) | 0x19C | 0x1C4 |
| +0x08 | s32 | index | 0 | 1 |
| +0x0C | s32 | parent index, -1 = none | -1 | 0 |
| +0x10 | ptr | parent | null | @0x143C |
| +0x14 | ptr | first child | @0x151C | null |
| +0x18 | ptr | previous sibling | null | null |
| +0x1C | ptr | next sibling | null | null |
| +0x20 | f32[3] | scale | (1, 1, 1) | (0.969094, 0.969094, 0.861467) |
| +0x2C | f32[3] | rotation, radians | (0, 0, 0) | (0, 0, 0) |
| +0x38 | f32[3] | translation | (0, 0, 44.786) | (0, 0.0309238, -43.3993) |
| +0x44 | f32[3][4] | local matrix | T(0, 0, 44.786) | diag(0.969094, 0.969094, 0.861467), T(0, 0.0309238, -43.3993) |
| +0x74 | f32[3][4] | world matrix: identity in the file, filled at run time | identity | identity |
| +0xA4 | f32[3][4] | inverse bind-pose world matrix | T(0, 0, -44.786) | diag(1.031892, 1.031892, 1.160811), T(0, -0.03191, -1.609696) |
| +0xD4 | u32 | billboard mode (SPICA: 0 off, 2 world, 3 world viewpoint, 4 screen, 5 screen viewpoint, 6 Y axial, 7 Y axial viewpoint) | 0 | 0 |
| +0xD8 | u32 | user data count | 0 | 0 |
| +0xDC | ptr | user data DICT | null | null |

Verified over all 85 bones in 14 skeletons, 14 of which have a non-zero
rotation:

- local = T · Rz · Ry · Rx · S (X rotated first), to 4e-8; the X-Y-Z
  product order fails by 0.18;
- inverse bind world = inverse(local of the root · … · local of the bone), to
  1.2e-6;
- the file's world matrix is identity in every bone.

Bone flags (SPICA `GfxBoneFlags`). The checked bits match the transforms in
all 85 bones:

| Bit | Name | Check |
|---|---|---|
| 0 | is identity | set ⇔ T = 0, R = 0, S = 1 |
| 1 | translation zero | set ⇔ T = 0 |
| 2 | rotation zero | set ⇔ R = 0 |
| 3 | scale volume one | set ⇔ S = (1, 1, 1) here (the name suggests Sx·Sy·Sz = 1; no bone separates the two) |
| 4 | scale uniform | set ⇔ Sx = Sy = Sz |
| 5 | segment scale compensate | never set |
| 6 | needed for rendering | set ⇔ some sub-mesh lists the bone |
| 7 | local matrix calculate | set in all |
| 8 | world matrix calculate | set in all |
| 9 | has skinning matrix | never set |

`root` 0x19C = rotation zero | scale volume one | scale uniform | bits 7, 8.
`BG` 0x1C4 = rotation zero | needed for rendering | bits 7, 8.

## 12. Where BannerBG's pieces sit

DATA, in file order:

| @ | What |
|---|---|
| 0x0014 | DATA header and 15 slots |
| 0x0094, 0x00C0, 0x011C, 0x0158 | DICTs: models, textures, skeletal anims, material anims |
| 0x01B4 | CMDL `BannerBG` (0xE4 bytes) |
| 0x0298, 0x029C | mesh and shape pointer arrays |
| 0x02A0, 0x02EC | animation-group DICT, material DICT |
| 0x0318–0x0BE7 | animation groups, their element DICTs and elements |
| 0x0BE8 | mesh |
| 0x0C68 | MTOB `mt_BG` |
| 0x1210 | shape, then: sub-mesh array 0x1254, vertex-buffer array 0x1258, OBB 0x125C, sub-mesh 0x129C, bone indices 0x12B0, primitive array 0x12B4, primitive 0x12B8, index-stream array 0x12C8, buffer objects 0x12CC, index stream 0x12D0, interleaved buffer 0x12FC, attribute array 0x132C, attributes 0x1338/0x136C/0x13A0 |
| 0x13D4 | skeleton, bone DICT 0x1400, bones 0x143C and 0x151C |
| 0x15FC–0x176B | 4 TXOBs |
| 0x176C–0x2AFB | 6 CANMs (SceneIn 0x176C, SceneOut 0x19B0, AppPause 0x1C6C, AppQuit 0x21DC, AppRestart 0x2468, Loop 0x298C) |
| 0x2AFC–0x3277 | string pool and padding |

IMAG contents (from @0x3280):

| IMAG offset | @ | Size | What |
|---|---|---|---|
| 0x000 | 0x3280 | 444 | BannerBG index stream (`u8`), SHA-256 `83bc76c3888cfe3c251f57fd697d2799971c76361e5ef9e029044a03001b2662` |
| 0x1BC | 0x343C | 2448 | BannerBG vertices, 102 × 24, SHA-256 `156fb73af72311b556578a6ab99bc44fec050fdb9f8b9c3b3738093951001012` |
| 0xB4C | 0x3DCC | 52 | zero padding up to a 128-byte boundary |
| 0xB80 | 0x3E00 | 4096 | `BG_64_00` image |
| 0x1B80 | 0x4E00 | 65536 | `BG_CapMask_00` image |
| 0x11B80 | 0x14E00 | 512 | `BG_DefBase_00` image |
| 0x11D80 | 0x15000 | 64 | `BG_DmyApp_00` image (ends at the file end) |

A rewrite that moves IMAG contents must fix every DATA pointer into IMAG.
They are self-relative from their DATA fields.

## 13. BannerBG decoded

- **Model** `BannerBG`: skeletal (0x40000092); identity transform; visible;
  3 animation groups; 1 material (`mt_BG`); 1 mesh; 1 shape; no mesh-node
  visibility.
- **Mesh 0** @0xBE8: shape 0, material 0 (`mt_BG`), visible, render priority
  0, mesh-node index -1, mesh-node name `BG`.
- **Shape 0** @0x1210: position offset (0, 0, 0); OBB center
  (3.92955e-06, 1, -12.7051), identity orientation, size (56.458, 40,
  14.5898); 1 sub-mesh; 1 interleaved vertex buffer; no fixed attributes.
- **Sub-mesh 0**: bone indices [1] = `BG`, skinning 0 (none), 1 primitive with
  1 index stream.
- **Index stream**: `u8` (0x1401), triangles, visible, 444 indices = 148
  triangles. Indices span 0–101 and all 102 vertices are used.
- **Vertices**: 102, stride 24, IMAG+0x1BC.

  | Attribute | Format | Components | Scale | Offset | Range in the data |
  |---|---|---|---|---|---|
  | position | float | 3 | 1 | 0 | x ±28.229, y −19…21, z −20…−5.4102 |
  | color | u8 | 4 | 1/255 | 12 | R ∈ {0, 40, 51, 204, 215, 255}; G always 0; B ∈ {0, 255}; A always 255 |
  | texcoord0 | float | 2 | 1 | 16 | u −0.3136…1.2511, v −0.0859…1.0859 |

  There is no normal, tangent, bone index or bone weight. The surface is a
  curved wall, concave toward the camera: z = −20 at x = 0, rising to −5.41
  at x = ±28.229 (19 x columns), with 6 rows in y. How the material uses the
  color channels is a question for the material/TEV spec.
- **Winding.** Seen from the BannerCamera (position (0, 1, 44.786), aiming at
  (0, 1, 0)), 146 triangles are clockwise. The other 2 have zero area: three
  collinear vertices at x = ±23.04, z = −12.79. `mt_BG`'s baked
  `GPUREG_FACECULLING_CONFIG` command (MTOB +0x110, @0xD78: value 1 for
  register 0x040) culls counter-clockwise triangles in Zakuro's rasterizer
  (`raster.rs`: mode 1 keeps clockwise, "as seen with y pointing up"), so the
  wall is drawn. This assumes the projection does not mirror; the camera
  spec covers that.
- **Skeleton**: `root` (bone 0) and `BG` (bone 1, child of `root`), with the
  values in the bone table above.

### Comparison files

| File | Model | Shapes | Notes |
|---|---|---|---|
| `BannerBGmask_LZ.bin` | `BannerBGmask`, 0x40000092 | 1 | same layout offsets as BannerBG (CMDL @0x1B4, skeleton @0x13D4); 198 vertices × 24 in the same 3 attributes; 840 `u8` indices; OBB center (0, 1, -10), size (59.916, 40, 20); bones `root` 0x19C and `BG` 0x1DC |
| `BannerFrame_LZ.bin` | `BannerFrame`, 0x40000012 | 1 | no skeleton, CMDL 0xE0 bytes; sub-mesh with 0 bone indices; 994 vertices, position only (stride 12); 5952 `u16` indices; render priority 10 |
| `BannerFolder_LZ.bin` | `BannerFolder`, 0x40000092 | 8 | 12 bones, 7 materials; strides 20–44; fixed normal and color attributes; mixed `u8`/`u16` indices; non-identity OBBs |
| `Textures_LZ.bin` | none | — | textures slot only (4 TXOBs) |

## 14. Skinned or bone-attached, and what SceneIn/SceneOut move

**BannerBG is bone-attached, not skinned.** Its only sub-mesh has skinning
mode 0 and exactly one bone index (1, `BG`). The vertex buffer has no bone
index or bone weight attribute. Bone `BG` has flag 6 (needed for rendering)
set and flag 9 (has skinning matrix) clear. The positions are in `BG`'s local
frame. SPICA.Rendering (`Mesh.cs`) transforms a sub-mesh that is not smooth
by the current world matrix of its bone, with no inverse bind matrix. Its
translated default vertex shader (`DefaultVertexShader.txt`, else-branch of
`proc_transform_matrix`) uses `UnivReg[0..2]` alone when neither `IsSmoSk`
nor `IsRgdSk` is set. HOME's own handling is **unverified**; the shader spec
covers it.

Both skeletal animations bind to group `SkeletalAnimation` and animate only
bone **`BG`**. `root` is never animated, so it keeps its local T(0, 0,
44.786).

| Animation | CANM @ | Frames, loop | Element | Channels present (flags @ element +0x00) |
|---|---|---|---|---|
| `BannerBG_SceneIn` | 0x176C | 20, one-time (0) | `BG` @0x17C0, transform (type 5) | scale X/Y/Z, translation Y/Z curves. Rotation X/Y/Z/W and translation X absent (flags 0xF80000), so translation X keeps its bind value 0 |
| `BannerBG_SceneOut` | 0x19B0 | 40, one-time (0) | `BG` @0x1A04, transform | same channels, flags 0xF80000 |

Curve end points (first and last keys; full curves in the animation spec):

| | Scale X, Y | Scale Z | Translation Y | Translation Z |
|---|---|---|---|---|
| SceneIn frame 0 → 20 | 0.777129 → 1 | 0.001 → 1 | 0.223 → 0 | −34.786 → −44.786 |
| SceneOut frame 0 → 40 | 1 → 0.777129 | 1 → 0.001 | 0 → 0.223 | −44.786 → −34.786 |

Consequences for drawing:

- At the end of SceneIn, and at the start of SceneOut, `BG`'s local matrix
  is T(0, 0, −44.786). Its world matrix is then root.local · BG.local =
  **identity**, so the vertex positions are world positions: the wall sits
  50–65 units in front of the camera.
- At SceneIn frame 0 the wall is flattened (scale z 0.001) at world z ≈ 10
  and scaled to 77.7%, then it unfolds and backs away over 20 frames.
- The bind pose stored in the file, S (0.969, 0.969, 0.861) and T (0,
  0.0309, −43.399), is neither end of these animations. Drawing without the
  animation gives a slightly smaller, nearer wall (world S (0.969, 0.969,
  0.861), T (0, 0.031, 1.387)). That HOME holds SceneIn's last frame
  afterwards is **unverified**, but SceneOut starting from exactly that pose
  supports it.

## 15. How the runtime uses it

**Names.** HOME's `.code` is BLZ-compressed. Decompressed in memory (to
2,367,488 bytes), it contains these strings, at offsets in the decompressed
code:

- `3D/BannerBG_LZ.bin` @0x187F14, `3D/BannerBGmask_LZ.bin` @0x187F28,
  `3D/BannerFrame_LZ.bin` @0x187F40, `3D/BannerCamera_LZ.bin` @0x187F68;
- `BannerBGmask` @0x1873CC, `BannerBG` @0x1873DC;
- `BannerBG_SceneIn`, `_SceneOut`, `_Loop`, `_AppPause`, `_AppQuit` and
  `_AppRestart` @0x14E134–0x14E194.

So HOME refers to the model and all six animations by these exact names,
which a mod should keep. `SkeletalAnimation`, `MaterialAnimation` and
`DefaultShader` do not occur in the code. Those bindings go through names
stored in the files: the CANM target group names, the CMDL animation groups
and the material's shader reference. The code that walks the DICTs was not
located, so the lookup convention rests on the trees in the files (section
6).

**Shader inputs.** `nwgfx_DefaultShader_LZ.bin` declares, in its symbol
tables:

- inputs `aPosition`, `aNormal`, `aTangent`, `aColor`, `aTexCoord0..2`,
  `aBoneIndex`, `aBoneWeight`, `aUserAttribute0..2`;
- uniforms `WrldMtx`, `NormMtx`, `PosOffs`, `IrScale`, `UnivReg`, `ViewMtx`,
  `ProjMtx` and others;
- bools `IsSmoSk`, `IsRgdSk`, `IsVertA`, `IsBoneW` and others.

SPICA.Rendering drives the same names. The uniform and input register
numbers, and HOME's real values, belong to the shader spec. Per SPICA, the
geometry supplies:

- `PosOffs` = shape position offset → (0, 0, 0). The shader computes
  `PosOffs + IrScale[0].x · aPosition`.
- `IrScale[0]` = (position, normal, tangent, color) scales and `IrScale[1]` =
  (texcoord0, texcoord1, texcoord2, bone weight) scales (`Mesh.SetScale`).
  For BannerBG: (1, 0, 0, 1/255) and (1, 0, 0, 0), absent attributes left at
  0. The translated shader treats normal or tangent scale ≠ 0 as "the mesh
  has normals/tangents".
- `UnivReg[0..2]` = world matrix of sub-mesh bone 0 (`BG`), identity at
  rest.
- bools: `IsSmoSk` = `IsRgdSk` = false (skinning 0); `IsVertA` = true (a
  color attribute exists); `IsBoneW` = false (`H3DMesh.UpdateBoolUniforms`).

**Draw order.** The mesh render priority (+0x25) and the material's
translucency kind order meshes (SPICA: `Priority`, `Layer`). BannerBG has a
single mesh, so order does not matter.

**Getting the mesh to Zakuro's GPU.** A sufficient register setup, from
3dbrew `GPU/Internal_Registers` and zakuro-gpu `registers.rs` /
`raster.rs::read_loaders`, `attribute_format`, `loc_register`. Assume the
IMAG contents are copied to physical address *B* (8-aligned), with attribute
slots 0, 1, 2 = position, color, texcoord0 in buffer order:

| Register | Value | Why |
|---|---|---|
| 0x200 `GPUREG_ATTRIBBUFFERS_LOC` | *B* >> 3 | Zakuro reads (reg & 0x0FFFFFFF) << 3 |
| 0x201 `..._FORMAT_LOW` | 0x000007DB | per slot, type \| (components − 1) << 2: float×3 = 0xB, u8×4 = 0xD, float×2 = 0x7 |
| 0x202 `..._FORMAT_HIGH` | 0x20000000 | fixed-attribute mask 0, attribute count − 1 = 2 in bits 28–31 |
| 0x203 loader 0 offset | 0x1BC | vertex buffer at IMAG+0x1BC |
| 0x204 loader 0 config 1 | 0x00000210 | components: attribute 0, 1, 2 |
| 0x205 loader 0 config 2 | 0x30180000 | 24 bytes per vertex (bits 16–23), 3 components (bits 28–31) |
| 0x227 `GPUREG_INDEXBUFFER_CONFIG` | 0x00000000 | offset 0 from *B*; bit 31 clear = `u8` indices |
| 0x228 `GPUREG_NUMVERTICES` | 444 | index count |
| 0x22A `GPUREG_VERTEX_OFFSET` | 0 | |
| 0x25E `GPUREG_PRIMITIVE_CONFIG` bits 8–9 | 0 | triangles; bits 0–3 come from the shader's output count |
| 0x22F `GPUREG_DRAWELEMENTS` | 1 | trigger |

Notes on this setup:

- No padding components are needed: Zakuro aligns each attribute to its
  component size, and BannerBG's offsets 0/12/16 already are.
- The shader input permutation (`0x2BB`/`0x2BC`, Zakuro
  `SHADER_INPUT_MAP_LOW/HIGH`) maps slots to the inputs named above; the
  shader spec gives it.
- 3dbrew says the official driver sets `GPUREG_GEOSTAGE_CONFIG` (0x229) bit 8
  for indexed triangle draws. Zakuro reads only its bits 0–1 (`raster.rs`,
  `lib.rs`).
- HOME's runtime builds equivalent commands itself. The zeroed command-cache
  and attribute-scale-command fields in the index stream and the mesh are
  filled at run time. Its exact command stream is **unverified**.

## 16. Differences from SPICA and 3dbrew

- DATA has 15 slots, not SPICA's 16 (section 4).
- The OBB starts with a type word that SPICA's `GfxBoundingBox` does not read
  (10.1).
- A primitive is 0x10 bytes; SPICA's `GfxFace.Flags` and `CommandAlloc` are
  not there (10.3).
- Animation-group elements are larger than SPICA's `GfxAnimGroupElement`
  (8.1).
- The skeleton's magic is `SOBJ`. 3dbrew's "Matrix 2, identity?" in the
  bone entry is the world matrix, filled at run time (11).
- 3dbrew's one-byte "component data type" at attribute +0x24 is the low byte
  of a `u32` GL enum (10.5).

## 17. Unknowns

- What slots 3, 7 and 11–14 hold (materials, fogs, visibility, camera and
  light animations, emitters): no HOME file uses them. The order is taken
  from 3dbrew and Nouwt.
- Type-flag bits other than CMDL bit 7.
- CMDL +0x18/+0x1C (branch visibility), the children list, flags bit 1, face
  culling (+0xD8) and layer id (+0xDC): constant in every file, meaning per
  SPICA only.
- Mesh primitive index (+0x28) and the exact use of render priority at run
  time.
- Primitive modes 1–3, the 0x140C fixed format, top-level non-interleaved
  attributes, blend shapes, mesh-node visibility entries and user data: none
  in HOME's files.
- Bone flag bit 3 (volume one) as opposed to "scale = 1"; billboard mode 1
  (seen in 4 bones of other files, absent from SPICA's enum).
- Animation-group element fields beyond those listed in 8.1.
- HOME's actual uniform values and command lists for this mesh, including
  whether `UnivReg` really receives `BG`'s world matrix as SPICA assumes, and
  whether SceneIn's last frame is held while the menu is idle.

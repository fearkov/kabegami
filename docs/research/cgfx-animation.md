# CGFX animations (CANM), revision 0x05000000

## 1. Purpose

This spec describes the animation object (`CANM`) of revision-5 CGFX files: what its members
are, how curves and keys are stored, and how the HOME Menu evaluates a curve at a frame. It
also decodes every animation in `3D/BannerBG_LZ.bin` (the default top-screen wallpaper), with
`BannerBG_Loop` in full, compares it with `3D/BannerBGmask_LZ.bin`, and checks SPICA's reader
against the bytes. Kabegami needs this to play the wallpaper's drift the way the console does,
and to write a changed `BannerBG_Loop` back that the console still reads correctly.

Each fact carries a source tag:

| Tag | Meaning |
|---|---|
| **[file]** | Read in the named file at the given offset. Offsets are in the LZ11-decompressed data. |
| **[code]** | Seen in HOME 11.17 EUR's own ARM code at the given address (see §3.1). |
| **[SPICA]** | Taken only from SPICA's sources (Unlicense). |
| **[unverified]** | Not confirmed by either the files or the code. |

Inputs (the user's own dumps, read in place and never copied):

| File | Size | SHA-256 | Decompressed | SHA-256 of decompressed data |
|---|---|---|---|---|
| `romfs/3D/BannerBG_LZ.bin` | 7212 | `27d58c2113d2c2d46e3bcc36bb2ae56c35e19d9823488287b6759df998108711` | 86080 | `092c8682d0cfabf0a1823a8e3a2c12556515c437afba2aa6f6ac7fc4d5e34595` |
| `romfs/3D/BannerBGmask_LZ.bin` | 8354 | `d8c3cf350e2af35263f640a80144a40aa1d9a1b878f1b6eb872be0b4ec669541` | 23360 | `d9139c60d5d608704b26252699a275f034ba04640859e0a8b1b9037047e1d233` |
| `exefs-code.bin` (HOME `.code`) | 1393164 | `e77d417b8609f891aa5a9ba20e26a087afdb254ef6e7f27fd8a4162b24cd6adc` | 2367488 (0x242000) | `5f4d8d0e80d8e7d6fafcefa0630c5106473ade4658585cfb499a6ec3324d89a7` |

The other 16 CGFX files in `romfs/3D/` were scanned for statistics and for features that
BannerBG lacks. All 18 are revision `0x05000000`, which is the u32 at file offset 0x08.

Conventions:
- All values are little-endian.
- "rel" means a signed 32-bit pointer relative to the address of the pointer field itself.
  The value 0 means null.
- f32 is IEEE single precision.

## 2. Byte layouts

### 2.1 Where the animations are: DATA slots

The DATA block (magic at 0x14 in every file) holds **15** slots of 8 bytes each, at
DATA+0x08 through DATA+0x7F. Each slot is a u32 count followed by a rel pointer to a `DICT`.
In all 18 HOME files the first DICT starts at DATA+0x80 (file offset 0x94) **[file]**.
3dbrew and SPICA list a 16th slot. In these files that position holds the first DICT's
`'DICT'` magic.

| Slot | Contents | Evidence | BannerBG_LZ.bin |
|---|---|---|---|
| 9 | Skeletal animations (`CANM`, group `SkeletalAnimation`) | [file]; [code] 0x1f8268 reads file+0x64 and file+0x68 to find skeletal anims by name | count 2 at 0x64, DICT at 0x11c |
| 10 | Material animations (`CANM`, group `MaterialAnimation`) | [file]; [code] 0x1f8078 reads file+0x6c and file+0x70 | count 4 at 0x6c, DICT at 0x158 |
| 11, 12, 13 | Visibility, camera and light animations (3dbrew order) | Empty in all 18 files [unverified] | 0 |
| 14 | Emitters (3dbrew). SPICA puts fog animations here and emitters in a 16th slot | Empty in all 18 files [unverified] | 0 |

Slots seen in use across the 18 files **[file]**:

| Slot | Object |
|---|---|
| 0 | CMDL |
| 1 | TXOB |
| 2 | LUTS |
| 4 | SHDR |
| 5 | CCAM |
| 6 | CFLT |
| 8 | CENV |
| 9, 10 | CANM |

DICT, as used by the member dictionary. The Patricia-tree fields are part of the container
spec, so only what the runtime needs here is listed.

| Offset | Type | Meaning | Loop's member DICT at 0x29b4 [file] |
|---|---|---|---|
| +0x00 | char[4] | `'DICT'` | `'DICT'` |
| +0x04 | u32 | Size in bytes, 0x1C + 0x10 × count | 0x4c |
| +0x08 | u32 | Entry count | 3 |
| +0x0C | node | Root node: reference bit 0xFFFFFFFF, no name, no data | left 2, right 0 |
| +0x1C + 0x10·i | node | Entry *i*: u32 reference bit, u16 left, u16 right, rel name, rel data | see §4.6 |

The runtime reads member *i*'s data pointer at DICT+0x28+0x10·i **[code]** 0x2e1090 and 0x18c6bc.
It finds names with the DICT name lookup at 0x20b8ac **[code]**. That lookup is called with the
string and its length; its internals were not read.

### 2.2 CANM header (0x28 bytes)

The member DICT follows at +0x28 in all six BannerBG animations **[file]**.

| Offset | Type | Meaning | BannerBG_Loop at 0x298c [file] |
|---|---|---|---|
| +0x00 | char[4] | Magic `'CANM'` | `'CANM'` |
| +0x04 | u32 | Revision | 0x05000000 |
| +0x08 | rel → string | Animation name | `BannerBG_Loop` (→0x2b91) |
| +0x0C | rel → string | Target animation group name | `MaterialAnimation` (→0x2bc5) |
| +0x10 | u32 | Loop mode: 0 = one time, 1 = loop. The runtime reads only the low byte (`ldrb`) [code 0x18c3fc] | 1 |
| +0x14 | f32 | Frame count. This becomes the frame controller's end frame [code 0x18c3e4] | 600.0 |
| +0x18 | u32 | Member count [code 0x18c650] | 3 |
| +0x1C | rel → DICT | Member dictionary, keyed by member path | →0x29b4 |
| +0x20 | u32 | Metadata count? 0 in all 37 HOME animations [unverified] | 0 |
| +0x24 | rel | Metadata DICT? 0 in all 37 [unverified] | 0 |

### 2.3 Member (animation element)

| Offset | Type | Meaning | Loop member at 0x2a24 [file] |
|---|---|---|---|
| +0x00 | u32 | Flags. Their meaning depends on the primitive type (§2.4) | 0x0 |
| +0x04 | rel → string | **Path**. This is the key the runtime binds by (§3.3) | `Materials["mt_BG"].TextureCoordinators[1].Translate` (→0x30a1) |
| +0x08 | rel → string | Target object name (material or bone) | `mt_BG` (→0x2bd7) |
| +0x0C | rel → string or 0 | Member name inside the object. 0 for bone members | `TextureCoordinators[1]` (→0x31d3) |
| +0x10 | u32 | Primitive type (§2.4) | 3 (Vector2D) |
| +0x14 | … | Content (§2.4) | X → 0x2a40, Y → 0x2a7c |

In the skeletal animations the path and the object name point to the same string, and +0x0C
is 0. For example, member `BG` at 0x17c0 has path and object at →0x2be2 **[file]**.

### 2.4 Content by primitive type

The content starts at member+0x14. Each component has a 4-byte **slot**:
- If the component is constant, the slot holds an f32 (an s32 for Integer).
- If it is animated, the slot holds a rel pointer to a curve (§2.6).
- If it is not animated, the slot is ignored. It is 0 in all files.

The runtime's per-type evaluator is the switch at 0x2e7174 **[code]**.

| Type | Name (SPICA) | Slots and extra fields | Flag bits | HOME 3D files | Runtime |
|---|---|---|---|---|---|
| 0 | Float | 1 slot | bit 0: constant | not used | [code] 0x2e71c0 tests bit 0 only |
| 1 | Integer | 1 slot: s32 constant, or rel → integer curve (§3.6) | bit 0: constant | not used | [code] 0x2e71ec → 0x1ab74c |
| 2 | Boolean | 1 slot: rel → boolean curve | bit 0: constant; bit 1: the constant's value | not used | [code] 0x2e721c → 0x1ab78c |
| 3 | Vector2D | X at +0x14, Y at +0x18 | bits 0–1: X/Y constant; bits 2–3: X/Y not animated | 46 members | [file] + [code] 0x2e7250 |
| 4 | Vector3D | X, Y, Z at +0x14…+0x1C | bits 0–2 constant; bits 3–5 not animated | not used | [code] 0x2e6f20 |
| 5 | Transform | 10 slots at +0x14…+0x38: ScaleX/Y/Z, RotateX/Y/Z, RotateW (unused), TranslateX/Y/Z | bits 6–15: slot 0–9 constant; bits 16–25: slot 0–9 not animated; bits 0–5 are 0 in all 29 members | 29 members | [file] + [code] 0x2e74ac. RotateW is never tested. A component that is not animated takes the value from a base transform the caller passes in (presumably the bone's own) |
| 6 | RGBA | R, G, B, A at +0x14…+0x20 | bits 0–3 constant; bits 4–7 not animated | 24 members | [file] + [code] 0x2e7394 |
| 7 | Texture | Value slot at +0x14 (f32 constant or float curve); u32 texture count at +0x18; rel at +0x1C → *count* rel pointers to texture references (§2.5) | bit 0: constant | 8 members | [file] + [code] 0x2e7310 |
| 8 | QuatTransform | SPICA: 3 rel pointers to baked arrays | — | not used | Not handled by 0x2e7174 (types ≥ 8 do nothing) [unverified] |
| 9 | MtxTransform | SPICA: baked 3×4 matrices | — | not used | as above [unverified] |

Flag values seen in BannerBG **[file]**:

| Flags | Type | Meaning |
|---|---|---|
| 0x78 | RGBA | R, G, B not animated; A constant |
| 0x80 | RGBA | A not animated; R, G, B curves |
| 0x70 | RGBA | R, G, B not animated; A curve |
| 0x0 | Vector2D | X and Y are curves |
| 0x3 | Vector2D | X and Y constant |
| 0x1 | Texture | Constant |
| 0xf80000 | Transform | Rotate X/Y/Z/W and TranslateX not animated; scales and TranslateY/Z are curves |

### 2.5 Texture reference (in a Texture member's list)

| Offset | Type | Meaning | Loop's entry at 0x2adc [file] |
|---|---|---|---|
| +0x00 | u32 | Object type | 0x20000004 (texture reference) |
| +0x04 | char[4] | `'TXOB'` | `'TXOB'` |
| +0x08 | u32 | Revision | 0x05000000 |
| +0x0C | rel → string | Name | `""` (→0x3201) |
| +0x10, +0x14 | u32, rel | Metadata count and DICT | 0, 0 |
| +0x18 | rel → string | Path, the name of the texture in DATA slot 1 | `BG_64_00` (→0x2b05) |
| +0x1C | u32 | Texture pointer, filled in at run time | 0 |

The runtime converts the value to an int by truncating toward zero (`vcvt.s32.f32`). It
ignores negative indices and indices ≥ count. Otherwise it binds list entry *index*
**[code]** 0x2e7334–0x2e7380.

### 2.6 Float curve

| Offset | Type | Meaning | Loop X curve at 0x2a40 [file] | AppQuit TexCoord0.Scale X at 0x230c [file] |
|---|---|---|---|---|
| +0x00 | f32 | Start frame | 0 | 0 |
| +0x04 | f32 | End frame | 600 | 20 |
| +0x08 | u8 | Pre-repeat method, used for frames before the start | 0 | 0 |
| +0x09 | u8 | Post-repeat method, used for frames at or after the end | 0 | 0 |
| +0x0A | u16 | Padding | 0 | 0 |
| +0x0C | u32 | Curve flags (below) | 0x4 | 0x0 |
| +0x10 | u32 | Segment count | 1 | 2 |
| +0x14 | rel[count] | Segment pointers, stored inline | →0x2a58 | →0x2328, →0x234c |

Repeat methods (SPICA's names; the runtime's function tables are 0x317fd4 for pre and
0x317fe4 for post **[code]**):

| Value | Name | Seen in HOME files |
|---|---|---|
| 0 | None | 0/0 on 112 of 122 curves, including all BannerBG curves |
| 1 | Repeat | pre/post 0/1 on 6 curves; 1/3 on 2 |
| 2 | MirroredRepeat | not used |
| 3 | RelativeRepeat | 3/3 on 2 curves (BannerPrize) |

Curve flags **[code]** 0x208778–0x2087f4:

| Bit | Meaning | Data |
|---|---|---|
| 0 | Composite. The value is curve(rel at +0x10) + curve(rel at +0x14), both evaluated at the repeat-mapped frame | Not in any HOME file |
| 1 | Constant curve. The f32 value is at +0x10 | Not in any HOME file. SPICA calls this `IsConstantValue` |
| 2 | Single segment. The runtime reads only the pointer at +0x14 and ignores the count | Set on all 106 single-segment curves |
| none | Multi-segment. Count at +0x10, inline pointers from +0x14 | All 16 multi-segment curves (2 or 4 segments) have flags 0 |

SPICA calls bit 2 `IsQuantizedCurve`, which is not what it means.

### 2.7 Segment

Common header:

| Offset | Type | Meaning | Loop X segment at 0x2a58 [file] |
|---|---|---|---|
| +0x00 | f32 | Segment start frame (absolute) | 0 |
| +0x04 | f32 | Segment end frame (absolute) | 600 |
| +0x08 | u32 | Segment flags: bit 0 constant; bits 2–4 interpolation (0 step, 1 linear, 2 hermite); bits 5–7 key format (§2.8); bit 1 is 0 everywhere [code 0x20886c–0x208894] | 0xc4 (linear, format 6) |

A **constant segment** (flags bit 0) is 16 bytes. +0x0C holds its f32 value **[file]** + **[code]** 0x208870.
For example, AppQuit's second segment at 0x234c has start 20, end 20, flags 0x1, and value 1.0 at 0x2358.

A **keyed segment** continues as follows:

| Offset | Type | Meaning | Loop X segment [file] |
|---|---|---|---|
| +0x0C | u16 | Key count. The runtime reads a u16 (`ldrh`) [code 0x300130] | 2 |
| +0x0E | u16 | 0 in all 136 keyed segments; not read [unverified] | 0 |
| +0x10 | f32 | 1 / (end − start). Exactly f32(1/(end − start)) in 133 of 136 segments; 1 ulp above in the 3 whose start is −0.03. Only the key search's starting index uses it (§3.5) | 0.0016666667 at 0x2a68 |
| +0x14 | f32 | **Quantized formats only:** value scale | — |
| +0x18 | f32 | **Quantized formats only:** value offset. Value = offset + raw × scale | — |
| +0x14 (float formats) or +0x1C (quantized) | key[count] | Keys, with **frames relative to the segment start** | (0, −0.357778) at 0x2a6c, (600, −0.6911) at 0x2a74 |

There is no frame scale field. The only quantized segments in HOME's files are two
UnifiedHermite32 segments in BannerGift. The first, at 0x6268, has:
- start 0, end 50, flags 0xa8, count 3
- inv 0.02 at 0x6278
- scale 0.0 at 0x627c and offset 1.0 at 0x6280
- three 4-byte keys at 0x6284–0x628f

The next segment starts at 0x6290 **[file]**. The UnifiedHermite32 evaluator reads scale at
segment+0x14, offset at +0x18 and keys from +0x1C **[code]** 0x2ffac0. So do Hermite64
(0x2fffcc), Hermite48 (0x2ffe64), UnifiedHermite48 (0x2ffc20) and StepLinear32 (0x2ff1c0).

Invariants in all 136 keyed HOME segments **[file]**:
- The first key is at relative frame 0.
- The last key is at relative frame end − start.
- The key count's high half is 0.
- inv equals 1/(end − start), to within 1 ulp.

### 2.8 Key formats (segment flags bits 5–7)

| # | Name (SPICA) | Bytes per key | Layout | HOME files | Source |
|---|---|---|---|---|---|
| 0 | Hermite128 | 16 | f32 frame, f32 value, f32 in-slope, f32 out-slope | 52 segments (flags 0x08) | [file] + [code] 0x300130 |
| 1 | Hermite64 | 8 | u32: frame in bits 0–11 (integer), raw value in bits 12–31; s16 in-slope; s16 out-slope; slopes × 1/256 | none | [code] 0x2fffcc |
| 2 | Hermite48 | 6 | u8 frame; u16 raw value; 24 bits: in-slope in bits 0–11, out-slope in bits 12–23, both signed × 1/32 | none | [code] 0x2ffe64 |
| 3 | UnifiedHermite96 | 12 | f32 frame, f32 value, f32 slope (used on both sides) | 32 segments (0x68) | [file] + [code] 0x2ffd6c |
| 4 | UnifiedHermite48 | 6 | u16 frame × 1/32; u16 raw value; s16 slope × 1/256 | none | [code] 0x2ffc20 |
| 5 | UnifiedHermite32 | 4 | u8 frame; 24 bits: raw value in bits 0–11, signed slope in bits 12–23 × 1/32 | 2 segments (0xa8) | [file] + [code] 0x2ffac0 |
| 6 | StepLinear64 | 8 | f32 frame, f32 value | 50 segments (0xc0 step ×11, 0xc4 linear ×39) | [file] + [code] 0x2fed00, 0x2ff304 |
| 7 | StepLinear32 | 4 | u32: frame in bits 0–11, raw value in bits 12–31 | none | [code] 0x2ff1c0, linear |

Slopes are in value units per frame. The evaluator table at 0x33da5c **[code]** has 8 step
entries, 8 linear entries and 6 hermite entries; formats 6 and 7 have no hermite entry.
Interpolation values 3–7 would index past the table.

## 3. How the runtime uses it

### 3.1 Source

The HOME `.code` is BLZ-compressed. It was decompressed in memory, never written to disk,
and disassembled with capstone, assuming the usual load address 0x00100000. The vtables and
literal pools inside the image are consistent with that base. Every address below is in
that image. The code is NW4C (strings such as `nw::lyt::Layout::Animate` are present);
NW4C's own source was not used.

### 3.2 Loading the BannerBG animations

The BG object's set-up at 0x24e088 looks up six animations by name. The names are inline
strings at 0x24e134–0x24e1a7. It looks up `BannerBG_SceneIn` and `BannerBG_SceneOut` through
0x1f81d8 (skeletal, slot 9), and `BannerBG_Loop`, `BannerBG_AppPause`, `BannerBG_AppQuit` and
`BannerBG_AppRestart` through 0x1f8044 (material, slot 10). It stores them at object
+0x50…+0x64.

It then starts the Loop at once (0x24e118–0x24e130) with HOME's Play(2). That method,
0x250298, sets the frame step to +1.0 through 0x2502e4; modes 1 and 3 would set −1.0.

For each animation 0x1f8044 does the following **[code]**:
1. Builds a 0x6c-byte evaluator (constructor 0x1851f0, final vtable 0x31fafc).
2. Calls its set-up method, vtable +0x38 = 0x184edc. That method first calls 0x18c3c0
   (§3.4), then allocates the result cache.
3. Binds it through vtable +0x0C = 0x18c62c (§3.3) to the model's material binding at
   model+0x150. Skeletal animations bind at model+0x234 (0x1f8228).

### 3.3 Binding by path

0x18c62c **[code]** takes each member's **path string (member+0x04)** and looks it up in the
model's animation-group DICT with the name lookup 0x20b8ac. It records the member ↔
group-element mapping. A member whose path is missing is left unbound. The object name and
member name strings (+0x08, +0x0C) are not read on this path, and neither is the CANM's
group name (+0x0C). Which group a CANM binds to is chosen by HOME's code, not by that string.

BannerBG's CMDL has these groups **[file]**:
- `SkeletalAnimation` at 0x318: `root`, `BG`.
- `VisibilityAnimation` at 0x3dc: `IsVisible`, `Meshes[0].IsVisible`.
- `MaterialAnimation` at 0x49c, with 27 elements in its DICT at 0x4d0.

The 27 `MaterialAnimation` elements are all under `Materials["mt_BG"]`:
- `MaterialColor.{Emission, Ambient, Diffuse, Specular0, Specular1, Constant0…Constant5}`
- `TextureMappers[0…2].Sampler.BorderColor`
- `TextureMappers[0…2].Texture`
- `FragmentOperation.BlendOperation.BlendColor`
- `TextureCoordinators[0…2].{Scale, Rotate, Translate}`

These are the only material paths a BannerBG animation can drive.

### 3.4 Frame controller and loop mode

Evaluator fields set by 0x18c3c0 **[code]**:

| Field | Value at set-up |
|---|---|
| +0x34 frame | 0 |
| +0x38 previous frame | 0 |
| +0x3c step | 1.0 |
| +0x40 start | 0 |
| +0x44 end | CANM frame count |
| +0x48 play policy | Chosen from the CANM loop-mode byte |
| +0x54 dirty | 1 |

The loop mode picks the policy:
- 0 → **one time** 0x1ab720: `clamp(frame, start, end)`.
- 1 → **loop** 0x1ab6a4: with `len = end − start` (if `len == 0`, the result is `start`) and
  `o = frame − start`:
  - if `o < 0`, add `(floor(−o/len) + 1)·len` to `o`;
  - the result is `start + fmodf(o, len)`.

UpdateFrame (0x18c5d8) does nothing when the step is 0. Otherwise it sets
`frame = policy(start, end, frame + step)`.

Members are evaluated at the controller frame exactly as stored, an f32 with no rounding
(0x2e10e4 passes +0x34). With step 1 the Loop's frame runs 0, 1, …, 599, 0, …. Frame 600 is
never evaluated. One-time animations stop at their frame count and keep that value.

### 3.5 Evaluating a float curve

This describes 0x208764 and the functions it calls. All arithmetic is f32.

```
curve(c, f):
    f = repeat(c, f)                                         # 0x20893c
    if c.flags & 1: return curve(rel c+0x10, f) + curve(rel c+0x14, f)
    if c.flags & 2: return f32 at c+0x10
    if c.flags & 4: s = rel c+0x14
    else:           s = first of the count segments with s.end > f, else the last one   # 0x208800
    return segment(s, f)

repeat(c, f):                       # pre if f < start, post if f >= end, else f unchanged
    None:                 pre → start;  post → min(f, end)
    Repeat, RelativeRepeat (same function in both tables):
                          f − floor((f − start)/len)·len, len = end − start
    MirroredRepeat:       n = floor((f − start)/len); g = f − n·len
                          if n is odd: g = (start + end) − g
                          result g

segment(s, f):                                               # 0x208858
    f = max(f, s.start)
    if s.flags & 1: return f32 at s+0x0C                     # constant segment
    t = f − s.start                                          # keys are relative
    k = key search(t); n = the key after k
    if k.frame == t or interpolation == step: return k.value
    linear:  w = (t − k.frame)/(n.frame − k.frame);  k.value·(1 − w) + n.value·w
    hermite: herp(k.value, k.out_slope, n.value, n.in_slope, t − k.frame, n.frame − k.frame)

herp(v0, s0, v1, s1, x, d):                                  # 0x1cbab4
    w = x/d
    return v0 + (v0 − v1)·(2w − 3)·w² + x·(w − 1)·((w − 1)·s0 + w·s1)
```

**Key search** (same in every evaluator, 0x2fea8c–0x300220):
- If t ≤ key₀.frame, use key₀.
- If t ≥ key_last.frame, use key_last.
- Otherwise use the **last key with frame ≤ t**. The search starts at index
  `(u32)(inv·(count − 1)·t)` and walks from there, so inv changes speed but not the result.

So:
- **Step** gives the value of the last key at or before t.
- At a key's exact frame, every mode returns that key's value.
- With duplicate frames, such as BannerGift's keys at 50 and 50, the later key wins.

Formats with integer frames (1, 2, 5, 7) compare the frame truncated to u32 during the search.
UnifiedHermite48 compares `(s32)(t·32)`. All of them use the float t for the exact-frame test
and for the weights.

Edge cases. No HOME file reaches them, but Kabegami's writer must avoid them:
- If key₀.frame > t, linear and hermite extrapolate from the first pair.
- If t is greater than the last key's frame, linear and hermite read one key past the array.

The Hermite formula is the standard cubic Hermite with slopes per frame. It is the same as
SPICA's `Interpolation.Herp`. A Hermite segment whose two slopes are 0, as in all of BannerBG's
Hermite128 segments, is the ease curve v0 + (v1 − v0)(3w² − 2w³).

### 3.6 Member evaluation and other curve kinds

0x2e7174 evaluates each animated component and writes it into the bound target. It returns
a mask of the components it wrote. A component that is not animated is not written, so the
material keeps its own value or the value an earlier animation wrote. For a Transform, a
component that is not animated is copied from a base transform the caller passes in (0x2e74ac).
That base is presumably the bone's own; this is unverified.

Two curve kinds exist only in the code here; no HOME 3D file contains them **[code]**:
- **Integer curves** (0x1ab74c):
  - Flags at +0x0C: bit 0 means a constant s32 at +0x10.
  - Otherwise bits 3–5 pick a key format. At +0x10 are a u16 count and an f32 inv; keys start at +0x18.
  - The formats are f32 frame + s32 value (0x2feb94), u16 frame + s16 value (0x2feb10) and
    u8 frame + s8 value (0x2fea8c). All three use step lookup.
- **Boolean curves** (0x1ab78c):
  - Flags bit 0 means a constant whose value is flags bit 1.
  - Bit 2 means a bit array at +0x10, read least-significant bit first and indexed by
    `(u32)(frame − start)`.
  - Otherwise the integer formats are used.

### 3.7 Authoring constraints for a rewritten BannerBG_Loop

These follow from §3.3–§3.5:
- Keep each member's path identical. It must exist in the CMDL's `MaterialAnimation` group.
- Keep the first key of each keyed segment at relative frame 0 and the last at end − start.
- Write inv = 1/(end − start).
- Keep key counts below 65536.
- Make interpolation 0, 1 or 2. Hermite needs format 0–5.
- Use flags bit 2 with a count of 1 for a single segment.
- Use loop mode 1 for a looping drift. The frame count is the loop period in controller frames.

## 4. BannerBG_LZ.bin animations, decoded

All values are **[file]**. Key lists use these forms:
- step and linear keys: (frame, value)
- UnifiedHermite96 keys: (frame, value, slope)
- Hermite128 keys: (frame, value, in, out)

Key frames are relative to their segment's start. "Curve a..b" is the curve's start and end.
Every curve here has pre and post repeat 0 (None).

| Animation | Offset | Slot | Group | Loop mode | Frames | Members |
|---|---|---|---|---|---|---|
| BannerBG_SceneIn | 0x176c | 9 | SkeletalAnimation | 0 | 20 | 1 |
| BannerBG_SceneOut | 0x19b0 | 9 | SkeletalAnimation | 0 | 40 | 1 |
| BannerBG_AppPause | 0x1c6c | 10 | MaterialAnimation | 0 | 20 | 7 |
| BannerBG_AppQuit | 0x21dc | 10 | MaterialAnimation | 0 | 20 | 6 |
| BannerBG_AppRestart | 0x2468 | 10 | MaterialAnimation | 0 | 40 | 6 |
| BannerBG_Loop | 0x298c | 10 | MaterialAnimation | 1 | 600 | 3 |

### 4.1 BannerBG_SceneIn

Member `BG` (Transform) at 0x17c0, flags 0xf80000. Every curve is one UnifiedHermite96
segment (flags 0x68) over 0..20.

| Component | Curve | Keys (frame, value, slope) |
|---|---|---|
| ScaleX | 0x17fc | (0, 0.777129, 0.0116729), (12, 0.942945, 0.0124529), (19, 0.998914, 0.00211728), (20, 1, 0.00108647) |
| ScaleY | 0x1858 | same as ScaleX |
| ScaleZ | 0x18b4 | (0, 0.001, 0.0523226), (2, 0.109891, 0.0583166), (19, 0.99513, 0.00949052), (20, 1, 0.00487012) |
| TranslateY | 0x1910 | (0, 0.223, −0.0116796), (20, 0, −0.00108712) |
| TranslateZ | 0x1954 | (0, −34.786, −0.52375), (2, −35.876, −0.58375), (19, −44.7373, −0.0949993), (20, −44.786, −0.048748) |

RotateX/Y/Z/W and TranslateX are not animated.

### 4.2 BannerBG_SceneOut

Member `BG` at 0x1a04, flags 0xf80000. UnifiedHermite96 segments over 0..40. This is
SceneIn in reverse, held for the first 19 frames.

| Component | Curve | Keys (frame, value, slope) |
|---|---|---|
| ScaleX, ScaleY | 0x1a40, 0x1ab4 | (0, 1, 0), (19, 1, 0), (20, 1, −0.00583643), (21, 0.988327, −0.0121465), (39, 0.778216, −0.00211728), (40, 0.777129, −0.00108647) |
| ScaleZ | 0x1b28 | (0, 1, 0), (19, 1, 0), (20, 1, −0.0261613), (21, 0.947677, −0.0544455), (39, 0.00587013, −0.0094905), (40, 0.001, −0.00487013) |
| TranslateY | 0x1b9c | (0, 0, 0), (19, 0, 0), (30, 0.139375, 0.0139096), (40, 0.223, 0.00108713) |
| TranslateZ | 0x1bf8 | (0, −44.786, 0), (19, −44.786, 0), (20, −44.786, 0.261875), (21, −44.2622, 0.545), (39, −34.8348, 0.0950012), (40, −34.786, 0.0487518) |

### 4.3 BannerBG_AppPause (all paths are `Materials["mt_BG"].…`)

| Member (offset, flags) | Components |
|---|---|
| `MaterialColor.Constant0` (0x1d20, 0x80) | R, G, B: Hermite128 0..20 with slopes 0. R (0, 1)→(20, 0.4); G (0, 1)→(20, 0.45); B (0, 1)→(20, 0.5). A not animated |
| `TextureCoordinators[0].Scale` (0x1e28, 0x0) | X = Y: linear StepLinear64 0..19: (0, 1), (1, 1.002), (2, 1), (5, 0.98), (10, 0.93), (15, 0.89), (19, 0.87) |
| `MaterialColor.Constant4` (0x1f0c, 0x78) | A constant 0 |
| `MaterialColor.Constant1` (0x1f30, 0x80) | Hermite128 0..20 with slopes 0. R (0, 0)→(20, 0); G (0, 0)→(20, 0.1); B (0, 0)→(20, 0.2) |
| `TextureCoordinators[1].Scale` (0x2038, 0x0) | X = Y: linear 0..20: (0, 1), (1, 1.002), (2, 1), (5, 0.98), (10, 0.93), (15, 0.89), (20, 0.87) |
| `TextureMappers[1].Texture` (0x211c, 0x0) | Step StepLinear64, curve and segment −1..19: (0, 1), (20, 0). Texture list [BG_64_00, BG_CapMask_00]. The runtime picks index 1 (BG_CapMask_00) for frames 0 up to 19 and index 0 (BG_64_00) from frame 19 |
| `TextureCoordinators[1].Translate` (0x21c0, 0x3) | X constant 0, Y constant 0 |

### 4.4 BannerBG_AppQuit

| Member (offset, flags) | Components |
|---|---|
| `MaterialColor.Constant4` (0x2280, 0x70) | A: Hermite128 0..20 with slopes 0: (0, 0)→(20, 1) |
| `TextureCoordinators[0].Scale` (0x22f0, 0x0) | X (0x230c) = Y (0x235c): **two segments** (curve flags 0). Step StepLinear64 0..20: (0, 0.87), (20, 1). Then a constant segment 20..20 with value 1. Result: 0.87 until frame 20, then 1 |
| `TextureCoordinators[0].Translate` (0x23ac, 0x3) | (0, 0) constant |
| `TextureCoordinators[1].Translate` (0x23c8, 0x3) | (−0.6911, 0.1666) constant. This is the Loop's value at frame 600 |
| `TextureMappers[1].Texture` (0x23e4, 0x1) | Constant 0 → BG_64_00 (list [BG_64_00, BG_CapMask_00]) |
| `TextureCoordinators[1].Scale` (0x244c, 0x3) | (3, 6) constant |

### 4.5 BannerBG_AppRestart

Every segment here starts at frame 20 except the texture curve's. Key frames are relative.

| Member (offset, flags) | Components |
|---|---|
| `MaterialColor.Constant0` (0x250c, 0x80) | Hermite128 20..40 with slopes 0. R (0, 0.4)→(20, 1); G (0, 0.45)→(20, 1); B (0, 0.5)→(20, 1) |
| `TextureCoordinators[0].Scale` (0x2614, 0x0) | X = Y: linear 20..38: (0, 0.87), (5, 0.89), (10, 0.93), (15, 0.98), (18, 1) |
| `MaterialColor.Constant4` (0x26d8, 0x78) | A constant 0 |
| `MaterialColor.Constant1` (0x26fc, 0x80) | Hermite128 20..40 with slopes 0. R (0, 0)→(20, 0); G (0, 0.1)→(20, 0); B (0, 0.2)→(20, 0) |
| `TextureMappers[1].Texture` (0x2804, 0x0) | Step 0..19: (0, 0), (19, 1). BG_64_00 until frame 19, then BG_CapMask_00 |
| `TextureCoordinators[1].Scale` (0x28a8, 0x0) | Linear 20..40. X: (0, 0.87), (5, 0.89), (10, 0.93), (15, 0.98), (18, 0.99), (19, 0.99), (20, 1). Y: same but (18, 0.995), (19, 0.997) |

### 4.6 BannerBG_Loop: the wallpaper drift

Member DICT at 0x29b4:

| Node | Reference bit | Left | Right | Name | Data |
|---|---|---|---|---|---|
| 0x29d0 | 0x14d | 0 | 1 | `…MaterialColor.Constant4` | 0x2a00 |
| 0x29e0 | 0x196 | 3 | 2 | `…TextureCoordinators[1].Translate` | 0x2a24 |
| 0x29f0 | 0x15e | 1 | 3 | `…TextureMappers[1].Texture` | 0x2ab8 |

| # | Path | Type | Flags | Content |
|---|---|---|---|---|
| 1 | `Materials["mt_BG"].MaterialColor.Constant4` (0x2a00) | RGBA | 0x78 | R, G, B not animated (slots at 0x2a14–0x2a1c = 0). **A = 1.0 constant** (0x2a20) |
| 2 | `Materials["mt_BG"].TextureCoordinators[1].Translate` (0x2a24) | Vector2D | 0x0 | X → curve 0x2a40, Y → curve 0x2a7c |
| 3 | `Materials["mt_BG"].TextureMappers[1].Texture` (0x2ab8) | Texture | 0x1 | Value 0.0 constant (0x2acc). List count 1 (0x2ad0) → [BG_64_00] (reference at 0x2adc) |

The two curves:

| Curve | Range | Flags | Segment | Keys |
|---|---|---|---|---|
| X (0x2a40) | 0..600, pre/post 0 | 0x4 | 0x2a58: 0..600, flags 0xc4 (**linear**, StepLinear64), 2 keys, inv 1/600 | (0, **−0.357778**) at 0x2a6c, (600, **−0.6911**) at 0x2a74 |
| Y (0x2a7c) | 0..600, pre/post 0 | 0x4 | 0x2a94: 0..600, flags 0xc4, 2 keys, inv 1/600 | (0, **0**) at 0x2aa8, (600, **0.1666**) at 0x2ab0 |

What it does:
- It holds Constant4's alpha at 1.0.
- It keeps texture mapper 1 on `BG_64_00`, the cell texture.
- It moves texture coordinator 1's translation in a straight line, at a constant rate of
  (−5.555366e−4, +2.776667e−4) per frame. The totals are
  ΔX = −0.3333220 and ΔY = +0.1666000 per 600 frames.
- Loop mode 1 wraps the controller frame at 600 (§3.4). The runtime model in `evalcurve.py`
  gives:

| Frame | Translate |
|---|---|
| 0 | (−0.3577780, 0) |
| 150 | (−0.4411085, 0.0416500) |
| 300 | (−0.5244390, 0.0833000) |
| 450 | (−0.6077695, 0.1249500) |
| 599 | (−0.6905445, 0.1663223) |
| next | back to frame 0 |

The Loop does not animate the scale. The material's own texture coordinator 1 is at 0xe2c
(field order per SPICA's `GfxTextureCoord`; see the material spec). It has:
- transform type 2 (3ds Max)
- scale (3, 6) at 0xe3c and 0xe40
- rotate 0
- translate (0, 0) at 0xe48

AppQuit also writes scale (3, 6). SPICA's 3ds Max texture matrix (`TextureTransform`, not
checked against the runtime here) gives u' = 3u − 3·TX + c and v' = 6v + 6·TY + c′. So one
600-frame cycle shifts the pattern by +0.999966 tiles in u and +0.999600 in v. That is about
one tile diagonally, with a jump at the wrap of at most 4·10⁻⁴ tile.

## 5. BannerBGmask_LZ.bin compared

The Loop at 0x29a4 is **byte-identical** to BannerBG's at 0x298c over its 0x170 bytes, apart
from 16 rel string pointers. Each of those points to an identical string 4 bytes further on,
because `BannerBGmask` is a longer name **[file]**. SceneIn and SceneOut are identical too.
The differences are all in the App* animations:

| Animation, member | BannerBG | BannerBGmask |
|---|---|---|
| AppPause `TextureCoordinators[0].Scale` X/Y | Linear 0..19, last key (19, 0.87) | 0..20, last key (20, 0.87) |
| AppPause `TextureMappers[1].Texture` | −1..19, keys (0, 1), (20, 0): switches at frame 19 | −1..20, keys (0, 1), (21, 0): switches at frame 20 |
| AppQuit `TextureCoordinators[0].Scale` X/Y | Step (0, 0.87), (20, 1) plus a constant segment at 20 | One Hermite128 segment, slopes 0: (0, 0.87)→(20, 1), a smooth ease |
| AppRestart `TextureCoordinators[0].Scale` X/Y | Linear 20..38: … (15, 0.98), (18, 1) | 20..40: … (15, 0.98), (18, 1), (19, 1.002), (20, 1) |
| AppRestart `TextureMappers[1].Texture` | 0..19: (0, 0), (19, 1) | 0..20: (0, 0), (20, 1) |
| AppRestart `TextureCoordinators[1].Scale` X/Y | (18, 0.99), (19, 0.99) for X; (18, 0.995), (19, 0.997) for Y | (18, 1), (19, 1.002) for both |

In BannerBGmask, AppRestart starts at 0x2460 and the Loop at 0x29a4.

## 6. SPICA's reader checked against the bytes

These are the sources in `SPICA/Formats/CtrGfx/Animation`, plus `GfxDict`/`Gfx` and the H3D
evaluator they convert to. `evalcurve.py` puts SPICA's reading and evaluation next to the
runtime model at every integer frame of every BannerBG curve.

**What SPICA gets right:**
- The CANM header (name, group, loop mode, frame count, member DICT).
- The member layout for revision ≤ 5 (two extra string fields).
- The flag bits for Vector2D, Vector3D, RGBA and Transform.
- The curve header.
- Bit 1 as a constant curve.
- Reading a single-segment curve.
- The segment header (start, end, flags, count, inv).
- Format number = flags >> 5.
- Linear versus step for the StepLinear formats from bit 2.
- The Hermite128, UnifiedHermite96 and StepLinear64 keys.
- The packed key bit layouts in `KeyFrameQuantizationHelper`. UnifiedHermite32 was confirmed
  in the file and the code; Hermite64, Hermite48, UnifiedHermite48 and StepLinear32 match
  the code.
- The Hermite formula.
- Using the out-slope of key k and the in-slope of key k+1.

**What SPICA gets wrong or leaves out:**

| SPICA | Bytes and runtime | Effect on HOME files |
|---|---|---|
| `Gfx` reads 16 DATA slots: fog animations as the 15th, emitters as the 16th | 15 slots. The 16th position is the first DICT's magic | Its bogus 16th dictionary happens to read 0xFFFFFFFF+1 = 0 entries in BannerBG; nothing breaks |
| `GfxAnimationElement.Dummy` is read as a u32 | It is a rel pointer to the member-name string | None |
| No reader for Integer (1) and Texture (7). The content becomes a bare `object`, and `ToH3DAnimation` drops it | Texture members bind a texture from the member's list (§2.4) | **Every `TextureMappers[1].Texture` member of BannerBG is lost**, including the Loop's |
| `GfxAnimFloat` hands `_Value` to `SetVector`, which assigns its parameter | — | Float members always end up empty. None exist in HOME files |
| Curve flag bit 2 is called `IsQuantizedCurve`; bit 0 is ignored | Bit 2 = single segment; bit 0 = sum of two curves | None for HOME files. Composite curves would be misread |
| Reads the segment count, then only the first segment pointer | Inline array of *count* pointers. The runtime picks the first segment whose end > frame, else the last | Later segments are dropped. For BannerBG's AppQuit scale this is harmless: the dropped constant segment equals the last key |
| No constant-segment case (segment flags bit 0) | 16-byte segment, value at +0x0C | See the note below the table |
| Key frames used as absolute frames | Relative to the segment start (§3.5) | Wrong whenever a segment starts away from 0. BannerBG: AppPause's texture curve (start −1; index 1 instead of 0 at frame 19), and 9 of AppRestart's 11 curves (start 20; up to 0.6 off, for example Constant0.R is 0.4 at frame 20 in the runtime but 1 in SPICA). The Loop is unaffected |
| Quantized segments: reads value scale, value offset **and frame scale**, and multiplies frames by it | Revision 5 has only scale and offset; frames are integers (u8, 12-bit) or u16/32 | Every quantized segment is misread. BannerGift 0x6268: the first key word becomes the frame scale and the keys are read 4 bytes late, into the next segment. BannerBG has none |
| Interpolation of non-StepLinear formats is always Hermite | Bits 2–4 apply to every format | None for HOME files: every hermite-format segment is flagged hermite |
| Key count read as a u32 | The runtime reads a u16 | None. The high halves are 0 |
| The H3D evaluator ignores pre/post repeat and clamps to the first or last key | §3.5 repeat methods | None in BannerBG (all None). Other files differ, for example BannerPrize's repeat curves at frame 600 |
| The viewer (`AnimationControl`) wraps every animation | Loop mode 0 clamps at the frame count | One-time animations loop in SPICA's viewer |
| The writer stores InvDuration = 1/EndFrame | 1/(end − start) | A SPICA-written segment with start ≠ 0 gets a wrong search start index; with start < 0 the index can pass the last key |

**The constant segment and the reported Hermite misread.** No HOME curve starts with a
constant segment, so SPICA's own code never reads one. Any reader that applies SPICA's
segment code to *every* segment pointer does reach them, and misreads them.
- Flags 0x1 give format 0, which SPICA treats as Hermite128.
- The f32 value is then read as the key count, and Hermite keys are read after it.
- For example, BannerBG's segment at 0x234c holds 1.0 at 0x2358, which reads as 0x3F800000 keys.
- The 12 constant segments in HOME's files are all second segments. They are in BannerBG,
  the five BannerApplet files, BannerDS, BannerDSiWare, BannerFolder and BannerPrize.

Result of the comparison for BannerBG:
- SPICA's evaluation equals the runtime model at every integer frame for SceneIn, SceneOut,
  AppQuit, the Loop, and AppPause apart from its texture curve.
- It differs for AppPause's texture curve and for 9 AppRestart curves.
- BannerBGmask gives the same pattern.

## 7. Unknowns

- **Frame rate of the drift.** The step is +1.0 per UpdateFrame (§3.2, §3.4), but I did not
  trace how often HOME calls UpdateFrame for the BG model, so 10 s versus 20 s per loop is
  open. To measure it, break on 0x18c5d8 for the Loop's evaluator in Zakuro, or log
  evaluator+0x34, and count calls per VBlank. When the first evaluation happens (frame 0
  or 1) is also unchecked.
- **Which animations run together.** For example, whether the App* animations and the Loop
  share the material binding at the same time, and how HOME switches them. Only set-up and
  Play were traced.
- **Curve flag bit 0** (composite sum) and **bit 1** (constant curve): known from the code
  only (bit 1 also from SPICA). No HOME file uses them, so their authoring rules are unknown.
- **CANM +0x20 and +0x24** (probably a metadata count and DICT): 0 everywhere.
- **Member +0x08 and +0x0C, and the CANM group name +0x0C:** not used for binding. Any other
  use is unknown.
- **Segment +0x0E, segment flags bit 1, interpolation values 3–7.**
- **Padding after 6-byte keys** (Hermite48, UnifiedHermite48): no file has them. SPICA pads
  to 4 bytes [SPICA].
- **QuatTransform and MtxTransform** (types 8 and 9): known from SPICA only; not handled by
  the member switch at 0x2e7174.
- **RelativeRepeat.** It uses the same frame mapping as Repeat (the same function in both
  tables), and the float curve path applies no value offset. Whether code elsewhere adds one
  is unknown.
- **Texture-matrix maths** behind §4.6's "one tile per cycle": taken from SPICA, not from the
  runtime. It belongs to the material spec.
- **FPSCR mode** (flush-to-zero, default NaN) of HOME's thread: not checked. It does not matter
  for these values.

Exploration scripts are in
`/tmp/claude-1000/-home-fearkov-Desktop-zakuro/9246e26f-7645-4fb9-b281-cd09c25c43f8/scratchpad/kabegami-explore/cgfx-animation/`:

| Script | What it does |
|---|---|
| `cgfxlib.py` | LZ11 and BLZ decoders |
| `canm.py` | Full decoder with offsets |
| `compact.py` | Tables |
| `evalcurve.py` | Runtime model versus SPICA |
| `disasm_home.py`, `scan_code.py` | In-memory disassembly |
| `animgroups.py`, `texcoords.py` | CMDL lookups |
| `raw_words.py` | Word view |

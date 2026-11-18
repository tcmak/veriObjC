# 📼 VeriObjC Ternary Tape Format — Specification v1.0 (Frozen Contract — Task 5.6a)

**Status: FROZEN v1.0 (Task 5.6a).** This document freezes the two-profile **VTPE v1.0** wire contract and §8 rejection diagnostics consumed across VeriObjC (`rt/`), `TernaryTape.jl`, `setun`, and the kit's M4.5 INT8 tape oracle. Full reconciliation with the Phase 1 single-section `VOBJ` snapshot helper is tracked under Task 5.6b.

## 🧭 Background & Context (self-contained — read this first in a fresh conversation)

**VeriObjC** is a hardened, verifiable dialect of Objective-C for deterministic low-latency systems (bare metal, FPGAs, HFT). It compiles via a Clang 23 AST plugin, links a freestanding C micro-runtime (`rt/`: compiler-assigned grow-only data stacks, `vpool` freelists, zero heap), and enforces a closed world: dynamic dispatch, `id`, reflection, and exceptions are banned at the AST level with named diagnostics.

**veriStream** is its fixed-point streaming substrate (`Bits<W>`, `Stream<T,N>`): ternary-quantized inference (weights ∈ {-1, 0, +1}) lowered to CMUX ladders and popcount adder trees, so kernels elaborate with **zero DSP blocks** — enforced as a build gate. Weights pack as 2-bit codes, 4 per byte, pinned in BRAM. A pre-banked KV arena with a declared `ContextCap` makes context overflow a compile error.

**Weight provenance is training-source-agnostic by design.** Any hosted `@cold` producer — a Julia/Lux exporter (the **TernaryTape.jl** project), a PyTorch/BitNet-style pipeline, or a plain C++ tool — exports a content-addressed tape artifact. The VTPE v1 loader verifies framing, flags, profile rules, tags, lengths, checksums, and every link **before the image becomes hardware-visible**. This spec is the byte-exact contract between producers and the VeriObjC loader. VeriObjC never depends on a producer's toolchain; only these bytes cross the boundary.

## 📦 1. Scope, Ownership & Conformance Classes

- Owned by the **VeriObjC repo**, versioned independently of the toolchain.
- **Class D — Decoder (VeriObjC `rt/`)**: must accept all valid v1 tapes for its target profile (`CORE` or `EXTENDED`) and reject all invalid ones with the exact named diagnostic per §8.
- **Class P — Producer** (any language: Julia, Python, C++): must emit byte-identical output for identical inputs (determinism — required for content addressing) and must pass the golden vector corpus in `fixtures/vtpe_v1/`.
- **Class R — Reference Interpreter**: golden model reproducing veriStream semantics exactly (CMUX/popcount, shift-only norms), used for C/RTL cosimulation equivalence.

## 🧱 2. Framing & Two-Profile Rules (v1.0)

Little-endian throughout. No pointers anywhere — position independence is the point.

**Header (exactly 32 bytes):**

| Offset | Size | Field | Rule |
| :--- | :--- | :--- | :--- |
| `0x00` | 4 | `magic` | `"VTPE"` (`0x56 0x54 0x50 0x45`, little-endian `u32` `0x45505456`); mismatch → `VTPE_ERR_BAD_MAGIC` |
| `0x04` | 1 | `version_major` | `1`; unknown major → `VTPE_ERR_UNSUPPORTED_VERSION` |
| `0x05` | 1 | `version_minor` | `0` in v1.0; v1.x minor bumps may only append optional metadata inside `manifest` (`0x0004`). New section tags or flag bits require a major bump. |
| `0x06` | 1 | `flags` | Bit 0 = `links_present` (`0x01`), Bit 1 = `manifest_present` (`0x02`), Bit 2 = `profile` (`0 = Core`, `1 = Extended` / `0x04`). Bits 3–7 (`0xF8`) must be `0` → else `VTPE_ERR_UNKNOWN_FLAGS` (kit ADR-0002). If a Core-only reader (`VTPE_PROFILE_CORE`) receives `bit 2 == 1` → `VTPE_ERR_UNSUPPORTED_PROFILE`. |
| `0x07` | 1 | `section_count` | Must be `>= 3` and equal actual framed sections → else `VTPE_ERR_SECTION_COUNT_MISMATCH` |
| `0x08` | 4 | `total_length` | Framed section bytes after the 32-byte header; `32 + total_length` must equal total buffer length → else `VTPE_ERR_LENGTH_MISMATCH` |
| `0x0C` | 4 | `header_fnv1a` | 32-bit FNV-1a (`offset_basis = 0x811c9dc5`, `prime = 0x01000193`) over **all 28 non-checksum header bytes** (`0x00..0x0B` followed by `0x10..0x1F`) → mismatch rejects with `VTPE_ERR_CHECKSUM_MISMATCH` |
| `0x10` | 8 | `artifact_id` | 64-bit FNV-1a (`offset_basis = 0xcbf29ce484222325`, `prime = 0x100000001b3`) over all framed section bytes (`0x20 .. 0x20 + total_length - 1`) → mismatch rejects with `VTPE_ERR_ARTIFACT_ID_MISMATCH` |
| `0x18` | 8 | `reserved` | Must be all zero (`0x00 * 8`) → else `VTPE_ERR_NONZERO_RESERVED` |

**Sections (repeat `section_count` times, 12-byte section header + payload):**
`u16 tag` · `u16 reserved (0)` · `u32 length` · `u32 payload_fnv1a` (32-bit FNV-1a over `length` payload bytes) · `uint8_t payload[length]`.

**Section tags (v1.0):**

| Tag | Name | Profile & Presence Rules |
| :--- | :--- | :--- |
| `0x0001` | `weights` | **Mandatory in Core & Extended.** Ternary code stream (§3). |
| `0x0002` | `scales` | **Mandatory in Core & Extended.** Per-tensor power-of-two exponents (§4). |
| `0x0003` | `arch_descriptor` | **Mandatory in Core & Extended.** Layer list, dims, latency contracts (§6). |
| `0x0004` | `manifest` | **Conditional on `flags` bit 1 (`0x02`).** Must be present iff bit 1 == `1` (§5). |
| `0x0005` | `link_table` | **Conditional on `flags` bit 0 (`0x01`).** Must be present iff bit 0 == `1` (§7). |
| `0x0006` | `fp32_aux` | **Mandatory in Extended (`flags` bit 2 == `1`); Forbidden in Core (`flags` bit 2 == `0`).** Host-only FP32 auxiliary tensors (§4.1). Presence in a Core tape rejects with `VTPE_ERR_PROFILE_SECTION_FORBIDDEN`. |

- Unknown tag (`< 0x0001` or `> 0x0006`) → `VTPE_ERR_UNKNOWN_TAG`.
- Duplicate tag → `VTPE_ERR_DUPLICATE_TAG`.
- Nonzero section `reserved` field → `VTPE_ERR_NONZERO_RESERVED`.
- Sum of `(12 + length)` across all `section_count` sections must equal `total_length` → else `VTPE_ERR_LENGTH_MISMATCH`.
- Missing any mandatory section for the declared profile, or mismatch between `flags` bits 0/1 and presence of `0x0005`/`0x0004` → `VTPE_ERR_MISSING_REQUIRED_SECTION`.

## 🔢 3. Ternary Weight Packing (`0x0001`)

- 2-bit codes, 4 per byte. **Lane order little-endian**: lane 0 occupies bits `[1:0]`, lane 1 bits `[3:2]`, lane 2 bits `[5:4]`, lane 3 bits `[7:6]`.
- **Code map:** `0b00` → `0`, `0b01` → `+1`, `0b10` → `−1`, `0b11` → **RESERVED — reject with `VTPE_ERR_RESERVED_TERNARY_CODE`**.
- Each `ternary_dense` layer row (`dim_in` elements) pads to a byte boundary (`row_bytes = (dim_in + 3) / 4`), and a layer with `dim_out` rows occupies `dim_out * row_bytes` bytes. Any trailing padding lanes (`dim_in % 4 != 0`) must be `0b00` → else reject with `VTPE_ERR_NONZERO_PADDING`.
- Total `weights` payload length must equal the sum of `dim_out * ((dim_in + 3) / 4)` over all `ternary_dense` layers in `arch_descriptor` (§6) → else `VTPE_ERR_DIM_PAYLOAD_MISMATCH`.

## ⚖️ 4. Scales (`0x0002`, Power-of-Two Only) & Host `fp32_aux` (`0x0006`)

- **Scales (`0x0002`):** One little-endian `s16` exponent per `ternary_dense` layer (`[-30, +30]`). Total `scales` payload length must equal `2 * num_ternary_dense_layers` (at least 1 `ternary_dense` layer required) → else `VTPE_ERR_DIM_PAYLOAD_MISMATCH`; out-of-range exponent (`< -30` or `> 30`) rejects with `VTPE_ERR_INVALID_SCALE`.
- **Rationale:** Keeps hardware normalization **shifts-only**, preserving the zero-multiplier (`DSP = 0`) synthesis gate.

### 4.1 Host-Consumed `fp32_aux` Section (`0x0006`, Extended Profile Only)

In the Extended profile (`flags` bit 2 == `1`), embeddings, floating-point RMSNorm weights, and softmax head scales are carried in `0x0006` (`fp32_aux`). They are integrity-checked by the section's `payload_fnv1a` and `artifact_id`, but **remain strictly host-consumed and are never mapped into a hardware-visible DMA/BRAM region**.

**Payload layout (little-endian, 4-byte aligned):**
- Header (4 bytes): `u16 tensor_count` (`> 0`) · `u16 reserved` (`must be 0`).
- Followed by `tensor_count` records, each:
  - `u16 layer_index`: must be `< n_layers` in `arch_descriptor` (§6).
  - `u8 aux_role`:
    - `1 = embedding_table` (target layer must have `layer_kind == 6` `host_embedding`, `elem_count == (uint32_t)dim_in * (uint32_t)dim_out`)
    - `2 = rmsnorm_weight` (target layer must have `layer_kind == 3` `rmsnorm_shift`, `elem_count == dim_in`)
    - `3 = softmax_scale` (target layer must have `layer_kind == 7` `host_softmax_head`, `elem_count == dim_out`)
  - `u8 reserved`: must be `0`.
  - `u32 elem_count`: must be `> 0` and match the target layer's dimension rule above.
  - `float32 values[elem_count]`: `elem_count * 4` bytes of IEEE-754 little-endian `binary32` values. Any NaN or Inf bit pattern (`(bits & 0x7F800000u) == 0x7F800000u`) is rejected.
- Sum of record bytes (`4 + sum(8 + elem_count * 4)`) must equal the section's `length`. Any violation rejects with **`VTPE_ERR_MALFORMED_FP32_AUX`**.

## 🏷️ 5. Manifest (`0x0004`, Producer-Declared)

Five length-prefixed (`u16 byte_len` followed by `byte_len` non-NUL ASCII/raw bytes, each `byte_len > 0`) fields in fixed order:
1. `producer_id` (e.g. `"ternarytape.jl/0.1.0"`)
2. `training_run_id` (**must be exactly 32 bytes**)
3. `quant_algorithm` (e.g. `"absmean-b158-pot"`)
4. `quant_params` (algorithm-specific parameter blob)
5. `content_hash` (producer-declared digest, `>= 8` bytes)

The sum of `(2 + byte_len)` across all 5 fields must equal the `manifest` section's `length` → else reject with `VTPE_ERR_MALFORMED_MANIFEST`.

## 🏗️ 6. Arch Descriptor (`0x0003`)

- Header (4 bytes): `u16 n_layers` (`> 0`) · `u16 reserved` (`must be 0`).
- Followed by `n_layers` 16-byte layer descriptors (`length == 4 + 16 * n_layers`):
  - `u16 layer_kind`:
    - `1 = token_shift` (`dim_in == dim_out > 0`, `reduce_stages == 0`, `latency_cycles == 1`)
    - `2 = glu_mux` (`dim_in == dim_out > 0`, `reduce_stages == 0`, `latency_cycles == 0`)
    - `3 = rmsnorm_shift` (`dim_in == dim_out > 0`, `reduce_stages == 0`)
    - `4 = ternary_dense` (`dim_in > 0`, `dim_out > 0`, `reduce_stages == clog2(dim_in)`, `latency_cycles >= reduce_stages`)
    - `5 = kv_arena` (`dim_in > 0`, `context_cap > 0`)
    - `6 = host_embedding` (**Extended profile only**; `dim_in > 0`, `dim_out > 0`; presence in Core tape rejects with `VTPE_ERR_PROFILE_SECTION_FORBIDDEN`)
    - `7 = host_softmax_head` (**Extended profile only**; `dim_in > 0`, `dim_out > 0`; presence in Core tape rejects with `VTPE_ERR_PROFILE_SECTION_FORBIDDEN`)
  - `u16 dim_in`
  - `u16 dim_out`
  - `u8 reduce_stages`
  - `u8 reserved` (`must be 0`)
  - `u32 latency_cycles`
  - `u32 context_cap`

Any structural mismatch in `arch_descriptor` rejects with `VTPE_ERR_MALFORMED_ARCH`.

## 🪢 7. Self-Relative Links (`0x0005`, Optional Section)

When `flags` bit 0 (`0x01`) is set, `0x0005` (`link_table`) carries an array of `u32` signed `int32_t` self-relative offsets (`length > 0` and `length % 4 == 0`). For each entry `i` at byte offset `4 * i` within `link_table`:
- `delta != 0` (self-link sentinel `0` is rejected)
- `(delta % 4) == 0` (4-byte alignment required)
- `0 <= (int64_t)(4 * i) + delta < (int64_t)length` (in-section resolution required)
Any failure rejects the tape with `VTPE_ERR_INVALID_LINK`.

## 🚫 8. Rejection Contract & Named Diagnostics (Fuzz Ground Truth)

Every conforming Class D decoder validates a tape in the following precedence order and returns the exact named error symbol:

| Error Symbol | Trigger Clause |
| :--- | :--- |
| `VTPE_OK` | Valid v1.0 tape for the requested reader profile |
| `VTPE_ERR_NULL_OR_TRUNCATED` | Input pointer is `NULL` or `in_len < 32` |
| `VTPE_ERR_BAD_MAGIC` | Header bytes `0x00..0x03 != "VTPE"` (`0x45505456`) |
| `VTPE_ERR_UNSUPPORTED_VERSION` | `version_major != 1` |
| `VTPE_ERR_NONZERO_RESERVED` | Header `reserved` (`0x18..0x1F`), section `reserved`, or descriptor `reserved != 0` |
| `VTPE_ERR_CHECKSUM_MISMATCH` | Header `header_fnv1a` (`0x0C..0x0F`) or any section `payload_fnv1a` mismatch |
| `VTPE_ERR_UNKNOWN_FLAGS` | Header `flags` has any reserved bit 3–7 set (`(flags & 0xF8) != 0`) |
| `VTPE_ERR_UNSUPPORTED_PROFILE` | Core-only reader (`VTPE_PROFILE_CORE`) given an Extended tape (`(flags & 0x04) != 0`) |
| `VTPE_ERR_SECTION_COUNT_MISMATCH` | `section_count < 3` or section walk count `!= section_count` |
| `VTPE_ERR_LENGTH_MISMATCH` | `32 + total_length != in_len` or sum of framed sections `!= total_length` |
| `VTPE_ERR_ARTIFACT_ID_MISMATCH` | Header `artifact_id` (`0x10..0x17`) `!= fnv1a64(sections)` |
| `VTPE_ERR_UNKNOWN_TAG` | Section tag not in `0x0001..0x0006` |
| `VTPE_ERR_DUPLICATE_TAG` | Two sections share the same tag |
| `VTPE_ERR_PROFILE_SECTION_FORBIDDEN` | Tag `0x0006` (`fp32_aux`) or `layer_kind in {6, 7}` present in a Core tape (`(flags & 0x04) == 0`) |
| `VTPE_ERR_MISSING_REQUIRED_SECTION` | Missing `0x0001`/`0x0002`/`0x0003`, missing `0x0006` in Extended, or `0x0004`/`0x0005` disagrees with `flags` bits 1/0 |
| `VTPE_ERR_MALFORMED_ARCH` | Invalid `arch_descriptor` (`0x0003`) header, `layer_kind`, dims, or `reduce_stages != clog2(dim_in)` |
| `VTPE_ERR_DIM_PAYLOAD_MISMATCH` | `weights` (`0x0001`) or `scales` (`0x0002`) byte length disagrees with `arch_descriptor` |
| `VTPE_ERR_RESERVED_TERNARY_CODE` | Any active lane in `weights` (`0x0001`) contains code `0b11` |
| `VTPE_ERR_NONZERO_PADDING` | Any trailing row-padding lane in `weights` (`0x0001`) is non-zero |
| `VTPE_ERR_INVALID_SCALE` | Any `s16` exponent in `scales` (`0x0002`) is outside `[-30, +30]` |
| `VTPE_ERR_MALFORMED_MANIFEST` | `manifest` (`0x0004`) field framing or 32-byte `training_run_id` rule violated |
| `VTPE_ERR_INVALID_LINK` | `link_table` (`0x0005`) self-relative offset is `0`, unaligned, or out of bounds |
| `VTPE_ERR_MALFORMED_FP32_AUX` | `fp32_aux` (`0x0006`) framing, `layer_index`/`aux_role`/`elem_count` binding, or NaN/Inf check failed |

**Per-Byte Mutation Invariant:** Every byte index `0 .. len-1` of any valid reference tape must have at least one single-byte mutation that flips `VTPE_OK` → `VTPE_ERR_*`.

## 🧪 9. Golden Vectors & Determinism (`fixtures/vtpe_v1/`)

- `fixtures/vtpe_v1/accept/`: named `.bin` tapes plus paired `.json` expectation sidecars (`expected: "accept"`, `reader_profile`, `artifact_id_hex`, and decoded spot values).
- `fixtures/vtpe_v1/reject/`: named `.bin` tapes plus paired `.json` expectation sidecars (`expected: "reject"`, `reader_profile`, and exact `error_code` symbol from §8).
- Encoders must be **deterministic**: same model + params → byte-identical tape. Content addressing depends on it.

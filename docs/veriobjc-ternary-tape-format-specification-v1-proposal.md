# 📼 VeriObjC Ternary Tape Format — Specification v1 (Proposal)

**Status: PROPOSAL.** Derived from VeriObjC's documented runtime invariants (`veriobjc_to_bytes` / `veriobjc_from_bytes`, FNV-1a framing, self-relative links, ternary weight packing). The `rt/` implementation is **authoritative until reconciled** — every field here must be checked against `rt/` before this document is published as a contract.

## 🧭 Background & Context (self-contained — read this first in a fresh conversation)

**VeriObjC** is a hardened, verifiable dialect of Objective-C for deterministic low-latency systems (bare metal, FPGAs, HFT). It compiles via a Clang 23 AST plugin, links a freestanding C micro-runtime (`rt/`: compiler-assigned grow-only data stacks, `vpool` freelists, zero heap), and enforces a closed world: dynamic dispatch, `id`, reflection, and exceptions are banned at the AST level with named diagnostics.

**veriStream** is its fixed-point streaming substrate (`Bits<W>`, `Stream<T,N>`): ternary-quantized inference (weights ∈ {-1, 0, +1}) lowered to CMUX ladders and popcount adder trees, so kernels elaborate with **zero DSP blocks** — enforced as a build gate. Weights pack as 2-bit codes, 4 per byte, pinned in BRAM. A pre-banked KV arena with a declared `ContextCap` makes context overflow a compile error.

**Weight provenance is training-source-agnostic by design.** Any hosted `@cold` producer — a Julia/Lux exporter (the **TernaryTape.jl** project), a PyTorch/BitNet-style pipeline, or a plain C++ tool — exports a content-addressed tape artifact. `veriobjc_from_bytes` verifies framing, tags, lengths, and every link **before the image becomes hardware-visible**. This spec is the byte-exact contract between producers and the VeriObjC loader. VeriObjC never depends on a producer's toolchain; only these bytes cross the boundary.

## 📦 1. Scope, Ownership & Conformance Classes

- Owned by the **VeriObjC repo**, versioned independently of the toolchain.
- **Class D — Decoder (VeriObjC `rt/`)**: must accept all valid v1 tapes and reject all invalid ones per §8.
- **Class P — Producer** (any language: Julia, Python, C++): must emit byte-identical output for identical inputs (determinism — required for content addressing) and must pass the golden vector corpus.
- **Class R — Reference Interpreter**: golden model reproducing veriStream semantics exactly (CMUX/popcount, shift-only norms), used for C/RTL cosimulation equivalence.

## 🧱 2. Framing (v1 Proposal)

Little-endian throughout. No pointers anywhere — position independence is the point.

**Header (exactly 32 bytes):**

| Offset | Size | Field | Rule |
| :--- | :--- | :--- | :--- |
| 0x00 | 4 | magic | `"VTPE"` (0x56 0x54 0x50 0x45); mismatch → reject |
| 0x04 | 1 | version major | `1`; unknown major → reject |
| 0x05 | 1 | version minor | additive fields; decoder skips unknown minors |
| 0x06 | 1 | flags | bit 0 links_present, bit 1 manifest_present; reserved bits must be 0 → else reject |
| 0x07 | 1 | section_count | must equal actual sections → else reject |
| 0x08 | 4 | total_length | framed bytes after header; must match exactly → else reject |
| 0x0C | 4 | header FNV-1a (32-bit) | over header bytes 0x00–0x0B |
| 0x10 | 8 | artifact_id | content-address key, producer-assigned |
| 0x18 | 8 | reserved | must be zero → else reject |

**Sections (repeat `section_count` times):** `u16 tag` · `u16 reserved (0)` · `u32 length` · `u32 FNV-1a-32 over payload` · payload bytes.

**Section tags (v1):**

| Tag | Name | Notes |
| :--- | :--- | :--- |
| 0x0001 | weights | ternary code stream (§3) |
| 0x0002 | scales | per-tensor power-of-two exponents (§4) |
| 0x0003 | arch descriptor | layer list, dims, latency contracts (§6) |
| 0x0004 | manifest | provenance, producer-declared (§5) |
| 0x0005 | link table | optional (flags bit 0); self-relative links (§7) |

Unknown tag → reject. Duplicate tag → reject. Sum of section sizes must equal `total_length` → else reject.

## 🔢 3. Ternary Weight Packing

- 2-bit codes, 4 per byte. **Lane order little-endian**: lane 0 occupies bits [1:0], lane 3 bits [7:6].
- **Code map:** `0b00` → 0, `0b01` → +1, `0b10` → −1, `0b11` → **RESERVED — the loader must reject the tape**.
- Rows pad to byte boundaries; padding lanes must be `0b00` → else reject.
- Tensors are contiguous and row-major; declared dims (§6) must satisfy `payload_bytes == ceil(prod(dims) / 4)` → else reject.

## ⚖️ 4. Scales (Power-of-Two Only)

- One `s16` exponent per tensor. Scales are powers of two **by construction** — a producer that cannot quantize to a power-of-two scale must refuse the export, not silently fudge.
- Rationale: this is what keeps hardware normalization **shifts-only**, preserving the zero-multiplier (DSP=0) gate. This single rule is why a float-trained Lux model can land on veriStream silicon without violating the build gate.

## 🏷️ 5. Manifest (Producer-Declared)

Length-prefixed ASCII fields, fixed order: `producer_id` (e.g. `"ternarytape.jl/0.1.0"`), `training_run_id` (32 bytes), `quant_algorithm` tag (e.g. `"absmean-b158-pot"`), `quant_params` blob (algorithm-specific, versioned by tag), `content_hash` (producer-declared).

The loader treats the manifest as **data**: it verifies structural well-formedness and records provenance; it does not interpret producer semantics beyond tags it knows.

## 🏗️ 6. Arch Descriptor

`u16 n_layers`, then per layer: `u16 layer_kind` (enum: `token_shift`, `glu_mux`, `rmsnorm_shift`, `ternary_dense`, `kv_arena`), dims, `u8 reduce_stages` (must equal `clog2(N)` for ternary dot layers → else reject), `u32 latency_cycles`, `u32 context_cap`. Payload sizes cross-checked against the weights and scales sections (§3, §4).

## 🪢 7. Self-Relative Links (Optional Section)

Where a tape carries linked structures (rare for weights; the norm for snapshot images), offsets are stored as narrow `u8`/`u16`/`u32` deltas from the field's own address, per `rt/`'s `__relative(8|16|32)`. On load, every link must pass: bounds check, element-alignment check, `delta == 0` rejection (self-link sentinel), in-section resolution. A link that fails any check rejects the whole tape — corrupt or hostile bytes never become references.

## 🚫 8. Rejection Contract (Fuzz Ground Truth)

A conforming decoder rejects, at minimum: bad magic; unknown major version; nonzero reserved bits/fields; section count mismatch; truncated or oversized `total_length`; any FNV-1a mismatch (header or section); unknown or duplicate section tag; reserved ternary code `0b11`; nonzero padding lane; payload size inconsistent with declared dims; any link violation per §7; malformed manifest field framing.

**Every byte index of a small reference tape must have at least one mutation that flips accept → reject.** This matches the existing exhaustive mutation sweep in `TestTapeAndRelPtr.cpp` and is the acceptance bar for any external encoder.

## 🧪 9. Golden Vectors & Determinism

- `fixtures/accept/`: named `.bin` tapes plus `.json` expectations (loads clean, decoded spot values).
- `fixtures/reject/`: named mutations with the expected rejection reason.
- Encoders must be **deterministic**: same model + params → byte-identical tape. Content addressing depends on it.

## 🔐 10. Open Questions (Honest Boundaries)

1. **FNV-1a 32-bit is a corruption check, not an adversarial defense.** If tamper-resistance ever enters the threat model, add a producer-declared SHA-256 over sections as a v1.1 additive field. State the threat model before claiming it.
2. **Reconciliation with `rt/`:** the actual header field order, header size, and tag numbering in `rt/` win over this proposal; the published spec must be regenerated from the implementation (TDD plan Task 5.6).
3. **Streamed/quantized KV arenas:** whether `kv_arena` banks are exported as tapes or synthesized against `ContextCap` is unresolved.
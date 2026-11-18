# VeriObjC Tape Format Specification v1 — Full `rt/` Reconciliation

**Status:** Frozen v1.0 (`rt/` authoritative — Task 5.6a & Task 5.6b)  
**Authoritative Implementation:** `rt/include/veriobjc_runtime.h`, `rt/include/rt/vtpe_reader.h`  
**Golden Corpora:** `fixtures/vobj_v1/{accept,reject}/` (`VOBJ` single-table snapshots) and `fixtures/vtpe_v1/{accept,reject}/` (`VTPE` multi-section ternary inference tapes)

---

## 0. Architecture & Scope

VeriObjC defines two byte-exact, zero-copy wire formats implemented directly in the freestanding `rt/` micro-runtime without dynamic memory allocation:

1. **Part I — Single-Table Snapshot Frame (`VOBJ` v1):**
   - Implemented by `veriobjc_to_bytes`, `veriobjc_from_bytes`, `veriobjc_verify_rel32_link`, and `veriobjc_tape_validate_v1` in `rt/include/veriobjc_runtime.h`.
   - Designed for deterministic flat-struct arrays containing position-independent self-relative links (`__relative(8)`, `__relative(16)`, `__relative(32)`), such as order-book snapshots (`examples/order_book_snapshot.c`, Task 5.2).
2. **Part II — Multi-Section Ternary Inference Tape (`VTPE` v1.0):**
   - Implemented by `vtpe_v1_validate_tape` (`rt/include/veriobjc_runtime.h`) and `<rt/vtpe_reader.h>` / `<rt/dispatch.h>` / `<rt/scheduler.h>` / `<rt/portfolio_conformance.h>`.
   - Full section specification in `docs/veriobjc-ternary-tape-format-specification-v1-proposal.md`.

In all cases, **`rt/` is authoritative** and VeriObjC CI executes only the freestanding C loader against committed `.bin` + `.json` golden fixtures (no external producer toolchain such as Julia or Python is ever required at test time).

---

## Part I: `VOBJ` v1 Single-Table Snapshot Format (`veriobjc_to_bytes` / `veriobjc_from_bytes`)

### 1.1 Wire Header (`struct veriobjc_tape_header`, 24 Bytes, Little-Endian)

| Offset | Size | C Type | Field | Invariant |
|---|---|---|---|---|
| `0x00` | `4` | `uint32_t` | `magic` | Must equal `0x564F424A` (`"VOBJ"` in little-endian: `'J','B','O','V'`). |
| `0x04` | `2` | `uint16_t` | `version` | Must equal `1` (`VERIOBJC_TAPE_VERSION`). |
| `0x06` | `2` | `uint16_t` | `type_tag` | Producer/consumer schema tag (`expected_tag` passed to `veriobjc_from_bytes`). |
| `0x08` | `4` | `uint32_t` | `elem_size` | Byte size of one record (`expected_elem_size > 0`). |
| `0x0C` | `4` | `uint32_t` | `elem_count` | Number of records in the payload (`0` permitted for empty snapshots). |
| `0x10` | `4` | `uint32_t` | `payload_bytes` | Must equal `(uint64_t)elem_size * (uint64_t)elem_count` and `in_len - 24`. |
| `0x14` | `4` | `uint32_t` | `checksum` | FNV-1a-32 over header prefix bytes `0x00..0x13` (20 bytes) followed by all `payload_bytes` (`0x18 .. 0x18 + payload_bytes - 1`). |

### 1.2 FNV-1a-32 Checksum Rule (`veriobjc_tape_checksum`)

- Offset basis: `0x811c9dc5u`
- Prime: `0x01000193u`
- Domain: The first `offsetof(struct veriobjc_tape_header, checksum) = 20` bytes of `struct veriobjc_tape_header` (`0x00..0x13`), followed sequentially by `payload[0 .. payload_bytes - 1]`.
- Because `checksum` itself occupies `0x14..0x17`, **every single byte index `0 .. in_len - 1`** in any valid `VOBJ` v1 frame is covered either by the FNV-1a-32 domain (`0x00..0x13` and `0x18..end`) or by the `checksum` comparison (`0x14..0x17`). Thus any single-byte mutation flips accept → reject.

### 1.3 Self-Relative Link Encoding (`__relative(8|16|32)`)

| Macro / Struct | Storage | Null Sentinel | Non-Null Resolution | Set Range & Self-Reference Invariant |
|---|---|---|---|---|
| `__relative(8)` / `struct veriobjc_rel8` | `int8_t offset` | `offset == 0` → `NULL` | `(const uint8_t *)link + link->offset` | `delta = target - link`; rejects `delta == 0` (self-link) or `delta ∉ [-128, 127]`. |
| `__relative(16)` / `struct veriobjc_rel16` | `int16_t offset` | `offset == 0` → `NULL` | `(const uint8_t *)link + link->offset` | `delta = target - link`; rejects `delta == 0` (self-link) or `delta ∉ [-32768, 32767]`. |
| `__relative(32)` / `struct veriobjc_rel32` | `int32_t offset` | `offset == 0` → `NULL` | `(const uint8_t *)link + link->offset` | `delta = target - link`; rejects `delta == 0` (self-link) or `delta ∉ [INT32_MIN, INT32_MAX]`. |

### 1.4 Link Boundary & Alignment Verification (`veriobjc_verify_rel32_link`)

Given a loaded payload `[payload_base, payload_base + payload_bytes)` of records of size `elem_size`, a `struct veriobjc_rel32 *link` is valid if and only if:
1. `link != NULL && payload_base != NULL && elem_size > 0 && payload_bytes >= elem_size`.
2. `(const uint8_t *)link >= base` and `(const uint8_t *)link <= base + payload_bytes - sizeof(struct veriobjc_rel32)`.
3. Either `link->offset == 0` (null sentinel), **or**:
   - `target_pos = ((const uint8_t *)link - base) + link->offset` satisfies `0 <= target_pos <= (ptrdiff_t)(payload_bytes - elem_size)`, and
   - `(size_t)target_pos % elem_size == 0` (points to the exact start of a record in the array).

### 1.5 Named Rejection Contract (`veriobjc_tape_error_t` / `veriobjc_tape_validate_v1`)

| Code | Diagnostic Constant | Condition |
|---|---|---|
| `0` | `VERIOBJC_TAPE_OK` | Frame and all verified `rel32` link slots pass. |
| `1` | `VERIOBJC_TAPE_ERR_NULL_OR_TRUNCATED` | `in_buf == NULL`, `expected_elem_size == 0`, or `in_len < 24`. |
| `2` | `VERIOBJC_TAPE_ERR_HEADER` | `magic != 0x564F424A`, `version != 1`, `type_tag != expected_tag`, or `elem_size != expected_elem_size`. |
| `3` | `VERIOBJC_TAPE_ERR_PAYLOAD_SIZE` | `elem_size * elem_count != payload_bytes` or `in_len - 24 != payload_bytes`. |
| `4` | `VERIOBJC_TAPE_ERR_CHECKSUM` | Computed FNV-1a-32 does not equal `hdr.checksum`. |
| `5` | `VERIOBJC_TAPE_ERR_REL_LINK` | One or more `rel32` link fields inside the loaded records fail `veriobjc_verify_rel32_link`. |

---

## Part II: `VTPE` v1.0 Multi-Section Ternary Inference Tape Reconciliation

| Property | `VOBJ` v1 (`veriobjc_from_bytes`) | `VTPE` v1.0 (`vtpe_v1_validate_tape` / `<rt/vtpe_reader.h>`) |
|---|---|---|
| Magic | `0x564F424A` (`"VOBJ"`) | `0x45505456` (`"VTPE"`) |
| Header Size | `24` bytes (`struct veriobjc_tape_header`) | `32` bytes (`struct vtpe_v1_header`) |
| Checksum | FNV-1a-32 over `0x00..0x13` + payload | FNV-1a-32 over 28 non-CRC header bytes + FNV-1a-64 `artifact_id` + per-section FNV-1a-32 |
| Self-Relative Links | Inline `struct veriobjc_rel32` inside records | Section `0x0005` (`link_table`) 4-byte aligned relative offsets |
| Golden Corpus | `fixtures/vobj_v1/{accept,reject}/` (8 tapes) | `fixtures/vtpe_v1/{accept,reject}/` (12 tapes) |
| CI Gate | `TestTapeAndRelPtr.cpp` (CTest `unit_rt_tests`) | `TestVtpeV1GoldenCorpus.cpp` + `portfolio_conformance_tests` |

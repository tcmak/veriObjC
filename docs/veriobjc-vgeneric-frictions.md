# 🐛 Phase 3 Task 3.3b: `vgeneric` Compiler Friction Log (`<vstd/vector.h>`)

Per the Phase 3 Execution Guide (*"expect compiler friction, and treat every friction as signal: a container that can't be written cleanly in the language is a language bug, not a library workaround. File, don't finesse"*), this document records the concrete compiler/plugin frictions discovered when implementing `VVector<T : VVectorItem *>` over `vgeneric` in Task 3.3b.

---

## 1. Friction `VG-001`: Substituted Return-Type Mismatch on Devirtualized `vgeneric` Methods

- **Component:** `compiler/src/VeriObjCPlugin.cpp` (`buildDevirtualizedCall`)
- **Minimal Reproducer:**
  ```objc
  @interface VVector<T : VVectorItem *> {
  @public
      uint32_t count;
      uint32_t cap;
      T buf[4];
  }
  - (T)at:(uint32_t)idx;
  @end

  OrderLotItem *read_first(VVector<OrderLotItem *> *vec) {
      return [vec at:0u]; // Clang substitutes return type of ObjCMessageExpr to OrderLotItem *
  }
  ```
- **Symptom:**
  `getOrCreateDirectMethodDecl` synthesizes `__veriobjc_direct_VVector__at_` using the unspecialized `@implementation` return type (`VVectorItem *`), while the caller's `ObjCMessageExpr` has the substituted return type `OrderLotItem *`. Replacing the `ObjCMessageExpr` with the raw `CallExpr` without casting to `E->getType()` triggers `-Wincompatible-pointer-types` (an error under `-Werror`).
- **Root Cause & Fix:**
  `buildDevirtualizedCall` cast `RecvExpr` (`self`) to `ExpectedSelfTy` and cast arguments, but returned the `CallExpr` with `DirectFD->getReturnType()` instead of casting to `E->getType()`. Wrapping the `CallExpr` in an `ImplicitCastExpr` (`CK_BitCast`) to `E->getType()` when `DirectFD->getReturnType().getCanonicalType() != E->getType().getCanonicalType()` preserves the specialized `vgeneric` type across devirtualization.

---

## 2. Friction `VG-002`: Covariant `vgeneric` Ivar Array Rejected by Range-For Lowering (`for (T x in vec)`)

- **Component:** `compiler/src/VeriObjCPlugin.cpp` (`findBoundedArrayIvar`, `buildLoweredForCollection`)
- **Minimal Reproducer:**
  ```objc
  int32_t sum_lots(VVector<OrderLotItem *> *vec) {
      int32_t total = 0;
      for (OrderLotItem *item in vec) {
          if (item) total += item->lot_qty;
      }
      return total;
  }
  ```
- **Symptom:**
  `VeriObjC Violation: Range-based for loop requires a container with a statically bounded array field.`
- **Root Cause & Fix:**
  1. `findBoundedArrayIvar` checked `CandCAT->getElementType().getCanonicalType() == ElemVD->getType().getCanonicalType()`. On `VVector<T : VVectorItem *>`, the backing ivar `T buf[4]` has element type `VVectorItem *` (the `ObjCTypeParamType` bound), whereas the loop variable `ElemVD` has the specialized subclass type `OrderLotItem *`.
  2. Furthermore, a partially filled `VVector` (`count <= 4`) needs range-for to iterate up to `vec->count` if a `count` ivar is present (or the static array bound `CAT->getSize()`), and dereferencing `*__veriobjc_it` (`VVectorItem *`) to initialize `OrderLotItem *item` requires an `ImplicitCastExpr` (`CK_BitCast`) to `ElemVD->getType()`.
  3. **Fix:** Update `findBoundedArrayIvar` to permit covariant Objective-C object pointer assignment (`Context->canAssignObjCInterfaces`) between the ivar element bound and `ElemVD->getType()`, and emit a `CK_BitCast` to `ElemVD->getType()` in `buildLoweredForCollection`.

---

## 3. Friction `VG-003`: Static Capacity Tracking & Sealed-Bound Enforcement on `VVector<T>` Specializations

- **Component:** `compiler/src/VeriObjCPlugin.cpp` (`analyzeVResultAndStackArrays`, `checkLightweightGenerics`)
- **Minimal Reproducer:**
  ```objc
  // Case A: Pushing beyond VVector<T> capacity via Objective-C message send
  void overflow_generic_vec(VVector<OrderLotItem *> *vec, OrderLotItem *it) {
      [vec push:it]; [vec push:it]; [vec push:it]; [vec push:it];
      [vec push:it]; // 5th push on capacity-4 VVector<T>
  }

  // Case B: Specializing VVector<T> with an unsealed class
  @interface Unsealed rogue { @public int x; } @end
  void bad_spec(VVector<Unsealed *> *vec);
  ```
- **Symptom:**
  1. Static capacity overflow tracking in `analyzeVResultAndStackArrays` only inspected C `CallExpr` nodes (`__vvector_push_raw`), missing Objective-C `[vec push:it]` `ObjCMessageExpr` nodes prior to devirtualization.
  2. `checkLightweightGenerics` rejected `NSArray<...>` / `NSDictionary<...>` / `NSSet<...>`, but did not verify that type arguments passed to a user `vgeneric` specialization (`VVector<Unsealed *>`) are `@sealed` classes (`isSealedClass`).
- **Root Cause & Fix:**
  1. Extend `analyzeVResultAndStackArrays` to inspect `ObjCMessageExpr` with selector `push:` on `objc_vvector:<Cap>`-annotated interfaces (as well as devirtualized `__veriobjc_direct_VVector__push_` calls).
  2. Extend `checkLightweightGenerics` so that any specialized `ObjCObjectType` (`VVector<Arg *>`) verifies that every type argument `Arg` is marked `__attribute__((objc_sealed))`, emitting `"VeriObjC Violation: Invalid vgeneric type argument '%0': type arguments to constrained generics must be @sealed classes."`

---

## 4. Friction `VG-004`: Compound-Literal Temporary in C-Mode `vvector_push`

- **Component:** `vstd/include/vstd/vector.h` (`vvector_push`)
- **Minimal Reproducer:**
  ```c
  vvector_push(&vec, parse_value(x));
  ```
- **Symptom:**
  The initial C macro passed `(const void *)(const ElemType[1]){ (val) }` to `__vvector_push_raw` and copied bytes in a loop, materializing a stack temporary before copying into `vec.buf[idx]`.
- **Root Cause & Fix:**
  Replace `__vvector_push_raw` with direct slot allocation (`__vvector_alloc_slot_raw`) followed by direct assignment into `(vec_ptr)->buf[(vec_ptr)->count - 1u] = (val)`. Disassembly (`llvm-objdump -d`) confirms `parse_value(x)` writes its return register `%eax` directly into the `vec.buf` slot with zero temporary and zero `memcpy`.

---

## 5. Friction `VG-005`: `vgeneric` Lacks Value / Integer Parameters and Non-Pointer Value Type Arguments (Blocks `vvector<T, N>`)

- **Component:** Clang Objective-C `ObjCTypeParamList` Parser / `VeriObjCPlugin.cpp` (tracked for **Task 3.3c**)
- **Status:** Open — Tracked in **Task 3.3c** (`__vvector_storage_def` retained only as deprecated internal bridge until resolved)
- **Minimal Reproducer:**
  ```objc
  // 1. Non-type integer capacity parameter N is rejected by Clang's ObjC generic parser:
  @interface VVector<T : VVectorItem *, uint32_t N> {
  @public
      uint32_t count;
      T buf[N];
  }
  @end

  // 2. Scalar / flat C struct value specialization is rejected by Clang's ObjC type parameter check:
  VVector<int32_t, 4> batch; // error: type argument 'int32_t' is not an Objective-C object pointer
  ```
- **Symptom:**
  Clang's built-in Objective-C lightweight generic grammar (`ParseObjCTypeParamList` / `Sema::ActOnObjCTypeParam`) requires every parameter in `<...>` to be an Objective-C object pointer type (`id` or `ClassName *`). Integer capacity constants (`N`) and flat value types (`int32_t`, `@flat struct`) cannot be passed as `<T, N>` arguments without a custom frontend/plugin lowering pass.
- **Planned Resolution (Task 3.3c):**
  Implement `vvector<T, N>` monomorphization lowering in `VeriObjCPlugin` so value types `T` and compile-time integer capacities `N` (`vconstexpr`) specialize a single canonical container definition, then delete the internal `__vvector_storage_def` bridge macro.

---

## 6. Friction `VG-006`: Flat `@interface` Cannot Be Statically Allocated or Returned by Value (Blocks Return-by-Value on `VVector<T>`)

- **Component:** Clang Objective-C Sema (`SemaType.cpp` / `SemaDecl.cpp`) / `VeriObjCPlugin.cpp` (tracked for **Task 3.3c**)
- **Status:** Open — Tracked in **Task 3.3c** (`__vvector_storage_def` retained only as deprecated internal bridge until resolved)
- **Minimal Reproducer:**
  ```objc
  VVector<OrderLotItem *> build_vector_by_value(OrderLotItem *a, OrderLotItem *b) {
      VVector<OrderLotItem *> out; // error: interface type cannot be statically allocated
      [&out initVec];
      [&out push:a];
      [&out push:b];
      return out; // error: interface type 'VVector<OrderLotItem *>' cannot be returned by value
  }
  ```
- **Symptom:**
  Stock Clang's Objective-C Sema unconditionally forbids non-pointer `ObjCObjectType` in local variable declarations, parameter types, and function return types (`diag::err_objc_object_assignment` / `diag::err_statically_allocated_object`), even when the `@interface` is `@final`, `@sealed`, and has a fixed compile-time layout (`count`, `cap`, `buf[N]`).
- **Planned Resolution (Task 3.3c):**
  Lower `@flat` / value-convertible `@final @sealed` container interfaces (`vvector<T, N>`) to value-type C record layouts in ABI/return positions so `VVector<T>` can be allocated inline on the C call stack and returned by value via `sret`/registers without `__vvector_storage_def`.

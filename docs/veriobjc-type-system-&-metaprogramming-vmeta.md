# 🧬 VeriObjC Type System &amp; Metaprogramming (`vmeta`)

This specification defines genericity, compile-time computation, algebraic data types, strong types, borrow discipline, and layout control — the metaprogramming surface of VeriObjC.

**The design constraint that shaped everything:** C++ template metaprogramming is Turing-complete *by accident*, has no cost model, reports errors at the 47th instantiation instead of the definition, and can OOM the compiler. Every mechanism below is chosen so that compile-time work is ordinary code with a **measurable, bounded cost**, and so that the compiler itself can never be the thing that falls over.

---

## 0. What Traditional Objective-C Has (and Why None of It Survives)


| Mechanism in classic ObjC                                             | What it actually is                                                                                                                         | VeriObjC verdict                                                                                                   |
| :--------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------ |
| The C preprocessor                                                    | Text substitution; no syntax, no types, no scope                                                                                            | Kept only as `#import`/include guards; no computation macros                                                       |
| Lightweight generics (`NSArray<NSString *>`) (2015)                   | **Erased hints**: covariance annotations for the compiler and ARC; no monomorphization, no runtime existence, no generic *functions* at all | Replaced by real constrained generics (§1)                                                                         |
| Runtime reflection (`class_copyMethodList`, `NSClassFromString`, KVC) | The only real "metaprogramming" — done *at run time* on string tables                                                                       | Banned; replaced by compile-time `@reflect()`                                                                      |
| Categories + swizzling                                                | Runtime method table surgery                                                                                                                | Banned                                                                                                             |
| External codegen tools (protobuf/mogenerator)                         | Separate programs emitting source                                                                                                           | Absorbed into the plugin: `@reflect` + `objc_soa` are in-toolchain codegen whose output is visible in the AST dump |


So VeriObjC does not inherit a metaprogramming model; it must design one. It takes the *uses* the HFT world gets from C++ templates (generic containers, compile-time arithmetic, trait dispatch, `variant`+`visit`) and drops the mechanism that makes them dangerous.

---

## 1. Constrained Generics (`vgeneric`)

Genericity is **checked at the definition, not at instantiation** — the single largest divergence from C++. A generic body must typecheck *once* against its declared constraints. There is no two-phase lookup, no "works for `int` but explodes for `std::string`" surprise.

```objc
// Parameterized class. The constraint names a requirement, and it is
// checked here, at the definition — for every body in the file.
vgeneric <T : FlatValue>
@interface FrameStore<T> {
    vvector<T, 4096> slots;
}
- (uint32_t)put:(borrow T)frame;
@end

// Generic method, same rule: constraints are part of the signature.
vgeneric <T : Numeric>
- (T)clamp:(T)value min:(T)lo max:(T)hi;

// Constraints are protocols, sealed-class lists, or value predicates:
vgeneric <T : @sealed InstrumentProtocol>
vgeneric <N : vconstexpr uint32_t>          // compile-time value parameter
vgeneric <K : FlatValue & Hashable>
```

**Rules:**

- Every type parameter is constrained. Unconstrained parameters (`typename T`-style) are a compile error.
- Constraint sets are closed: a protocol constraint enumerates its requirements; the plugin checks the body against exactly those requirements. No duck typing at instantiation.
- Specialization is **not a metaprogramming mechanism**: overriding a generic for one type is a plain method overload or an explicit `where` clause at the definition. No partial specialization, no SFINAE, no overload-resolution-by-substitution-failure.

### Two instantiation strategies, chosen explicitly


| Strategy                                                         | Meaning                                                                              | Cost model                                                           |
| :---------------------------------------------------------------- | :------------------------------------------------------------------------------------ | :-------------------------------------------------------------------- |
| `@mono` (default for small value types)                          | Monomorphized per type argument, direct inlining                                     | One code copy per instantiation; **counted against the budget (§3)** |
| `@erased` (default for protocol-constrained, many-type generics) | One shared instantiation + link-time witness tables (sealed world makes this closed) | One code copy total; dispatch is a constant-time table read          |


The compiler proposes a strategy per generic in the `--mono-report`; the choice is recorded and reviewed like any effect decision.

---

## 2. Compile-Time Computation: `vconstexpr` — an Interpreter, Not a Type System

Compile-time work is **ordinary code executed by a bounded interpreter**, not template recursion. This is the direct answer to the trader-who-OOM'd-the-compiler story: there is no instantiation-driven recursion to explode; there is a step counter that stops with a clean error.

```objc
// Ordinary code, evaluated at compile time. Deterministic, memoized.
vconstexpr uint32_t roundUpPow2(uint32_t x) {
    vstatic_assert(x != 0, "must be nonzero");
    return 1u << (32 - clz32(x - 1));
}

// Compile-time-only: cannot be called at run time at all.
consteval uint32_t BookDepth = roundUpPow2(MAX_LEVELS);

// Tables: the classic HFT use of template metaprogramming, done readably.
vconstexpr uint32_t PriceToTickTable[65536] = buildTickTable();
```

**Fuel — the cost model:**

- Every `vconstexpr` evaluation runs with a **fuel budget**: max interpreter steps, max recursion depth, max result allocation (default e.g. 10⁶ steps / 256 depth / 16 MiB).
- Exhausting fuel is a **clean compile error naming the function, line, and steps consumed** — never a hang, never an OOM. Fuel is a language-level constant, adjustable per package manifest and *reported*, like everything else.
- `vconstexpr` functions may not: read mutable globals, call `@io`/`@blocking`/`@cold` code, or construct heap objects. They are pure interpretable code.
- Results are memoized program-wide by (function, arguments).

### Traits: enumerating instead of branching

The Day-10 pattern — "raise a runtime branch into a compile-time trait" — is served by `vtrait`, a closed trait table evaluated by the interpreter:

```objc
vtrait HashStrategy<T> { ... }        // defined per sealed type list
vconstexpr uint32_t hashKindOf = vtrait<HashStrategy, OrderId>::kind;
// Branch on hashKindOf and the fold is constant; nothing survives to run time.
```

Since the sealed world enumerates every concrete type, traits are exhaustive tables, not open recursion. Picking a *type* is `vconditional<THashable, TSorted, ...>` — again a closed enumeration resolved by the interpreter, never by overload probing.

---

## 3. The Monomorphization Budget (The Anti-Template-Explosion Guard)

C++ OOMs because instantiation is unbounded, invisible, and repeated per translation unit. VeriObjC makes instantiation a **budgeted, reported, deduplicated resource**:

1. **Program-wide dedup:** instantiations are keyed by mangled (generic, argument-set) and shared across the whole link. One `FrameStore<Instrument>` — not one per file that includes the header.
2. **Declared budgets:** each package manifest declares an instantiation budget (`mono_budget: 4096`) and a generated-code budget in bytes. Exceeding either is a **compile error naming the package and the offending generic** — not a memory error at 3 a.m.
3. **`--mono-report`:** per generic: instantiation count, code bytes, strategy (`@mono`/`@erased`), and the top consumers. Code review sees code-size cost the way `--effects-report` shows runtime cost.
4. **Explicit instantiation:** a package may (and for libraries *must*) publish its instantiation list in the manifest, so downstream code cannot silently multiply it.
5. **Variadics are bounded:** `vpack` is limited to depth 8; folds expand at parse into a fixed shape. Recursive pack processing is expressed as a `vconstexpr` loop over a `vtuple` — an interpreter loop with fuel, not recursive instantiation.

**Acceptance criterion:** the compiler can never die of metaprogramming. Every compile-time cost (steps, depth, instantiations, bytes) is finite, declared, and reported.

---

## 4. Algebraic Data Types: `vvariant` + Exhaustive `match`

The closed-set polymorphism that HFTs get from `std::variant` + `std::visit` (order states, message types, venue dispatch) — a *value-typed* alternative to class hierarchies, dispatched through jump tables:

```objc
vvariant OrderState {
    New                 { uint32_t seq; }
    PartiallyFilled     { uint32_t filledQty; }
    Filled              { uint32_t filledQty; }
    Cancelled           { uint32_t reasonCode; }
};

// Two storage modes, chosen at the point of use (Goose-style):
vvariant_storage OrderState fixed;      // tag + largest payload: indexable, overwritable
vvector<OrderState, 8192> levels;       // packed mode: each element sized to its variant

// Exhaustive, jump-table dispatch. Missing a case = compile error.
match (state) {
    Filled &f => { totalFilled += f.filledQty; },
    Cancelled &c => { tape.record(c.reasonCode); },
    _ => { /* remaining cases explicitly handled */ }
};
```

- Exhaustiveness is checked at compile time; `@pre`/contract reasoning can see the tag, so match arms inside a proven-one-sided tag are eliminated.
- Packed (variable-size) mode stores each value at its variant's exact size — sequential, cache-streaming, not per-value overhead like `sizeof(largest)`.
- Multi-variant match (`visit` over two variants) generates the cross product up front; the budget rule from §3 applies to it, with the same clean error if it explodes.

---

## 5. Strong Types (`vstrong`)

Zero-cost, no implicit conversions, checked everywhere:

```objc
vstrong Price  : int64_t;   // ticks
vstrong Qty    : int32_t;
vstrong OrderId: uint64_t;

Price p = Price(1234);      // explicit construction only
Qty   q = Qty(10);
// q + p  → compile error: no operator + between Qty and Price
int64_t raw = p.raw;        // unwrapping is explicit and greppable
```

Strong types interoperate with `vvariant` tags, channel payloads (still flat), and `@pre` contracts (a contract can now say `@pre(qty > Qty(0))`).

---

## 6. Borrow, Sink, and Weak — the Ownership Taxonomy

Named, checked forms of sink/borrow/share lesson — `@shared` refcounting is quarantined to `@cold`, so "share" becomes **weak observation** instead:

```objc
- (void)consume:(move Order)order;        // SINK: caller provably loses ownership (linear tracking)
- (bool)validate:(borrow Order)order;     // BORROW: non-owning; rooted at the caller's scope;
                                          // escaping the parameter's root = compile error
- (void)track:(vweak<OrderRef>)ref;       // WEAK: pool index + generation counter
```

- `borrow` is the default parameter form for non-trivial values; borrows may not be stored into anything that outlives the callee frame (root check, same machinery as data-stack references).
- `vweak<T>` is a **generation-counted** pool handle: `allocRef` bumps the slot's generation, `free` bumps it again; a `vweak` whose generation no longer matches reports *stale* instead of silently reading whatever the next owner wrote. This detects the one bug Goose's pools deliberately leave as "same-typed garbage."
- `vweak` cannot be dereferenced on constrained paths without a `guard` that both checks the generation and re-roots the borrow.

---

## 7. Layout Control Attributes

Padding/alignment lesson as dialect syntax, because wire codecs require exact layouts:

```objc
typedef struct __packed  { uint16_t len; uint8_t payload[64]; } WireHeader;  // no padding
typedef struct __align(64){ uint64_t seq; } AlignedSeq;                      // explicit alignment
typedef struct { __empty_member LogTag tag; uint32_t x; } Node;              // zero-size member, zero footprint
```

`__packed` types are rejected as channel payloads on architectures where unaligned access is not trivially safe — the layout that helps the wire hurts the core, so the type system separates them. `vstatic_assert(sizeof(WireHeader) == 66)` locks the layout into CI.

---

## 8. What VeriObjC Deliberately Rejects


| C++ mechanism                                 | Why it's rejected                                   | VeriObjC replacement                                                     |
| :--------------------------------------------- | :--------------------------------------------------- | :------------------------------------------------------------------------ |
| Unconstrained `typename T`                    | Errors at instantiation; no contract                | Constrained `vgeneric` (§1)                                              |
| SFINAE / substitution probing                 | Overload resolution *is* unbounded type computation | Named constraints, plain overloads, `vconditional` tables                |
| Template specialization as a programming tool | Hidden per-type behavior; the explosion vector      | Method overloads / explicit `where` at definition                        |
| Recursive template metaprogramming            | No cost model; the OOM story                        | `vconstexpr` interpreter with fuel (§2)                                  |
| CRTP                                          | Compile-time polymorphism via duplication           | Sealed protocols, statically dispatched; `@erased` generics (§1)         |
| Unbounded variadic packs                      | Fold/combining explosions                           | `vpack` depth 8; `vconstexpr` loops over `vtuple` (§3)                   |
| Macros generating code                        | Invisible, unhygienic                               | `@reflect` / `objc_soa` codegen, visible in the AST dump                 |
| `std::function` type erasure                  | Hidden heap allocation                              | Function blocks; `@erased` generics for stored callables in `@cold` code |


---

## 9. Mapping to the HFT C++ Curriculum


| Training topic (Day)      | C++ mechanism                               | VeriObjC mechanism                                              |
| :------------------------- | :------------------------------------------- | :--------------------------------------------------------------- |
| Templates &amp; concepts  | Templates, specialization, two-phase        | `vgeneric`, definition-site checking, no specialization-as-TMA  |
| Compile-time work         | Template metaprogramming, `constexpr`       | `vconstexpr` with fuel; `vtrait` tables; `consteval`            |
| Variadics, folds          | Parameter packs, fold expressions           | Bounded `vpack`, `vtuple` loops                                 |
| CRTP, `deducing this`     | Static polymorphism workarounds             | Sealed protocols + static dispatch; `@erased` generics          |
| `variant` + `visit`       | Library ADTs                                | `vvariant` + exhaustive `match`, two storage modes              |
| Strong types (`mpl::Tag`) | Hand-rolled tag wrappers                    | `vstrong` newtypes                                              |
| Borrow/sink/share         | Smart pointer conventions                   | `move` / `borrow` / `vweak` with root &amp; generation checks   |
| Padding &amp; alignment   | `alignas`, `#pragma pack`                   | `__packed`, `__align`, `__empty_member` + layout asserts        |
| Template cost warnings    | "Every template parameter gets constrained" | Constraints mandatory + `--mono-report` + instantiation budgets |
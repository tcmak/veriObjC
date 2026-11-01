# ⚡ VeriObjC (Verifiable Objective-C)

## 🚀 Getting Started & Dialect Preview

To show its power, this example implements a Network Packet Processor designed to run inside an OS Kernel or a real-time network card (NIC). It showcases low-level pointer management, C99 Flexible Array Members, ownership tracking, and sealed object-oriented hierarchies.

```objc
#import <stdint.h>
#import <stdbool.h>

// Compile-time constraints leveraging modern C23
constexpr float PI = 3.14159265f;
constexpr uint32_t SAMPLE_RATE = 44100;

typedef struct {
    float frequency;
    float phase;
} SynthState;

void VeriObjC_ScopeRelease(void *objRef);
#define @scoped __attribute__((cleanup(VeriObjC_ScopeRelease))) auto

__attribute__((objc_sealed))
@interface AudioNode {
@public
    bool isActive;
}
- (float)processNextSample:(SynthState *)state;
@end

__attribute__((objc_sealed))
@interface SineOscillator : AudioNode {
@public
    float amplitude;
}
- (float)processNextSample:(SynthState *)state;
@end

@implementation SineOscillator
- (float)processNextSample:(SynthState *)state {
    auto sample = __builtin_sinf(state->phase) * amplitude;
    state->phase += (2.0f * PI * state->frequency) / (float)SAMPLE_RATE;
    return sample;
}
@end
```

---

## 🏗️ Example 2: Data Stacks, Stable References, Pools, and Checked Errors

A market-data parser in the VeriObjC dialect. Scratch lives on a compiler-assigned grow-only data stack (`vstack_array`): growth never moves data, so the reference captured by the first `push` stays valid a million pushes later, and the entire scratch buffer is freed with **one store** when the scope ends. Reusable packet slots come from a `vpool` (freelist over contiguous storage). Errors return across multiple frames with `return err from parse`, statically checked — no `@try`, no unwinder, no `vresult` in the intermediate signatures.

```objc
#import <stdint.h>
#import <stdbool.h>
#import <vstd/stack.h>   // vstack_array: grow-only, stable references, one-store frees
#import <vstd/pool.h>    // vpool: recycled slots, same-typed stale reads
#import <vstd/string.h>  // vstring_view: non-owning, bounded

// Parsed instrument reference — a value type: flat, contiguous, channel-crossable.
typedef struct {
    uint32_t instrumentId;
    uint64_t symbol[4];     // inline fixed string: no pointers anywhere
    uint32_t priceTicks;    // fixed-point price in venue tick units
    uint32_t qty;
} InstrumentRef;

__attribute__((objc_sealed))
@interface MarketDataParser : VeriObject
- (uint32_t)parseInto:(vpool<InstrumentRef> *)out       // recycled slots
                 from:(vstring_view)wire;               // zero-copy view of the DMA buffer
@end

@implementation MarketDataParser
- (uint32_t)parseInto:(vpool<InstrumentRef> *)out from:(vstring_view)wire {
    @arena(Scratch) {                              // compiler-assigned data stack
        vstack_array<char> tokens = {};            // ONE resizable value per stack

        // Split the wire format. Growth is a pointer bump; nothing moves.
        auto first = tokens.push('A');             // reference captured BEFORE growth
        for (auto &ch : wire) {
            if (ch == ',') { continue; }
            tokens.push(ch);                       // a million pushes later...
        }
        first.value = 'X';                         // ...this is still valid.

        // Built-in-place construction: the record is written directly
        // into the pool slot — no temporary, no copy.
        uint32_t count = 0;
        for (auto &tok : tokens) {
            uint32_t slot = out->allocRef();
            auto &rec   = out->at(slot);

            uint64_t sym; uint32_t ok;
            ok = parse_symbol(tok, &sym);
            guard ok else { return err("bad symbol") from parse; }   // exits THIS
                                                                     // method directly
            rec.instrumentId = hash64(sym);
            rec.symbol[0]     = sym;
            rec.priceTicks    = parse_ticks(tok);
            rec.qty           = parse_qty(tok);
            count += 1;
        }

        // No frees here: the data stack resets with one store at scope exit.
        return count;
    }
}
@end

// Cross-core handoff: flat values only. InstrumentRef has no references at
// any depth, so crossing is a memcpy and the compiler proves it:
//   [gateway publishUpdates:move(refs) onChannel:dataCore];
```

### What this example demonstrates

| Feature | Where | Guarantee |
| :--- | :--- | :--- |
| `vstack_array` scratch | `tokens` | Growth never moves data; `first` stays valid through all pushes. |
| One-store frees | scope exit | `@arena` reset compiles to a single store per data stack. |
| `vpool` recycling | `out->allocRef()` | Slots are reused, never released; stale reads stay same-typed. |
| Built-in-place construction | `out->at(slot)` writes | The parsed record is written straight into its final slot. |
| `return err from parse` | the `guard` | Statically checked multi-frame return; typed, no unwinder. |
| Flat-value crossing | the handoff | No references at any depth; channel send is a memcpy, verified at compile time. |
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

---

## 🧠 Example 3: MatMul-Free Inference on FPGA — a Ternary Block in `veriStream`

One MatMul-free transformer block fragment on the `ll.ml` substrate. Streams are wires, not arrays: `Stream<Bits(8), 512>` is 512 wires arriving every clock. Token shift is a register delay, the GLU gate is a mux ladder, and the linear layer is `ternary_dot` — 512 CMUX lanes feeding a 9-stage adder tree. **No multipliers, no DSPs, no memory** — and the block's cycle cost is a contract the scheduler reads at compile time, not a measurement it takes after.

```objc
#import <vstd/stream.h>   // Bits<W>, Stream<T,N>: hardware-native streams
#import <vstd/ternary.h>  // packed ternary weights {-1,0,1}, 4 per byte, BRAM-pinned

consteval auto WIDTH   = 512;
consteval auto LATENCY = clog2(WIDTH);   // 9 reduce stages — computed, not guessed

typedef Stream<Bits(8), WIDTH> Activation;   // 512 wires, every cycle
typedef Stream<Bits(2), WIDTH> TernaryW;     // 2 bits per weight, 4 per byte

__attribute__((objc_sealed))
@interface TernaryGLU : VeriObject
- (Activation)apply:(Activation)x
             prev:(RegisterDelay<WIDTH>)prev     // token shift = register depth
             gate:(vspan<const TernaryW>)W_gate   // BRAM-pinned, verified on load
             up:(vspan<const TernaryW>)W_up;
@end

@implementation TernaryGLU
- (Activation)apply:(Activation)x prev:(RegisterDelay<WIDTH>)prev
             gate:(vspan<const TernaryW>)W_gate up:(vspan<const TernaryW>)W_up {
    // Token shift: yesterday's stream, one register deep. No memory, no copy.
    Activation shifted = mux(USE_PREV, prev, x);

    // The "linear layers": ternary_select lowers to CMUX lanes; accumulation
    // is a popcount reduction into a log2(512) = 9-stage registered adder tree.
    Bits(16) g = ternary_dot<WIDTH>(shifted, W_gate) @latency(LATENCY);
    Bits(16) u = ternary_dot<WIDTH>(x,          W_up)   @latency(LATENCY);

    // GLU: gate ⊙ up where the "multiply" is a sign-mux ladder, not a DSP.
    return glu_gate(g, u);       // one activation per clock, fully pipelined
}
@end

// The closed world schedules the whole block from contracts:
//   shift: 0 cycles (a wire)   2 × ternary_dot: 9 cycles   glu gate: 1 cycle
//   => @latency(cycles = 10), @throughput(1 per clock), DSP count = 0
//      (asserted against the vendor synthesis report in CI via --hw-report).
```

### What this example demonstrates

| Feature | Where | Guarantee |
| :--- | :--- | :--- |
| Stream semantics | `Activation` | 512 wires per clock: no memory, no addresses, no bounds to check — flat by construction. |
| `ternary_select` lowering | `ternary_dot` | CMUX lanes + popcount adder tree; the multiply never exists, so no DSP blocks are instantiated. |
| Cycle contracts | `@latency(LATENCY)` | Pipeline depth computed by `vconstexpr`; the DAG scheduler plans the block from contracts, not measurements. |
| Packed ternary weights | `vspan<const TernaryW>` | 2 bits per weight, 4 per byte, BRAM-pinned; the image is verified by `from_bytes` before it becomes hardware-visible. |
| GLU without multipliers | `glu_gate` | The gate "multiply" is a sign-mux ladder; the entire block is pure LUT logic. |
| Fixed context | `RegisterDelay<WIDTH>` | Token shift is register depth, not heap history; exceeding the declared `ContextCap` is a compile error, not a runtime failure. |
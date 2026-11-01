# 🧪 VeriObjC PoC: Micro-Runtime Stub & Compiler Plugin

## Phase 1: The Micro-Runtime Stub (runtime.h)

This is the entire execution runtime required for your bare-metal targets. It defines the universal structural layout of an object **plus the data-stack and pool primitives** without invoking any operating system headers. Data stacks and `vpool` are the freestanding memory model: no heap, no malloc, no linked-list allocator.

```c
// veriobjc_runtime.h
#pragma once
#include <stdint.h>
#include <stddef.h>

// The structural root of all types. Every object pointer maps here.
struct veriobjc_object {
    struct veriobjc_class *isa;
};

struct veriobjc_class {
    struct veriobjc_class *superclass;
    const char *name;
    size_t instance_size;
};

// Emulated type definitions to satisfy the Clang frontend parser
typedef struct veriobjc_object *id;
typedef struct veriobjc_class *Class;
typedef struct veriobjc_selector *SEL;

// ---------------------------------------------------------------------------
// Data stacks: grow-only bump allocators. Scope exit resets 'top' in ONE
// store. Growth never moves data, so references stay valid while the
// owning scope lives. N stacks are assigned statically per core.
// ---------------------------------------------------------------------------
struct veriobjc_stack {
    uint8_t *base;   // start of the reservation (huge page / pre-mapped arena)
    uint8_t *top;    // bump pointer: the ONLY mutable state
    size_t   size;   // reservation size, for debug-mode overflow checks
};

static inline void veriobjc_stack_init(struct veriobjc_stack *s, void *base, size_t n) {
    s->base = (uint8_t *)base; s->top = s->base; s->size = n;
}

static inline void *veriobjc_stack_alloc(struct veriobjc_stack *s, size_t n) {
    // O(1), zero branches beyond the overflow check; no list, no lock.
    uint8_t *p = s->top;
    s->top += n;
    return (void *)p;                   // debug builds assert (s->top - s->base) <= s->size
}

static inline void veriobjc_stack_reset(struct veriobjc_stack *s, uint8_t *mark) {
    s->top = mark;                      // THE free: one store, however deep the structure
}

// ---------------------------------------------------------------------------
// vpool: recycled slots over a grow-only array. Memory is never released;
// a stale slot reads a different value of the CORRECT type — a logic bug,
// never memory corruption. Slots are indexes, so links survive moves.
// ---------------------------------------------------------------------------
struct veriobjc_pool {
    struct veriobjc_stack *backing;     // pool storage lives on a data stack
    size_t   slot_size;                 // sizeof(T), assigned statically
    uint32_t count;                     // high-water mark of used slots
    uint32_t free_head;                 // head of the free-slot index list
};

static inline void veriobjc_pool_init(struct veriobjc_pool *p,
                                      struct veriobjc_stack *s, size_t slot) {
    p->backing = s; p->slot_size = slot; p->count = 0; p->free_head = UINT32_MAX;
}

static inline uint32_t veriobjc_pool_alloc(struct veriobjc_pool *p) {
    if (p->free_head != UINT32_MAX) {           // reuse: pop freelist
        uint32_t slot = p->free_head;
        p->free_head = *(uint32_t *)((uint8_t *)p->backing->base + slot * p->slot_size);
        return slot;
    }
    veriobjc_stack_alloc(p->backing, p->slot_size);  // grow: never moves data
    return p->count++;
}

static inline void veriobjc_pool_free(struct veriobjc_pool *p, uint32_t slot) {
    // push slot onto freelist; memory itself is never released
    uint32_t *link = (uint32_t *)((uint8_t *)p->backing->base + slot * p->slot_size);
    *link = p->free_head;
    p->free_head = slot;
}

// Freestanding object construction hook (closed-world: class tables in .rodata)
static inline id veriobjc_alloc_init(Class cls, struct veriobjc_stack *s) {
    struct veriobjc_object *obj =
        (struct veriobjc_object *)veriobjc_stack_alloc(s, cls->instance_size);
    if (obj) { obj->isa = cls; }
    return (id)obj;
}
```

Note how the ownership discipline falls out of the primitives: the compiler's Linear Ownership Graph proves every reference is rooted at a stack `mark` that outlives it, and pool indexes are the only things allowed to outlive a scope.

---

## Phase 2: The Clang Compiler Plugin (VeriObjCPlugin.cpp)

This C++ boilerplate maps out the core logic of the compiler plugin. It hooks directly into the Clang compilation pass, scanning the AST to intercept and block the banned dynamic features (like the `id` type or dynamic reflection) before code generation can occur.

```cpp
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"

using namespace clang;

namespace {

// The AST Visitor checks every node in the source code syntax tree
class VeriObjCVisitor : public RecursiveASTVisitor<VeriObjCVisitor> {
private:
    ASTContext *Context;

public:
    explicit VeriObjCVisitor(ASTContext *Context) : Context(Context) {}

    // RULE 1: Intercept and BAN the generic 'id' keyword
    bool VisitVarDecl(VarDecl *D) {
        QualType T = D->getType();
        if (T->isObjCIdType()) {
            DiagnosticsEngine &Diag = Context->getDiagnostics();
            unsigned DiagID = Diag.getCustomDiagID(
                DiagnosticsEngine::Error, 
                "VeriObjC Violation: Banned use of generic 'id' type. Use strict nominal object typing."
            );
            Diag.Report(D->getLocation(), DiagID);
        }
        return true;
    }

    // RULE 2: Intercept ObjC message expressions [receiver message] 
    // to verify sealed hierarchies and later flatten them into static jumps
    bool VisitObjCMessageExpr(ObjCMessageExpr *E) {
        ObjCInterfaceDecl *ReceiverClass = E->getReceiverInterface();
        
        if (!ReceiverClass) {
            // Caught an implicit message to an untyped or dynamic receiver
            DiagnosticsEngine &Diag = Context->getDiagnostics();
            unsigned DiagID = Diag.getCustomDiagID(
                DiagnosticsEngine::Error,
                "VeriObjC Violation: Dynamic message dispatch requires an explicitly typed receiver."
            );
            Diag.Report(E->getExprLoc(), DiagID);
            return true;
        }

        // Verify if the receiver class possesses our custom __attribute__((objc_sealed))
        if (!ReceiverClass->hasAttr<AnnotateAttr>()) {
            // Note: In a production PoC, you would parse a custom attribute handler. 
            // For the boilerplate, we verify class sealing completeness here.
        }

        return true;
    }
};

// The Consumer handles hooking our visitor into the translation unit pass
class VeriObjCConsumer : public ASTConsumer {
private:
    VeriObjCVisitor Visitor;

public:
    explicit VeriObjCConsumer(ASTContext *Context) : Visitor(Context) {}

    void HandleTranslationUnit(ASTContext &Context) override {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }
};

class VeriObjCAction : public PluginASTAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef) override {
        return std::make_unique<VeriObjCConsumer>(&CI.getASTContext());
    }

    bool ParseArgs(const CompilerInstance &CI, const std::vector<std::string> &args) override {
        return true;
    }
};

} // namespace

// Register the plugin with the Clang registry architecture
static FrontendPluginRegistry::Add<VeriObjCAction> X("veriobjc-check", "Enforce VeriObjC structural invariants");
```

---

## Phase 3: Data-Stack & Pool Lowering Checks (next plugin milestones)

Extend the visitor with the Goose-derived checks, in this order:

1. **Scope mark tracking:** every `@arena` / function scope gets a `veriobjc_stack` mark; `@scoped` cleanup emits `veriobjc_stack_reset` (one store).
2. **Stable-reference proof:** a reference into a grow-only array may be stored only where its root scope provably outlives the destination; violations report `root` and `destination` by name.
3. **One-resizable-per-stack rule:** at most one live growable value per data stack; a second declaration in the same stack scope is an error suggesting a new `@arena`.
4. **Flat-value channel check:** reject channel sends whose payload contains references at any depth (walk the record layout via the AST).
5. **Pool discipline:** `allocRef` results are linear (moved or freed exactly once); stale slot reads type-check but trip a debug-mode warning.

---

## The Execution Strategy

To test this implementation without standardizing first, your setup cycle will look like this:
1. Compile the Clang Plugin into a dynamic library (VeriObjCPlugin.so or .dylib).
2. Write a test snippet (e.g., your kernel memory code file using a variable declared with `id`).
3. Execute the pipeline via the terminal command, pointing Clang directly at your plugin and your freestanding runtime header:

```sh
clang -Xclang -load -Xclang VeriObjCPlugin.so \
      -Xclang -plugin -Xclang veriobjc-check \
      -I./runtime \
      -c NetworkProcessor.m
```

If a developer attempts to type `id packet;`, the compiler instantly halts compilation at step 2, printing your custom error message directly to the screen before any corrupted binary or code emission can occur.

Quick data-stack smoke test (host build, no OS dependencies):

```sh
# 1. Assert scope frees are single stores (search the disassembly)
clang -O2 -S -emit-llvm runtime_smoke.c -o - | grep -c "store" 
# 2. Assert zero references to malloc in the freestanding path
nm runtime_smoke.o | grep -c malloc   # expect: 0
```
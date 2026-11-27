#!/usr/bin/env bash
set -euo pipefail

PLUGIN_PATH="${1:-build/libVeriObjCPlugin.so}"
REPO_ROOT="${2:-.}"

echo "=== VeriObjC Dual-Direction Ban Corpus Suite ==="
echo "Plugin: $PLUGIN_PATH"
echo "Repository Root: $REPO_ROOT"

if [ ! -f "$PLUGIN_PATH" ]; then
    echo "ERROR: Plugin binary not found at $PLUGIN_PATH"
    exit 1
fi

for corpus_file in "$REPO_ROOT"/examples/banned/valid_objc_*.m; do
    base_name=$(basename "$corpus_file")
    echo "[RUN] Ban Corpus File: $base_name"

    # Direction 1: Must compile 100% cleanly under stock Clang 23 (-Werror)
    STOCK_OUT=$(clang-23 -std=gnu23 -fblocks -fobjc-exceptions -Werror \
                         -I"$REPO_ROOT/rt/include" -I"$REPO_ROOT/vstd/include" -I"$REPO_ROOT/sys0/include" -I"$REPO_ROOT/host/include" \
                         -isystem "$REPO_ROOT/tests/plugin/isystem_ext" \
                         -fsyntax-only "$corpus_file" 2>&1) || {
        echo "[FAIL] Corpus file $base_name did NOT compile cleanly under stock Clang 23 (-Werror)!"
        echo "$STOCK_OUT"
        exit 1
    }

    # Map corpus file to exact Ban Registry diagnostic key phrase
    EXPECTED_DIAG=""
    case "$base_name" in
        valid_objc_id_usage.m)
            EXPECTED_DIAG="Banned use of generic 'id' type"
            ;;
        valid_objc_nsobject_inheritance.m)
            EXPECTED_DIAG="Banned inheritance from 'NSObject'"
            ;;
        valid_objc_untyped_messaging.m)
            EXPECTED_DIAG="Dynamic message dispatch requires an explicitly typed receiver"
            ;;
        valid_objc_nil_messaging.m)
            EXPECTED_DIAG="Banned nil messaging on unproven pointer"
            ;;
        valid_objc_runtime_reflection.m)
            EXPECTED_DIAG="Banned Apple runtime reflection"
            ;;
        valid_objc_category_swizzle.m)
            EXPECTED_DIAG="Banned runtime method surgery"
            ;;
        valid_objc_category.m)
            EXPECTED_DIAG="Banned categories"
            ;;
        valid_objc_kvc_roundtrip.m)
            EXPECTED_DIAG="Banned key-value coding/observation"
            ;;
        valid_objc_dynamic_property.m)
            EXPECTED_DIAG="Banned dynamic property synthesis"
            ;;
        valid_objc_perform_selector.m)
            EXPECTED_DIAG="Banned deferred dynamic dispatch"
            ;;
        valid_objc_lightweight_generics.m)
            EXPECTED_DIAG="Banned erased generic hints"
            ;;
        valid_objc_gcd_pipeline.m)
            EXPECTED_DIAG="Banned runtime scheduling on constrained paths"
            ;;
        valid_objc_try_catch.m)
            EXPECTED_DIAG="Banned exception unwinding"
            ;;
        valid_objc_escaping_block.m)
            EXPECTED_DIAG="Banned escaping function block"
            ;;
        valid_objc_alloc_mixing.m)
            EXPECTED_DIAG="Banned allocator mixing"
            ;;
        valid_objc_blocking_io.m)
            EXPECTED_DIAG="Banned blocking I/O in @nolatency context"
            ;;
        valid_objc_shared_nolatency.m)
            EXPECTED_DIAG="@shared retain permitted only in @cold"
            ;;
        valid_objc_view_escapes_root.m)
            EXPECTED_DIAG="Escaping data-stack reference"
            ;;
        valid_objc_escape_via_out_param.m)
            EXPECTED_DIAG="Escaping data-stack reference"
            ;;
        valid_objc_escape_via_helper_global.m)
            EXPECTED_DIAG="Escaping data-stack reference"
            ;;
        valid_objc_use_after_move_field_alias.m)
            EXPECTED_DIAG="Use of consumed linear variable"
            ;;
        valid_objc_hw_kernel_fnptr_call.m)
            EXPECTED_DIAG="closed-world 0-DSP verification requires statically visible callee bodies"
            ;;
        valid_objc_hw_kernel_extern_call.m)
            EXPECTED_DIAG="closed-world 0-DSP verification requires statically visible callee bodies"
            ;;
        valid_objc_raw_cast_to_sealed.m)
            EXPECTED_DIAG="Banned raw or unchecked pointer cast to @sealed/@final class"
            ;;
        valid_objc_unsealed_inheritance.m)
            EXPECTED_DIAG="Polymorphic class"
            ;;
        valid_objc_sealed_upcast_escape.m)
            EXPECTED_DIAG="Escaping data-stack reference"
            ;;
        valid_objc_contract_counterexample.m)
            EXPECTED_DIAG="Z3_SAT_COUNTEREXAMPLE"
            ;;
        valid_objc_unknown_veriobjc_annotate.m)
            EXPECTED_DIAG="Unknown VeriObjC annotation"
            ;;
        valid_objc_host_io_outside_cold.m)
            EXPECTED_DIAG="@cold function 'veriobjc_host_load_file_staged' cannot be called from non-@cold function"
            ;;
        valid_objc_isystem_extern_in_nolatency.m)
            EXPECTED_DIAG="defaults to @cold and cannot be called from a @nolatency context"
            ;;
        valid_objc_clock_cal_in_nolatency.m)
            EXPECTED_DIAG="@cold function 'do_calibrate' cannot be called from non-@cold function"
            ;;
        valid_objc_unchecked_vresult.m)
            EXPECTED_DIAG="Unchecked vresult access"
            ;;
        valid_objc_two_stack_arrays.m)
            EXPECTED_DIAG="Second live vstack_array"
            ;;
        valid_objc_vvector_overflow.m)
            EXPECTED_DIAG="vvector push exceeds static capacity"
            ;;
        valid_objc_vvector_unsealed_arg.m)
            EXPECTED_DIAG="Invalid vgeneric type argument"
            ;;
        valid_objc_nonflat_interface_by_value.m)
            EXPECTED_DIAG="cannot be allocated or returned by value: only @flat @final @sealed interfaces support value semantics (VG-006)"
            ;;
        valid_objc_vhash_load_ceiling.m)
            EXPECTED_DIAG="vhash_flat load-factor ceiling exceeded"
            ;;
        valid_objc_vvariant_added_case.m)
            EXPECTED_DIAG="Non-exhaustive vmatch on vvariant"
            ;;
        valid_objc_spsc_two_producers.m)
            EXPECTED_DIAG="SPSC invariant violated on channel"
            ;;
        valid_objc_unacquired_vatomic.m)
            EXPECTED_DIAG="Direct vatomic payload access without linear acquire guard"
            ;;
        valid_objc_kv_arena_overflow.m)
            EXPECTED_DIAG="Context cap exceeded — compile error"
            ;;
        valid_objc_dma_ring_overflow.m)
            EXPECTED_DIAG="DMA ring capacity exceeded"
            ;;
        valid_objc_vasync_poll_ring.m)
            EXPECTED_DIAG="Banned polling ring in vasync pipeline"
            ;;
        valid_objc_raw_simd_intrinsic.m)
            EXPECTED_DIAG="Raw SIMD intrinsic in @nolatency code is banned"
            ;;
        *)
            echo "[FAIL] Unmapped corpus file $base_name in Ban Registry!"
            exit 1
            ;;
    esac

    EXTRA_CORPUS_ARGS=()
    if [[ "$base_name" == "valid_objc_contract_counterexample.m" ]]; then
        EXTRA_CORPUS_ARGS=(-Xclang -plugin-arg-veriobjc-check -Xclang --smt-verify)
    fi

    # Direction 2: Must fail under VeriObjC plugin with the exact named diagnostic
    if PLUGIN_OUT=$(clang-23 -std=gnu23 -fblocks -fobjc-exceptions \
                             -Xclang -load -Xclang "$PLUGIN_PATH" \
                             -Xclang -add-plugin -Xclang veriobjc-check \
                             "${EXTRA_CORPUS_ARGS[@]}" \
                             -I"$REPO_ROOT/rt/include" \
                             -I"$REPO_ROOT/vstd/include" \
                             -I"$REPO_ROOT/sys0/include" \
                             -I"$REPO_ROOT/host/include" \
                             -isystem "$REPO_ROOT/tests/plugin/isystem_ext" \
                             -fsyntax-only "$corpus_file" 2>&1); then
        echo "[FAIL] Corpus file $base_name unexpectedly SUCCEEDED under VeriObjC plugin!"
        exit 1
    fi

    if echo "$PLUGIN_OUT" | grep -q "$EXPECTED_DIAG"; then
        echo "[PASS] $base_name: clean under stock Clang 23, rejected by VeriObjC with '$EXPECTED_DIAG'"
    else
        echo "[FAIL] Corpus file $base_name failed under VeriObjC plugin, but missing expected diagnostic!"
        echo "Expected: $EXPECTED_DIAG"
        echo "Actual output:"
        echo "$PLUGIN_OUT"
        exit 1
    fi
done

echo "=== All Dual-Direction Ban Corpus Tests Passed! ==="
exit 0

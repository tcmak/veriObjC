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
    STOCK_OUT=$(clang-23 -std=gnu23 -fblocks -fobjc-exceptions -Werror -fsyntax-only "$corpus_file" 2>&1) || {
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
        *)
            echo "[FAIL] Unmapped corpus file $base_name in Ban Registry!"
            exit 1
            ;;
    esac

    # Direction 2: Must fail under VeriObjC plugin with the exact named diagnostic
    if PLUGIN_OUT=$(clang-23 -std=gnu23 -fblocks -fobjc-exceptions \
                             -Xclang -load -Xclang "$PLUGIN_PATH" \
                             -Xclang -add-plugin -Xclang veriobjc-check \
                             -I"$REPO_ROOT/rt/include" \
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

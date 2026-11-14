#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/result.h> — Branch-Proven Error Unions (vresult<T, E>)
// Layer: vstd (depends on: rt)
// ---------------------------------------------------------------------------

#define veriobjc_vresult __attribute__((annotate("objc_vresult"), annotate("objc_flat")))

#define vresult_def(ResultName, ValueType, ErrType) \
    typedef struct veriobjc_vresult { \
        bool      is_ok; \
        ValueType value; \
        ErrType   err; \
    } ResultName

#define vresult_ok(ResultName, val) \
    ((ResultName){ .is_ok = true, .value = (val), .err = 0 })

#define vresult_err(ResultName, err_val) \
    ((ResultName){ .is_ok = false, .value = 0, .err = (err_val) })

static inline bool __vresult_check_ok(bool is_ok) {
    return is_ok;
}

#define vresult_is_ok(res_val) \
    __vresult_check_ok((res_val).is_ok)

#define vresult_unwrap_checked(res_val) \
    ((res_val).value)

#define vresult_unwrap_or(res_val, fallback_val) \
    (__vresult_check_ok((res_val).is_ok) ? (res_val).value : (fallback_val))

#define vresult_unwrap_err_or(res_val, fallback_err) \
    (!__vresult_check_ok((res_val).is_ok) ? (res_val).err : (fallback_err))

#ifdef __cplusplus
}
#endif

#ifndef VERIOBJC_HOST_SMT_BRIDGE_H
#define VERIOBJC_HOST_SMT_BRIDGE_H

#include "host/bench_harness.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef VERIOBJC_DEFAULT_Z3_PATH
#define VERIOBJC_DEFAULT_Z3_PATH "z3"
#endif

// ---------------------------------------------------------------------------
// 1. Non-SMT Affine Interval Endpoint Helper (Unit-Test Utility ONLY)
//    Never conflated with formal Z3 SMT discharge below.
// ---------------------------------------------------------------------------

enum rt_contract_endpoint_status {
    RT_CONTRACT_ENDPOINT_CHECK_PASSED   = 0,
    RT_CONTRACT_ENDPOINT_COUNTEREXAMPLE = 1,
    RT_CONTRACT_ENDPOINT_INVALID_ARG    = 2
};

struct rt_smt_contract_spec {
    const char *function_name;
    const char *var_name;
    int64_t pre_lower_bound;
    int64_t pre_upper_bound;
    int64_t transform_scale;
    int64_t transform_offset;
    int64_t post_lower_bound;
    int64_t post_upper_bound;
    const char *pre_smt_expr;
    const char *post_smt_expr;
};

struct rt_contract_endpoint_result {
    enum rt_contract_endpoint_status status;
    int64_t counterexample_input;
    int64_t counterexample_output;
    char smtlib2_script[512];
};

static inline enum rt_contract_endpoint_status rt_contract_eval_endpoints_stub(
    const struct rt_smt_contract_spec *spec,
    struct rt_contract_endpoint_result *out_res) {
    if (!spec || !out_res || !spec->function_name || !spec->var_name ||
        !spec->pre_smt_expr || !spec->post_smt_expr ||
        spec->pre_lower_bound > spec->pre_upper_bound) {
        return RT_CONTRACT_ENDPOINT_INVALID_ARG;
    }

    size_t pos = 0u;
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, "(set-logic QF_LIA)\n(declare-const ");
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, spec->var_name);
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, " Int)\n(declare-const ret Int)\n(assert ");
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, spec->pre_smt_expr);
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, ")\n(assert (not ");
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, spec->post_smt_expr);
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, "))\n; UNIMPLEMENTED_STUB: affine endpoint check only (no external SMT solver invoked)");
    out_res->smtlib2_script[pos] = '\0';

    int64_t out_at_low = spec->transform_scale * spec->pre_lower_bound + spec->transform_offset;
    int64_t out_at_high = spec->transform_scale * spec->pre_upper_bound + spec->transform_offset;

    int64_t min_in = (out_at_low <= out_at_high) ? spec->pre_lower_bound : spec->pre_upper_bound;
    int64_t min_out = (out_at_low <= out_at_high) ? out_at_low : out_at_high;
    int64_t max_in = (out_at_low <= out_at_high) ? spec->pre_upper_bound : spec->pre_lower_bound;
    int64_t max_out = (out_at_low <= out_at_high) ? out_at_high : out_at_low;

    if (min_out < spec->post_lower_bound) {
        out_res->status = RT_CONTRACT_ENDPOINT_COUNTEREXAMPLE;
        out_res->counterexample_input = min_in;
        out_res->counterexample_output = min_out;
        return RT_CONTRACT_ENDPOINT_COUNTEREXAMPLE;
    }
    if (max_out > spec->post_upper_bound) {
        out_res->status = RT_CONTRACT_ENDPOINT_COUNTEREXAMPLE;
        out_res->counterexample_input = max_in;
        out_res->counterexample_output = max_out;
        return RT_CONTRACT_ENDPOINT_COUNTEREXAMPLE;
    }

    out_res->status = RT_CONTRACT_ENDPOINT_CHECK_PASSED;
    out_res->counterexample_input = 0;
    out_res->counterexample_output = 0;
    return RT_CONTRACT_ENDPOINT_CHECK_PASSED;
}

// ---------------------------------------------------------------------------
// 2. Real Z3 >= 4.8.12 SMT-LIB2 Subprocess Bridge (Task 4.4)
//    - Safe POSIX pipe + fork + execvp (zero system() / popen() shell surface)
//    - Cross-version witness parser for Z3 4.8.12 through Z3 4.13+
// ---------------------------------------------------------------------------

enum rt_z3_status {
    Z3_UNSAT_PROVEN       = 0,
    Z3_SAT_COUNTEREXAMPLE = 1,
    Z3_UNKNOWN_TIMEOUT    = 2,
    Z3_SOLVER_UNAVAILABLE = 3,
    Z3_INVALID_QUERY      = 4
};

#define RT_Z3_MAX_WITNESSES 16u

struct rt_z3_witness_entry {
    char name[64];
    char raw_smt_val[64];
    int64_t signed_val;
    uint64_t unsigned_val;
    uint32_t bit_width;
    bool is_signed;
    bool has_numeric_val;
};

struct rt_z3_discharge_result {
    enum rt_z3_status status;
    char solver_version[128];
    char diagnostic_detail[512];
    char raw_stdout[2048];
    uint32_t witness_count;
    struct rt_z3_witness_entry witnesses[RT_Z3_MAX_WITNESSES];
};

static inline void rt_z3_copy_bounded(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0u) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0u;
    while (i + 1u < cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static inline uint64_t rt_z3_now_ms(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return 0ULL;
    }
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static inline bool rt_z3_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline bool rt_z3_is_delim(char c) {
    return c == '\0' || rt_z3_is_space(c) || c == '(' || c == ')' || c == ';';
}

static inline bool rt_z3_version_at_least(
    const char *version_output,
    uint32_t req_major,
    uint32_t req_minor,
    uint32_t req_patch
) {
    if (!version_output) return false;
    const char *tag = strstr(version_output, "Z3 version ");
    if (!tag) return false;
    const char *p = tag + 11;

    uint32_t parts[3] = {0u, 0u, 0u};
    for (int idx = 0; idx < 3; ++idx) {
        if (*p < '0' || *p > '9') return false;
        uint32_t val = 0u;
        while (*p >= '0' && *p <= '9') {
            val = val * 10u + (uint32_t)(*p - '0');
            ++p;
        }
        parts[idx] = val;
        if (idx < 2) {
            if (*p != '.') return false;
            ++p;
        }
    }

    if (parts[0] != req_major) return parts[0] > req_major;
    if (parts[1] != req_minor) return parts[1] > req_minor;
    return parts[2] >= req_patch;
}

// Execute `argv` safely via fork + execvp, feeding optional `stdin_data` and capturing stdout+stderr.
// Returns 0 on normal exit (setting *out_exit_code), 1 on wall-clock timeout, -1 on exec/system failure.
static inline int rt_z3_run_subprocess(
    char *const argv[],
    const char *stdin_data,
    uint32_t wall_timeout_ms,
    char *out_buf,
    size_t out_cap,
    int *out_exit_code
) {
    if (out_buf && out_cap > 0u) out_buf[0] = '\0';
    if (out_exit_code) *out_exit_code = -1;

    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    if (pipe(in_pipe) != 0) {
        return -1;
    }
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        (void)dup2(in_pipe[0], STDIN_FILENO);
        (void)dup2(out_pipe[1], STDOUT_FILENO);
        (void)dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    if (stdin_data && stdin_data[0] != '\0') {
        // Ignore SIGPIPE if child exits before reading full input
        size_t len = strlen(stdin_data);
        size_t off = 0u;
        while (off < len) {
            ssize_t nw = write(in_pipe[1], stdin_data + off, len - off);
            if (nw > 0) {
                off += (size_t)nw;
            } else if (nw < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
    }
    close(in_pipe[1]);

    int flags = fcntl(out_pipe[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    uint64_t start_ms = rt_z3_now_ms();
    size_t used = 0u;
    bool timed_out = false;
    bool eof_seen = false;

    while (!eof_seen) {
        uint64_t elapsed = rt_z3_now_ms() - start_ms;
        if (elapsed >= (uint64_t)wall_timeout_ms) {
            timed_out = true;
            break;
        }
        int remain_ms = (int)((uint64_t)wall_timeout_ms - elapsed);
        struct pollfd pfd;
        pfd.fd = out_pipe[0];
        pfd.events = POLLIN | POLLHUP;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, remain_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            timed_out = true;
            break;
        }

        char chunk[256];
        for (;;) {
            ssize_t nr = read(out_pipe[0], chunk, sizeof(chunk));
            if (nr > 0) {
                if (out_buf && out_cap > 1u && used + 1u < out_cap) {
                    size_t avail = out_cap - 1u - used;
                    size_t to_copy = ((size_t)nr < avail) ? (size_t)nr : avail;
                    memcpy(out_buf + used, chunk, to_copy);
                    used += to_copy;
                    out_buf[used] = '\0';
                }
            } else if (nr == 0) {
                eof_seen = true;
                break;
            } else {
                if (errno == EINTR) continue;
                break;
            }
        }
    }

    close(out_pipe[0]);

    if (timed_out) {
        (void)kill(pid, SIGKILL);
        int status = 0;
        (void)waitpid(pid, &status, 0);
        return 1;
    }

    int wstatus = 0;
    while (waitpid(pid, &wstatus, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(wstatus)) {
        int code = WEXITSTATUS(wstatus);
        if (out_exit_code) *out_exit_code = code;
        return (code == 127) ? -1 : 0;
    }
    return -1;
}

static inline bool rt_z3_probe_version(
    const char *z3_path,
    uint32_t timeout_ms,
    char *out_ver,
    size_t out_ver_cap,
    char *out_err,
    size_t out_err_cap
) {
    const char *resolved = (z3_path && z3_path[0] != '\0') ? z3_path : VERIOBJC_DEFAULT_Z3_PATH;
    char prog_buf[512];
    char flag_buf[16];
    rt_z3_copy_bounded(prog_buf, sizeof(prog_buf), resolved);
    rt_z3_copy_bounded(flag_buf, sizeof(flag_buf), "--version");
    char *argv[3] = {prog_buf, flag_buf, NULL};

    char ver_out[256];
    int exit_code = -1;
    uint32_t probe_budget = (timeout_ms > 0u && timeout_ms < 2000u) ? 2000u : (timeout_ms ? timeout_ms : 2000u);
    int rc = rt_z3_run_subprocess(argv, NULL, probe_budget, ver_out, sizeof(ver_out), &exit_code);

    // Strip trailing newline
    for (size_t i = 0u; ver_out[i] != '\0'; ++i) {
        if (ver_out[i] == '\r' || ver_out[i] == '\n') {
            ver_out[i] = '\0';
            break;
        }
    }

    if (rc != 0 || exit_code != 0 || !rt_z3_version_at_least(ver_out, 4u, 8u, 12u)) {
        if (out_err && out_err_cap > 0u) {
            size_t pos = 0u;
            pos = rt_bench_append_str(out_err, pos, "Z3 binary '");
            pos = rt_bench_append_str(out_err, pos, resolved);
            pos = rt_bench_append_str(out_err, pos, "' unavailable or older than required Z3 >= 4.8.12");
            if (ver_out[0] != '\0') {
                pos = rt_bench_append_str(out_err, pos, " (got: ");
                pos = rt_bench_append_str(out_err, pos, ver_out);
                pos = rt_bench_append_str(out_err, pos, ")");
            }
            if (pos < out_err_cap) out_err[pos] = '\0';
            else out_err[out_err_cap - 1u] = '\0';
        }
        if (out_ver && out_ver_cap > 0u) out_ver[0] = '\0';
        return false;
    }

    rt_z3_copy_bounded(out_ver, out_ver_cap, ver_out);
    if (out_err && out_err_cap > 0u) out_err[0] = '\0';
    return true;
}

static inline const char *rt_z3_skip_ws_and_comments(const char *p) {
    while (p && *p != '\0') {
        if (rt_z3_is_space(*p)) {
            ++p;
        } else if (*p == ';') {
            while (*p != '\0' && *p != '\n') ++p;
        } else {
            break;
        }
    }
    return p;
}

static inline const char *rt_z3_skip_sexpr(const char *p) {
    p = rt_z3_skip_ws_and_comments(p);
    if (!p || *p == '\0') return p;
    if (*p != '(') {
        while (*p != '\0' && !rt_z3_is_delim(*p)) ++p;
        return p;
    }
    int depth = 0;
    while (*p != '\0') {
        if (*p == '(') ++depth;
        else if (*p == ')') {
            --depth;
            if (depth <= 0) {
                return p + 1;
            }
        }
        ++p;
    }
    return p;
}

static inline void rt_z3_decode_value_token(const char *raw_tok, uint32_t hint_bits, struct rt_z3_witness_entry *entry) {
    rt_z3_copy_bounded(entry->raw_smt_val, sizeof(entry->raw_smt_val), raw_tok);
    entry->signed_val = 0;
    entry->unsigned_val = 0ULL;
    entry->bit_width = hint_bits;
    entry->is_signed = false;
    entry->has_numeric_val = false;

    if (!raw_tok || raw_tok[0] == '\0') return;

    if (strcmp(raw_tok, "true") == 0) {
        entry->unsigned_val = 1ULL;
        entry->signed_val = 1;
        entry->bit_width = 1u;
        entry->has_numeric_val = true;
        return;
    }
    if (strcmp(raw_tok, "false") == 0) {
        entry->unsigned_val = 0ULL;
        entry->signed_val = 0;
        entry->bit_width = 1u;
        entry->has_numeric_val = true;
        return;
    }
    if (raw_tok[0] == '#' && raw_tok[1] == 'x') {
        uint64_t v = 0ULL;
        uint32_t digits = 0u;
        const char *q = raw_tok + 2;
        while (*q != '\0') {
            uint64_t nib = 0ULL;
            if (*q >= '0' && *q <= '9') nib = (uint64_t)(*q - '0');
            else if (*q >= 'a' && *q <= 'f') nib = (uint64_t)(*q - 'a' + 10);
            else if (*q >= 'A' && *q <= 'F') nib = (uint64_t)(*q - 'A' + 10);
            else break;
            v = (v << 4u) | nib;
            ++digits;
            ++q;
        }
        if (digits > 0u) {
            uint32_t w = (hint_bits > 0u) ? hint_bits : (digits * 4u);
            entry->bit_width = w;
            entry->unsigned_val = v;
            if (w > 0u && w < 64u && ((v >> (w - 1u)) & 1ULL) != 0ULL) {
                uint64_t mask = ~((1ULL << w) - 1ULL);
                entry->signed_val = (int64_t)(v | mask);
            } else {
                entry->signed_val = (int64_t)v;
            }
            entry->has_numeric_val = true;
        }
        return;
    }
    if (raw_tok[0] == '#' && raw_tok[1] == 'b') {
        uint64_t v = 0ULL;
        uint32_t bits = 0u;
        const char *q = raw_tok + 2;
        while (*q == '0' || *q == '1') {
            v = (v << 1u) | (uint64_t)(*q - '0');
            ++bits;
            ++q;
        }
        if (bits > 0u) {
            uint32_t w = (hint_bits > 0u) ? hint_bits : bits;
            entry->bit_width = w;
            entry->unsigned_val = v;
            if (w > 0u && w < 64u && ((v >> (w - 1u)) & 1ULL) != 0ULL) {
                uint64_t mask = ~((1ULL << w) - 1ULL);
                entry->signed_val = (int64_t)(v | mask);
            } else {
                entry->signed_val = (int64_t)v;
            }
            entry->has_numeric_val = true;
        }
        return;
    }

    // Signed or unsigned decimal (including "(- 5)" normalized to "-5")
    bool neg = false;
    const char *q = raw_tok;
    if (*q == '(') {
        ++q;
        q = rt_z3_skip_ws_and_comments(q);
        if (*q == '-') {
            neg = true;
            ++q;
            q = rt_z3_skip_ws_and_comments(q);
        }
    } else if (*q == '-') {
        neg = true;
        ++q;
    }

    if (*q >= '0' && *q <= '9') {
        uint64_t mag = 0ULL;
        while (*q >= '0' && *q <= '9') {
            mag = mag * 10ULL + (uint64_t)(*q - '0');
            ++q;
        }
        entry->has_numeric_val = true;
        entry->is_signed = neg;
        if (neg) {
            entry->signed_val = -(int64_t)mag;
            entry->unsigned_val = (uint64_t)entry->signed_val;
        } else {
            entry->signed_val = (int64_t)mag;
            entry->unsigned_val = mag;
        }
    }
}

static inline const char *rt_z3_read_value_expr(const char *p, char *out_tok, size_t out_cap) {
    p = rt_z3_skip_ws_and_comments(p);
    if (!p || *p == '\0') {
        if (out_cap > 0u) out_tok[0] = '\0';
        return p;
    }
    const char *start = p;
    const char *end = rt_z3_skip_sexpr(p);
    size_t len = (size_t)(end - start);
    if (len >= out_cap) len = out_cap - 1u;
    memcpy(out_tok, start, len);
    out_tok[len] = '\0';
    return end;
}

static inline void rt_z3_parse_solver_output(const char *raw_out, struct rt_z3_discharge_result *out_res) {
    if (!out_res) return;
    out_res->witness_count = 0u;
    if (!raw_out) {
        out_res->status = Z3_UNKNOWN_TIMEOUT;
        return;
    }

    const char *p = rt_z3_skip_ws_and_comments(raw_out);
    if (!p || *p == '\0') {
        out_res->status = Z3_UNKNOWN_TIMEOUT;
        return;
    }

    // Read leading status atom ("unsat", "sat", "unknown", "timeout", or "(error ...)")
    if (*p == '(') {
        out_res->status = Z3_INVALID_QUERY;
        rt_z3_copy_bounded(out_res->diagnostic_detail, sizeof(out_res->diagnostic_detail), p);
        return;
    }

    char status_word[32];
    size_t sw = 0u;
    while (*p != '\0' && !rt_z3_is_delim(*p) && sw + 1u < sizeof(status_word)) {
        status_word[sw++] = *p++;
    }
    status_word[sw] = '\0';

    if (strcmp(status_word, "unsat") == 0) {
        out_res->status = Z3_UNSAT_PROVEN;
        return;
    }
    if (strcmp(status_word, "unknown") == 0 || strcmp(status_word, "timeout") == 0) {
        out_res->status = Z3_UNKNOWN_TIMEOUT;
        return;
    }
    if (strcmp(status_word, "sat") != 0) {
        out_res->status = Z3_UNKNOWN_TIMEOUT;
        return;
    }

    out_res->status = Z3_SAT_COUNTEREXAMPLE;

    // Scan remaining output for both:
    //   1) `(define-fun <name> () <sort> <value>)` (Z3 4.8.12 `(model ...)` AND Z3 >= 4.12 `(...)`)
    //   2) `(<name> <value>)` inside `(get-value (...))` S-expression lists
    while (*p != '\0' && out_res->witness_count < RT_Z3_MAX_WITNESSES) {
        p = rt_z3_skip_ws_and_comments(p);
        if (*p == '\0') break;
        if (*p != '(') {
            ++p;
            continue;
        }
        ++p; // consume '('
        p = rt_z3_skip_ws_and_comments(p);
        if (*p == '(' || *p == ')' || *p == '\0') {
            continue;
        }

        char head[64];
        size_t hi = 0u;
        while (*p != '\0' && !rt_z3_is_delim(*p) && hi + 1u < sizeof(head)) {
            head[hi++] = *p++;
        }
        head[hi] = '\0';

        if (strcmp(head, "model") == 0) {
            continue;
        }
        if (strcmp(head, "error") == 0 || strcmp(head, "_") == 0 || strcmp(head, "-") == 0) {
            while (*p != '\0' && *p != ')') ++p;
            if (*p == ')') ++p;
            continue;
        }

        if (strcmp(head, "define-fun") == 0) {
            p = rt_z3_skip_ws_and_comments(p);
            char fn_name[64];
            size_t ni = 0u;
            while (*p != '\0' && !rt_z3_is_delim(*p) && ni + 1u < sizeof(fn_name)) {
                fn_name[ni++] = *p++;
            }
            fn_name[ni] = '\0';
            p = rt_z3_skip_sexpr(p); // skip parameter list ()

            // Inspect sort for optional (_ BitVec W)
            p = rt_z3_skip_ws_and_comments(p);
            const char *sort_start = p;
            const char *sort_end = rt_z3_skip_sexpr(p);
            uint32_t sort_bits = 0u;
            const char *bv_kw = strstr(sort_start, "BitVec ");
            if (bv_kw && bv_kw < sort_end) {
                bv_kw += 7;
                while (bv_kw < sort_end && (*bv_kw >= '0' && *bv_kw <= '9')) {
                    sort_bits = sort_bits * 10u + (uint32_t)(*bv_kw - '0');
                    ++bv_kw;
                }
            }
            p = sort_end;

            char val_tok[64];
            p = rt_z3_read_value_expr(p, val_tok, sizeof(val_tok));
            if (fn_name[0] != '\0' && val_tok[0] != '\0') {
                struct rt_z3_witness_entry *w = &out_res->witnesses[out_res->witness_count++];
                rt_z3_copy_bounded(w->name, sizeof(w->name), fn_name);
                rt_z3_decode_value_token(val_tok, sort_bits, w);
            }
            p = rt_z3_skip_ws_and_comments(p);
            if (*p == ')') ++p;
            continue;
        }

        // Otherwise `(get-value ...)` pair: `(<var_name> <val_expr>)`
        char val_tok[64];
        p = rt_z3_read_value_expr(p, val_tok, sizeof(val_tok));
        p = rt_z3_skip_ws_and_comments(p);
        if (*p == ')' && head[0] != '\0' && val_tok[0] != '\0') {
            struct rt_z3_witness_entry *w = &out_res->witnesses[out_res->witness_count++];
            rt_z3_copy_bounded(w->name, sizeof(w->name), head);
            rt_z3_decode_value_token(val_tok, 0u, w);
            ++p;
        }
    }
}

static inline enum rt_z3_status rt_z3_discharge_smtlib2(
    const char *z3_path,
    uint32_t timeout_ms,
    const char *smtlib2_query,
    struct rt_z3_discharge_result *out_res
) {
    if (!out_res) {
        return Z3_INVALID_QUERY;
    }
    memset(out_res, 0, sizeof(*out_res));
    if (!smtlib2_query || smtlib2_query[0] == '\0') {
        out_res->status = Z3_INVALID_QUERY;
        rt_z3_copy_bounded(out_res->diagnostic_detail, sizeof(out_res->diagnostic_detail), "empty or NULL SMT-LIB2 query");
        return Z3_INVALID_QUERY;
    }

    const char *resolved_path = (z3_path && z3_path[0] != '\0') ? z3_path : VERIOBJC_DEFAULT_Z3_PATH;
    uint32_t effective_ms = (timeout_ms == 0u) ? 2000u : timeout_ms;

    if (!rt_z3_probe_version(
            resolved_path,
            effective_ms,
            out_res->solver_version,
            sizeof(out_res->solver_version),
            out_res->diagnostic_detail,
            sizeof(out_res->diagnostic_detail))) {
        out_res->status = Z3_SOLVER_UNAVAILABLE;
        return Z3_SOLVER_UNAVAILABLE;
    }

    char prog_buf[512];
    char smt2_flag[16];
    char in_flag[16];
    char timeout_flag[32];
    rt_z3_copy_bounded(prog_buf, sizeof(prog_buf), resolved_path);
    rt_z3_copy_bounded(smt2_flag, sizeof(smt2_flag), "-smt2");
    rt_z3_copy_bounded(in_flag, sizeof(in_flag), "-in");
    size_t tpos = 0u;
    tpos = rt_bench_append_str(timeout_flag, tpos, "-t:");
    tpos = rt_bench_append_u64(timeout_flag, tpos, (uint64_t)effective_ms);
    timeout_flag[tpos] = '\0';

    char *argv[5] = {prog_buf, smt2_flag, in_flag, timeout_flag, NULL};
    int exit_code = -1;
    uint32_t wall_ms = effective_ms + 500u;
    int rc = rt_z3_run_subprocess(
        argv, smtlib2_query, wall_ms, out_res->raw_stdout, sizeof(out_res->raw_stdout), &exit_code
    );

    if (rc == 1) {
        out_res->status = Z3_UNKNOWN_TIMEOUT;
        rt_z3_copy_bounded(out_res->diagnostic_detail, sizeof(out_res->diagnostic_detail), "Z3 wall-clock timeout exceeded");
        return Z3_UNKNOWN_TIMEOUT;
    }
    if (rc < 0) {
        out_res->status = Z3_SOLVER_UNAVAILABLE;
        rt_z3_copy_bounded(out_res->diagnostic_detail, sizeof(out_res->diagnostic_detail), "failed to execute z3 subprocess");
        return Z3_SOLVER_UNAVAILABLE;
    }

    rt_z3_parse_solver_output(out_res->raw_stdout, out_res);
    return out_res->status;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_HOST_SMT_BRIDGE_H */

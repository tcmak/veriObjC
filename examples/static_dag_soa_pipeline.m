// ===========================================================================
// VeriObjC Example 9: Effect System, FFI @export, @reflect/SoA, vvariant/vmatch,
//                     vtrait, Static Task DAGs (vasync), and In-Place Construction
//
// Demonstrates Section 2.8 capabilities:
//   1. Effect Type System (@nolatency, @pure, @cold, @fence, @io)
//   2. FFI @export boundary checking + @pre/@post header contract annotations
//   3. Compile-time @reflect() field-count/sizeof/offset + vsoa_def3 SoA layout
//   4. Exhaustive vmatch on vvariant + vtrait closed table folded via vconstexpr
//   5. Monomorphization deduplication & instantiation budget tracking
//   6. Static core/NUMA topology, fixed Task DAG WCET verification, & stack vasync
//   7. Copy-free destination-slot threading via veriobjc_push_inplace
// ===========================================================================

#include "veriobjc_runtime.h"

// 1. Static Core & NUMA Topology Declarations
veriobjc_core_topology_def(kTelemetryRxCore, 1, 0);
veriobjc_core_topology_def(kTelemetryComputeCore, 2, 0);

// 2. Compile-Time @reflect Target & Cache-Aligned Struct-of-Arrays (objc_soa)
struct TelemetryHeader {
    uint64_t tsc_cycles;
    uint32_t sensor_id;
    uint16_t flags;
    uint16_t payload_bytes;
};

vsoa_def3(SensorParticleSoA, 128, int32_t, pos_x, int32_t, pos_y, uint32_t, energy);

// 3. Algebraic Data Type (vvariant) & Closed Compile-Time Trait Table (vtrait)
enum TelemetryEventTag {
    TelemetryEvent_Sample = 0,
    TelemetryEvent_Calibration = 1,
    TelemetryEvent_Fault = 2
};

struct veriobjc_vvariant TelemetryEvent {
    enum TelemetryEventTag tag;
    int32_t delta_x;
    int32_t delta_y;
    uint32_t code;
};

vtrait uint32_t sensor_scale_shift(uint32_t sensor_kind) {
    if (sensor_kind == 1u) {
        return 2u; // x4 shift
    }
    if (sensor_kind == 2u) {
        return 3u; // x8 shift
    }
    return 0u;
}

// 4. Built-in-Place Construction Target (objc_inplace_only)
struct veriobjc_inplace_only AuditRecordSlot {
    uint64_t dag_wcet_cycles;
    uint32_t reflected_fields;
    uint32_t final_energy;
};

static inline void init_audit_slot(
    struct AuditRecordSlot *dst,
    uint64_t wcet,
    uint32_t fields,
    uint32_t energy
) {
    if (!dst) {
        return;
    }
    dst->dag_wcet_cycles = wcet;
    dst->reflected_fields = fields;
    dst->final_energy = energy;
}

// 5. Effect-Annotated Primitives (@pure, @fence, @io, @cold)
veriobjc_pure
static inline uint32_t fold_energy(int32_t x, int32_t y, uint32_t shift) {
    uint32_t mag = (uint32_t)((x >= 0 ? x : -x) + (y >= 0 ? y : -y));
    return mag << shift;
}

veriobjc_effect_fence
static inline void publish_hw_doorbell(uint32_t *doorbell_mmio, uint32_t val) {
    if (doorbell_mmio) {
        *doorbell_mmio = val;
    }
}

veriobjc_effect_io
static inline void emit_cold_uart_trace(uint32_t fault_code) {
    (void)fault_code;
}

veriobjc_cold
void handle_cold_sensor_fault(uint32_t fault_code) {
    emit_cold_uart_trace(fault_code);
}

// 6. Hot @nolatency Pipeline Tick (DAG + vasync + SoA + vmatch + In-Place Push)
veriobjc_nolatency
uint32_t execute_sensor_dag_tick(
    struct SensorParticleSoA *soa,
    uint32_t idx,
    struct TelemetryEvent ev,
    struct AuditRecordSlot *audit_ring,
    uint32_t *audit_count,
    uint32_t audit_cap,
    uint32_t *doorbell_mmio
) {
    // Register static task DAG edges & fold critical-path WCET at compile time
    veriobjc_dag_edge("dma_ingest", "soa_transform", 10);
    veriobjc_dag_edge("soa_transform", "ring_commit", 14);
    uint32_t dag_wcet = veriobjc_dag_critical_path_wcet();

    // Register deduplicated generic instantiation within package budget
    vmono_instantiate("telemetry_pkg", "SoAStreamStep<int32_t>", 96, 16);
    vmono_instantiate("telemetry_pkg", "SoAStreamStep<int32_t>", 96, 16);

    // Compile-time reflection (@reflect) queries folded to constants
    uint32_t hdr_fields = veriobjc_reflect_field_count(struct TelemetryHeader);
    uint32_t hdr_size = veriobjc_reflect_sizeof(struct TelemetryHeader);
    uint32_t flags_off = (uint32_t)veriobjc_reflect_offset(struct TelemetryHeader, flags);

    // Closed vtrait lookup folded by vconstexpr interpreter
    uint32_t shift = sensor_scale_shift(1u);

    // Exhaustive vmatch over TelemetryEvent vvariant
    uint32_t computed_energy = 0u;
    vmatch (ev) {
        case TelemetryEvent_Sample:
            if (soa && idx < 128u) {
                soa->pos_x[idx] += ev.delta_x;
                soa->pos_y[idx] += ev.delta_y;
                computed_energy = fold_energy(soa->pos_x[idx], soa->pos_y[idx], shift);
                soa->energy[idx] = computed_energy;
            }
            break;
        case TelemetryEvent_Calibration:
            computed_energy = hdr_fields + hdr_size + flags_off;
            break;
        case TelemetryEvent_Fault:
            computed_energy = ev.code;
            break;
    }

    // Stack-allocated vasync future woken by hardware interrupt hook
    struct veriobjc_vasync_frame irq_frame;
    veriobjc_vasync_init(&irq_frame, kTelemetryRxCore.core_id);
    veriobjc_vasync_wake_irq(&irq_frame, computed_energy + kTelemetryComputeCore.core_id);

    if (veriobjc_vasync_is_ready(&irq_frame) && audit_ring && audit_count) {
        veriobjc_push_inplace(
            audit_ring,
            *audit_count,
            audit_cap,
            init_audit_slot,
            (uint64_t)dag_wcet,
            hdr_fields,
            irq_frame.result_code
        );
    }

    publish_hw_doorbell(doorbell_mmio, irq_frame.result_code);
    return irq_frame.result_code;
}

// 7. Pinned FFI @export Boundary Function with Contract Pre/Post Annotations
typedef struct veriobjc_packed {
    uint32_t sensor_id;
    int32_t  delta_x;
    int32_t  delta_y;
} ExportedSensorFrame;

vstatic_assert(sizeof(ExportedSensorFrame) == 12, "ExportedSensorFrame must be 12 bytes packed");

veriobjc_export
veriobjc_pre("frame.sensor_id < 128")
veriobjc_post("return.generation != 0")
struct veriobjc_handle export_submit_sensor_frame(
    ExportedSensorFrame frame,
    struct veriobjc_handle pool_handle
) {
    (void)frame;
    return pool_handle;
}

// One counter that moves on every output-merger state change EDVR can see.
//
// The forward-light watch (flat_forward_light_runtime.inl) asked the immediate context, on every watched draw, for
// the bound render targets, all 64 UAV slots, the blend state, the depth view and the depth-stencil state: about 350
// target reads and 520 depth reads a frame in a hangar (the flat cpu "copy checks" line, 2026-11-11), each one a
// call through the driver with a reference taken and dropped per slot. Between two draws those answers change only
// when something sets output-merger state, so the watch keeps its last answer and asks again only after a set.
//
// It moves at the top of every hooked setter (OMSetRenderTargets, OMSetRenderTargetsAndUnorderedAccessViews,
// OMSetBlendState, OMSetDepthStencilState), on ClearState, command lists and context-state swaps, and in EDVR's own
// raw setters that bypass the hooks (vscreen.cpp). It moves for every context and for EDVR's internal binds too:
// a spurious move costs one ask, a missed one would be a stale answer. The binding shadow's own render-target and
// depth generations (frame boundary, forget-all) are part of the same key at the reader, and the reader also checks
// a share of its answers against the context (flat_query_cut.h): a disagreement sends it back to asking for the
// session, with one log line.
#pragma once

#include <atomic>
#include <cstdint>

namespace edvr {

inline std::atomic<uint32_t> g_flatOmSets{1};
// Whether every setter above is hooked. OMSetDepthStencilState and SwapDeviceContextState sit past ClearState and exist
// only on a Context1 vtable (vscreen.cpp's issueSlot): without them a set could go unseen, and the reader asks every time.
inline std::atomic<bool> g_flatOmHooksComplete{false};

// What a set can change, for readers that hold only part of the output-merger state (2026-11-14). The forward-light
// depth answer (the bound depth view and depth-stencil state) was asked again after every set, and Elite sets blend or
// depth-stencil state before nearly every draw: about 1360 of 1720 answers a frame were asks at a busy station. A blend
// set cannot change the depth answer, and a depth-stencil set on the owner context names its state (recorded below).
// The no-argument form moves every counter: ClearState, command lists, context swaps, and any caller not classified.
enum FlatOmSetKind : unsigned {
    kFlatOmBlend = 1u,         // OMSetBlendState
    kFlatOmDepthState = 2u,    // OMSetDepthStencilState
    kFlatOmTargets = 4u,       // OMSetRenderTargets and OMSetRenderTargetsAndUnorderedAccessViews: the views, depth included
    kFlatOmAll = 7u,
};
inline std::atomic<uint32_t> g_flatOmDepthStateSets{1};
inline std::atomic<uint32_t> g_flatOmTargetSets{1};

// Moves the all-sets counter and the counters of what the set can change. Returns the depth-state counter's new value
// when the set moved it (the recorder below ties its value to it), else 0.
inline uint32_t flatOmSetNote(unsigned kind = kFlatOmAll) noexcept {
    g_flatOmSets.fetch_add(1, std::memory_order_relaxed);
    if (kind & kFlatOmTargets) g_flatOmTargetSets.fetch_add(1, std::memory_order_relaxed);
    return (kind & kFlatOmDepthState) ? g_flatOmDepthStateSets.fetch_add(1, std::memory_order_relaxed) + 1u : 0u;
}
// Who made the latest target set (a diagnostic for the depth answer's asks, flat_cpu.h's depth-ask counts): the game
// through the hook, EDVR's own code through the hook (its return address is in EDVR's image), EDVR's raw setter
// (vScreenSetRenderTargetsRaw), or another context. Only the latest; several sets between two asks count as the last.
enum FlatOmTargetSource : uint32_t { kFlatOmTargetGame = 0, kFlatOmTargetEdvr, kFlatOmTargetRaw, kFlatOmTargetForeign, kFlatOmTargetSources };
inline std::atomic<uint32_t> g_flatOmTargetSource{kFlatOmTargetGame};
inline void flatOmTargetSourceNote(uint32_t source) noexcept { g_flatOmTargetSource.store(source, std::memory_order_relaxed); }
inline uint32_t flatOmTargetSourceLast() noexcept {
    const uint32_t s = g_flatOmTargetSource.load(std::memory_order_relaxed);
    return s < kFlatOmTargetSources ? s : kFlatOmTargetGame;
}
inline uint32_t flatOmSetGeneration() noexcept { return g_flatOmSets.load(std::memory_order_relaxed); }
inline uint32_t flatOmTargetGeneration() noexcept { return g_flatOmTargetSets.load(std::memory_order_relaxed); }
inline uint32_t flatOmDepthStateGeneration() noexcept { return g_flatOmDepthStateSets.load(std::memory_order_relaxed); }
inline void flatOmSetHooksComplete(bool complete) noexcept { g_flatOmHooksComplete.store(complete, std::memory_order_relaxed); }
inline bool flatOmSetTracked() noexcept { return g_flatOmHooksComplete.load(std::memory_order_relaxed); }

// The depth-stencil state the owner context's hooked OMSetDepthStencilState bound, tied to the depth-state counter value
// that set produced. It names the bound state only while that counter has not moved since: any other depth-state set (a
// foreign context's, EDVR's raw setter, ClearState, a command list, a context swap) moves it, and the reader asks the
// context instead. Identities only: the state is alive while bound, and the reader takes its own reference. Written and
// read on the owner context's thread; the generation is cleared first and stored last so a torn record is never used.
struct FlatOmDepthStateRecord {
    std::atomic<const void*> context{nullptr};
    std::atomic<void*> state{nullptr};
    std::atomic<uint32_t> generation{0};
};
inline FlatOmDepthStateRecord g_flatOmDepthStateRecord;
// After the hooked setter's flatOmSetNote(kFlatOmDepthState): `generation` is what that call returned.
inline void flatOmDepthStateRecord(const void* context, void* state, uint32_t generation) noexcept {
    g_flatOmDepthStateRecord.generation.store(0, std::memory_order_release);
    g_flatOmDepthStateRecord.context.store(context, std::memory_order_relaxed);
    g_flatOmDepthStateRecord.state.store(state, std::memory_order_relaxed);
    g_flatOmDepthStateRecord.generation.store(generation, std::memory_order_release);
}
// True, with the state, when the record names what `context` has bound now.
inline bool flatOmDepthStateRecorded(const void* context, void*& state) noexcept {
    const uint32_t generation = g_flatOmDepthStateRecord.generation.load(std::memory_order_acquire);
    if (!generation || generation != flatOmDepthStateGeneration()) return false;
    if (g_flatOmDepthStateRecord.context.load(std::memory_order_relaxed) != context) return false;
    state = g_flatOmDepthStateRecord.state.load(std::memory_order_relaxed);
    return g_flatOmDepthStateRecord.generation.load(std::memory_order_acquire) == generation;
}

}  // namespace edvr

// What the game is told about graphics memory (diagnostic only; 2026-11-21, the memory growth sitting still in space).
//
// The lead: Elite sizes its graphics caches to a share of the memory DXGI reports, unaware that on a Mac with unified
// memory that memory is the whole machine's. On a 36 GB M4 Max, DXMT reports DedicatedVideoMemory 14376 MiB and a
// QueryVideoMemoryInfo budget of 28753 MB for both segment groups. Which of these does the game read, how often, and
// what does it get back?
//
// The adapter's methods that carry those figures are hooked in place on the adapter the game's device sits on (the
// class's shared table, so every adapter object of that class is seen): GetDesc (slot 8), GetDesc1 (10), GetDesc2 (11),
// QueryVideoMemoryInfo (14), SetVideoMemoryReservation (15), RegisterVideoMemoryBudgetChangeNotificationEvent (16) and
// GetDesc3 (18) where the table has them. Each call is forwarded unchanged; nothing the game sees is altered. Calls are
// counted by caller: the game's executable, ED-UP's own module, or any other module. The first call per method per
// caller is logged with what it returned and, for the game, the return address as an offset into the executable; a
// summary line follows every 60 s while anything is counted. Calls made before the device existed (the game's adapter
// enumeration at startup) are not seen; the install line says so.
#pragma once

#include <d3d11.h>
#include <dxgi1_6.h>
#include <intrin.h>
#include <windows.h>

#include <atomic>
#include <cstdint>

#include "../common/log.h"
#include "../common/vtable_hook.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace edvr::vram_budget_probe {

enum Method : int { kGetDesc, kGetDesc1, kGetDesc2, kQvmi, kReservation, kBudgetEvent, kGetDesc3, kMethods };
enum Caller : int { kGame, kEdup, kOther, kCallers };
inline const char* kMethodName[kMethods] = {"GetDesc", "GetDesc1", "GetDesc2", "QueryVideoMemoryInfo",
                                            "SetVideoMemoryReservation", "RegisterVideoMemoryBudgetChangeNotificationEvent",
                                            "GetDesc3"};
inline const char* kCallerName[kCallers] = {"the game", "ED-UP", "another module"};
inline constexpr size_t kSlot[kMethods] = {8, 10, 11, 14, 15, 16, 18};

inline VTableHook g_hook;
inline void* g_real[kMethods] = {};
inline std::atomic<uint64_t> g_count[kMethods][kCallers] = {};
inline std::atomic<bool> g_noted[kMethods][kCallers] = {};
inline std::atomic<uint64_t> g_lastBudget{0}, g_lastUsage{0}, g_lastReserve{0};
inline uint64_t g_nextSummary = 0;

inline Caller callerOf(void* ret, uintptr_t* offset) {
    HMODULE m = nullptr;
    *offset = 0;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(ret), &m) || !m)
        return kOther;
    if (m == reinterpret_cast<HMODULE>(&__ImageBase)) return kEdup;
    if (m == GetModuleHandleW(nullptr)) {
        *offset = reinterpret_cast<uintptr_t>(ret) - reinterpret_cast<uintptr_t>(m);
        return kGame;
    }
    return kOther;
}

// Counts the call; true the first time this method is called by this caller (the caller logs it then).
inline bool note(Method m, void* ret, Caller* who, uintptr_t* offset) {
    *who = callerOf(ret, offset);
    g_count[m][*who].fetch_add(1, std::memory_order_relaxed);
    return !g_noted[m][*who].exchange(true, std::memory_order_relaxed);
}

inline void logDesc(Method m, Caller who, uintptr_t offset, HRESULT hr, const wchar_t* name, SIZE_T dedicated,
                    SIZE_T dedicatedSys, SIZE_T shared) {
    char n[128] = {};
    if (name) WideCharToMultiByte(CP_UTF8, 0, name, -1, n, sizeof(n) - 1, nullptr, nullptr);
    Log::get().note("vram budget probe: first %s by %s (exe+0x%llX): hr 0x%08lX \"%s\" DedicatedVideoMemory %llu MiB, "
                    "DedicatedSystemMemory %llu MiB, SharedSystemMemory %llu MiB.",
                    kMethodName[m], kCallerName[who], static_cast<unsigned long long>(offset), static_cast<unsigned long>(hr), n,
                    static_cast<unsigned long long>(dedicated >> 20), static_cast<unsigned long long>(dedicatedSys >> 20),
                    static_cast<unsigned long long>(shared >> 20));
}

inline HRESULT STDMETHODCALLTYPE getDesc(IDXGIAdapter* self, DXGI_ADAPTER_DESC* d) {
    const HRESULT hr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter*, DXGI_ADAPTER_DESC*)>(g_real[kGetDesc])(self, d);
    Caller who; uintptr_t off;
    if (note(kGetDesc, _ReturnAddress(), &who, &off) && d)
        logDesc(kGetDesc, who, off, hr, d->Description, d->DedicatedVideoMemory, d->DedicatedSystemMemory, d->SharedSystemMemory);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE getDesc1(IDXGIAdapter1* self, DXGI_ADAPTER_DESC1* d) {
    const HRESULT hr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter1*, DXGI_ADAPTER_DESC1*)>(g_real[kGetDesc1])(self, d);
    Caller who; uintptr_t off;
    if (note(kGetDesc1, _ReturnAddress(), &who, &off) && d)
        logDesc(kGetDesc1, who, off, hr, d->Description, d->DedicatedVideoMemory, d->DedicatedSystemMemory, d->SharedSystemMemory);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE getDesc2(IDXGIAdapter2* self, DXGI_ADAPTER_DESC2* d) {
    const HRESULT hr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter2*, DXGI_ADAPTER_DESC2*)>(g_real[kGetDesc2])(self, d);
    Caller who; uintptr_t off;
    if (note(kGetDesc2, _ReturnAddress(), &who, &off) && d)
        logDesc(kGetDesc2, who, off, hr, d->Description, d->DedicatedVideoMemory, d->DedicatedSystemMemory, d->SharedSystemMemory);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE getDesc3(IDXGIAdapter4* self, DXGI_ADAPTER_DESC3* d) {
    const HRESULT hr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter4*, DXGI_ADAPTER_DESC3*)>(g_real[kGetDesc3])(self, d);
    Caller who; uintptr_t off;
    if (note(kGetDesc3, _ReturnAddress(), &who, &off) && d)
        logDesc(kGetDesc3, who, off, hr, d->Description, d->DedicatedVideoMemory, d->DedicatedSystemMemory, d->SharedSystemMemory);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE queryVideoMemoryInfo(IDXGIAdapter3* self, UINT node, DXGI_MEMORY_SEGMENT_GROUP group,
                                                      DXGI_QUERY_VIDEO_MEMORY_INFO* info) {
    const HRESULT hr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter3*, UINT, DXGI_MEMORY_SEGMENT_GROUP,
                                                                    DXGI_QUERY_VIDEO_MEMORY_INFO*)>(g_real[kQvmi])(self, node, group, info);
    Caller who; uintptr_t off;
    const bool first = note(kQvmi, _ReturnAddress(), &who, &off);
    if (who == kGame && SUCCEEDED(hr) && info) {
        g_lastBudget.store(info->Budget, std::memory_order_relaxed);
        g_lastUsage.store(info->CurrentUsage, std::memory_order_relaxed);
        g_lastReserve.store(info->CurrentReservation, std::memory_order_relaxed);
    }
    if (first)
        Log::get().note("vram budget probe: first QueryVideoMemoryInfo by %s (exe+0x%llX): node %u group %s, hr 0x%08lX, "
                        "budget %llu MB, usage %llu MB, available for reservation %llu MB, reservation %llu MB.",
                        kCallerName[who], static_cast<unsigned long long>(off), node,
                        group == DXGI_MEMORY_SEGMENT_GROUP_LOCAL ? "local" : "non-local", static_cast<unsigned long>(hr),
                        info ? static_cast<unsigned long long>(info->Budget >> 20) : 0ull,
                        info ? static_cast<unsigned long long>(info->CurrentUsage >> 20) : 0ull,
                        info ? static_cast<unsigned long long>(info->AvailableForReservation >> 20) : 0ull,
                        info ? static_cast<unsigned long long>(info->CurrentReservation >> 20) : 0ull);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE setVideoMemoryReservation(IDXGIAdapter3* self, UINT node, DXGI_MEMORY_SEGMENT_GROUP group,
                                                           UINT64 reservation) {
    const HRESULT hr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter3*, UINT, DXGI_MEMORY_SEGMENT_GROUP, UINT64)>(
        g_real[kReservation])(self, node, group, reservation);
    Caller who; uintptr_t off;
    if (note(kReservation, _ReturnAddress(), &who, &off))
        Log::get().note("vram budget probe: first SetVideoMemoryReservation by %s (exe+0x%llX): group %s, %llu MB, hr 0x%08lX.",
                        kCallerName[who], static_cast<unsigned long long>(off),
                        group == DXGI_MEMORY_SEGMENT_GROUP_LOCAL ? "local" : "non-local",
                        static_cast<unsigned long long>(reservation >> 20), static_cast<unsigned long>(hr));
    return hr;
}
inline HRESULT STDMETHODCALLTYPE registerBudgetEvent(IDXGIAdapter3* self, HANDLE event, DWORD* cookie) {
    const HRESULT hr = reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter3*, HANDLE, DWORD*)>(g_real[kBudgetEvent])(self, event, cookie);
    Caller who; uintptr_t off;
    if (note(kBudgetEvent, _ReturnAddress(), &who, &off))
        Log::get().note("vram budget probe: first RegisterVideoMemoryBudgetChangeNotificationEvent by %s (exe+0x%llX): hr 0x%08lX.",
                        kCallerName[who], static_cast<unsigned long long>(off), static_cast<unsigned long>(hr));
    return hr;
}

inline void* kHook[kMethods] = {reinterpret_cast<void*>(&getDesc), reinterpret_cast<void*>(&getDesc1),
                                reinterpret_cast<void*>(&getDesc2), reinterpret_cast<void*>(&queryVideoMemoryInfo),
                                reinterpret_cast<void*>(&setVideoMemoryReservation), reinterpret_cast<void*>(&registerBudgetEvent),
                                reinterpret_cast<void*>(&getDesc3)};

// Once, with the adapter the game's device sits on (device_hook.cpp hookFactoryForDevice).
inline void install(IDXGIAdapter* adapter) {
    if (!adapter || g_hook.attached()) return;
    if (!g_hook.attach(adapter)) {
        Log::get().note("vram budget probe: the adapter's table could not be read; nothing hooked.");
        return;
    }
    const size_t prefix = g_hook.executablePrefix();
    char hooked[256] = "";
    for (int m = 0; m < kMethods; ++m) {
        if (kSlot[m] >= prefix) continue;
        if (g_hook.replace(kSlot[m], kHook[m], &g_real[m])) {
            if (hooked[0]) strcat_s(hooked, ", ");
            strcat_s(hooked, kMethodName[m]);
        }
    }
    if (!g_hook.commit()) {
        g_hook.uninstall();
        for (void*& r : g_real) r = nullptr;
        Log::get().note("vram budget probe: the adapter hook did not commit; nothing hooked.");
        return;
    }
    Log::get().note("vram budget probe: hooked on the device's adapter (table %zu methods): %s. Calls before the device "
                    "existed (the game's own adapter enumeration) are not seen.", prefix, hooked);
    g_nextSummary = GetTickCount64() + 60000;
}

// Every frame (device_hook.cpp); a line every 60 s once installed.
inline void tick() {
    if (!g_hook.committed()) return;
    const uint64_t now = GetTickCount64();
    if (now < g_nextSummary) return;
    g_nextSummary = now + 60000;
    char line[512];
    int n = 0;
    for (int m = 0; m < kMethods; ++m) {
        const uint64_t a = g_count[m][kGame].load(std::memory_order_relaxed), b = g_count[m][kEdup].load(std::memory_order_relaxed),
                       c = g_count[m][kOther].load(std::memory_order_relaxed);
        if (!a && !b && !c) continue;
        n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, "%s%s game %llu edup %llu other %llu", n ? "; " : "", kMethodName[m],
                         static_cast<unsigned long long>(a), static_cast<unsigned long long>(b), static_cast<unsigned long long>(c));
        if (n < 0) break;
    }
    if (n <= 0) _snprintf_s(line, sizeof(line), _TRUNCATE, "no calls");
    Log::get().note("vram budget probe: calls since the hook: %s; the game's last QueryVideoMemoryInfo answer: budget %llu MB, usage %llu MB, "
                    "reservation %llu MB.",
                    line, static_cast<unsigned long long>(g_lastBudget.load(std::memory_order_relaxed) >> 20),
                    static_cast<unsigned long long>(g_lastUsage.load(std::memory_order_relaxed) >> 20),
                    static_cast<unsigned long long>(g_lastReserve.load(std::memory_order_relaxed) >> 20));
}

}  // namespace edvr::vram_budget_probe

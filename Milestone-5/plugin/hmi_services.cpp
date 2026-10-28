// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Exported HMI services: plugin resolver, main dispatcher unit list, processing-unit timer.
// Layouts are documented in plugin_abi.hpp (reversed from libhmicom.so / HmiMain3D).
#include "hmi_services.hpp"
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

void nbtPluginLog(const char* fmt, ...);   // defined in nbt_aad_plugin.cpp

namespace nbt {
namespace hmi {

namespace {

typedef void (*CapuStrCtor)(void* self, const char* s);
typedef void (*CapuStrDtor)(void* self);
typedef void (*MutexOp)(void* self);
typedef void* (*TreeNext)(void* node);
typedef int (*ResolveFn)(void* resolver, const void* name, void** out, int flags);
typedef void* (*GetTimerFn)(void* procUnit);
typedef int (*SetTimerFn)(void* timer, void* target, void* fn, int adj, unsigned firstMs, unsigned periodMs,
                          unsigned user);

CapuStrCtor s_strCtor = 0;
CapuStrDtor s_strDtor = 0;
MutexOp s_mutexTake = 0;
MutexOp s_mutexGive = 0;
TreeNext s_treeNext = 0;

void* sym(const char* name) {
    void* p = dlsym(RTLD_DEFAULT, name);
    if (!p) nbtPluginLog("symbol %s not found", name);
    return p;
}

inline void* word(const void* base, size_t off) {
    return *reinterpret_cast<void* const*>(reinterpret_cast<const char*>(base) + off);
}

const char* rttiName(const void* obj) {
    const void* vtbl = word(obj, 0);
    if (!vtbl) return "";
    const void* ti = *(reinterpret_cast<void* const*>(vtbl) - 1);
    if (!ti) return "";
    const char* n = reinterpret_cast<const char*>(word(ti, 4));
    return n ? n : "";
}

} // namespace

bool loadSymbols() {
    s_strCtor = reinterpret_cast<CapuStrCtor>(sym("_ZN4capu6StringC1EPKc"));
    s_strDtor = reinterpret_cast<CapuStrDtor>(sym("_ZN4capu6StringD1Ev"));
    s_mutexTake = reinterpret_cast<MutexOp>(sym("_ZN8COSMutex4takeEv"));
    s_mutexGive = reinterpret_cast<MutexOp>(sym("_ZN8COSMutex4giveEv"));
    s_treeNext = reinterpret_cast<TreeNext>(sym("_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base"));
    return s_strCtor && s_strDtor && s_mutexTake && s_mutexGive && s_treeNext;
}

void* resolvePlugin(void* resolver, const char* name) {
    if (!resolver || !s_strCtor) return 0;
    char str[32];                                   // capu::String (16 bytes used)
    memset(str, 0, sizeof(str));
    s_strCtor(str, name);
    void* out = 0;
    ResolveFn fn = reinterpret_cast<ResolveFn>(reinterpret_cast<void* const*>(word(resolver, 0))[2]);
    int rc = fn(resolver, str, &out, 0);
    s_strDtor(str);
    nbtPluginLog("resolve(%s) -> rc %d, %p", name, rc, out);
    return rc == 0 ? out : 0;
}

// CHmiMainDispatcher: std::map<uint, std::vector<IHmiProcUnit*>> header at +4 (leftmost +0xc),
// node vector begin/end at +0x14/+0x18, guarded by COSMutex at +0x18.
void* findAppProcUnit(void* disp) {
    if (!disp) return 0;
    char* d = reinterpret_cast<char*>(disp);
    void* found = 0;
    s_mutexTake(d + 0x18);
    void* header = d + 4;
    for (void* n = word(d, 0xc); n && n != header && !found; n = s_treeNext(n)) {
        void** it = reinterpret_cast<void**>(word(n, 0x14));
        void** end = reinterpret_cast<void**>(word(n, 0x18));
        for (; it && it != end; ++it) {
            if (*it && strcmp(rttiName(*it), "15CHmiAppProcUnit") == 0) { found = *it; break; }
        }
    }
    s_mutexGive(d + 0x18);
    return found;
}

// CHmiVariantServer (libhmicoding.so, registered as static plugin "IHmiVariantServer") answers
// isVariantSet(feature) from a dense in-object array of {int32 id; uint8 value; ...} entries indexed by
// feature (each id filled at init), NOT from the CAFD blob. We flip a gate (e.g. CarPlay 0x5153) by setting
// the value byte of its entry to 1 (RAM only -> reverts on reboot; nothing dlsym'd or hard-addressed).
//
// Binary-independent: no fixed stride / base-index / feature-range. The array is found by its self-evident
// SHAPE - three entries whose ids are consecutive integers N, N+1, N+2 and whose value bytes are boolean -
// which yields the array origin, the entry stride and a reference id. We then jump to the requested feature's
// entry and only write if the id word there really equals the feature, so a wrong geometry can never corrupt
// memory. The one remaining local fact is that the value byte sits right after the 4-byte id (guarded: it must
// read back as 0/1).
namespace {
const size_t kIvaScanCap = 0x8000;                 // numeric read guard only (the variant-server object is large)
inline int32_t ivaI32(const void* b, size_t o) { return *reinterpret_cast<const volatile int32_t*>(reinterpret_cast<const char*>(b) + o); }
inline uint8_t ivaU8 (const void* b, size_t o) { return *reinterpret_cast<const volatile uint8_t*>(reinterpret_cast<const char*>(b) + o); }

// Locate the IVA entry array: origin offset, its first id, and the entry stride. False if the shape is absent.
bool findIvaArray(const void* vs, size_t& origin, int32_t& firstId, size_t& stride) {
    for (size_t a = 4; a + 8 <= kIvaScanCap; a += 4) {
        int32_t n = ivaI32(vs, a);
        if (n < 0x100 || n > 0x100000) continue;             // a small int id, not a pointer / garbage
        if (ivaU8(vs, a + 4) > 1) continue;                  // value byte must be boolean
        for (size_t s = 4; s <= 64; s += 4) {
            if (a + 2 * s + 5 > kIvaScanCap) break;
            if (ivaI32(vs, a + s) == n + 1 && ivaI32(vs, a + 2 * s) == n + 2 &&
                ivaU8(vs, a + s + 4) <= 1 && ivaU8(vs, a + 2 * s + 4) <= 1) {
                origin = a; firstId = n; stride = s; return true;   // three consecutive ids + boolean values
            }
        }
    }
    return false;
}
} // namespace

int setVariantIvaFlag(void* resolver, unsigned feature) {
    if (!s_strCtor && !loadSymbols()) { nbtPluginLog("IVA: loadSymbols failed"); return 0; }
    void* vs = resolvePlugin(resolver, "IHmiVariantServer");
    if (!vs) { nbtPluginLog("IVA: IHmiVariantServer not resolvable from this resolver"); return 0; }
    const char* rn = rttiName(vs);
    if (!strstr(rn, "VariantServer")) { nbtPluginLog("IVA: resolved RTTI '%s' is not a variant server - abort", rn); return 0; }

    size_t origin = 0, stride = 0;
    int32_t firstId = 0;
    if (!findIvaArray(vs, origin, firstId, stride)) {
        nbtPluginLog("IVA 0x%x: entry-array shape not found on %s (%p) - not writing", feature, rn, vs);
        return -1;
    }
    long target = (long)origin + ((long)feature - (long)firstId) * (long)stride;
    if (target < 4 || (size_t)target + 5 > kIvaScanCap) {
        nbtPluginLog("IVA 0x%x: computed entry @+0x%lx out of range (array@+0x%zx stride %zu id0 0x%x)",
                     feature, target, origin, stride, (unsigned)firstId);
        return -1;
    }
    // Self-test: the id at the computed entry must equal the feature, else our geometry is wrong -> no write.
    if (ivaI32(vs, (size_t)target) != static_cast<int32_t>(feature)) {
        nbtPluginLog("IVA 0x%x: id @computed +0x%lx = 0x%x != feature -> not writing (array@+0x%zx stride %zu)",
                     feature, target, (unsigned)ivaI32(vs, (size_t)target), origin, stride);
        return -1;
    }
    volatile uint8_t* valByte = reinterpret_cast<volatile uint8_t*>(reinterpret_cast<char*>(vs) + target + 4);
    uint8_t prev = *valByte;
    *valByte = 1;                                                 // flip the gate (replicates ___IVAfillEntry value)
    nbtPluginLog("IVA 0x%x SET on %s (%p): array@+0x%zx stride %zu id0 0x%x, value @+0x%lx %u -> %u",
                 feature, rn, vs, origin, stride, (unsigned)firstId, target + 4, prev, (unsigned)*valByte);
    return (*valByte == 1) ? 1 : -1;
}

// IHmiProcUnit vtbl[3] = getTimer(); IHmiTimer vtbl[1] = setTimer(target, memfn{fn, adj}, first, period, user).
int armTimer(void* pu, void* target, TimerFn fn, unsigned delayMs) {
    if (!pu) return 0;
    GetTimerFn gt = reinterpret_cast<GetTimerFn>(reinterpret_cast<void* const*>(word(pu, 0))[3]);
    void* timer = gt(pu);
    if (!timer) return 0;
    SetTimerFn st = reinterpret_cast<SetTimerFn>(reinterpret_cast<void* const*>(word(timer, 0))[1]);
    return st(timer, target, reinterpret_cast<void*>(fn), 0, delayMs < 10 ? 10 : delayMs, 0, 0);
}

} // namespace hmi
} // namespace nbt

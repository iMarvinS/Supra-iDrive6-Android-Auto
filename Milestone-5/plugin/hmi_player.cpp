// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Weg-B projection-player registration through iDrive's own services. Runs on the HMI thread.
//
// Model (reversed from HmiMain3D, documented in aa-supra/focus_problem.md §7d):
//   - CHmiActivityMainMenu::registerPlayer(self, app)  [FUN_0030309c]: for app type 0x10 (CarPlay) and
//     with VACodingFeature 0x5153 set, it builds an internal 0x9c-byte player wrapper from `app`, hangs it
//     in the registry at self+0x178, and (if the CarPlay service reports available) auto-calls
//     onCarPlayStateChanged. We supply `app` = a real CHmiCarPlayApplication descriptor (0x54 bytes, built
//     with iDrive's own ctor FUN_00440684) so every one of its 29 vtable slots is natively correct.
//   - onCarPlayStateChanged(self, available)  [FUN_00302cd0]: looks up the wrapper by focusId 0x2535 and
//     calls wrapper.vt[13](available?2:0). The wrapper's activate (FUN_00323b68) is a state-machine poke,
//     NOT a projection-surface grab, so it is safe to drive without a live CarPlay session.
//
// Everything is validated before use: firmware code-checks on each function, RTTI checks on the descriptor
// ("22CHmiCarPlayApplication") and on the located main-menu instance ("20CHmiActivityMainMenu"). If any
// check fails nothing HMI-mutating happens. The caller keeps this inert unless explicitly armed.
#include "hmi_player.hpp"
#include "hmi_services.hpp"
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>
#include <new>

void nbtPluginLog(const char* fmt, ...);   // defined in nbt_aad_plugin.cpp

namespace nbt {
namespace hmi {

namespace {

// ---- non-exported HmiMain3D functions (this firmware build) -----------------------------------
const uint32_t kRegisterPlayer      = 0x0030309c;   // registerPlayer(self, app)
const uint32_t kOnCarPlayState      = 0x00302cd0;   // onCarPlayStateChanged(self, available)
const uint32_t kDescCtor            = 0x00440684;   // CHmiCarPlayApplication ctor(this)
const uint32_t kDescSize            = 0x54;

const char* const kDescRtti     = "22CHmiCarPlayApplication";
const char* const kMainMenuRtti = "20CHmiActivityMainMenu";

struct CodeCheck { uint32_t addr, w0, w1; const char* what; };
const CodeCheck kChecks[] = {
    { kRegisterPlayer, 0xe92d4ff0, 0xe2514000, "registerPlayer" },
    { kOnCarPlayState, 0xe92d4870, 0xe28db010, "onCarPlayStateChanged" },
    { kDescCtor,       0xe92d4830, 0xe28db00c, "CHmiCarPlayApplication ctor" },
};

typedef int   (*RegisterPlayerFn)(void* self, void* app);
typedef void  (*OnCarPlayStateFn)(void* self, char available);
typedef void* (*DescCtorFn)(void* self);

bool  s_checked = false;
void* s_mainMenu = 0;      // located + RTTI-verified CHmiActivityMainMenu
void* s_descriptor = 0;    // our long-lived CHmiCarPlayApplication descriptor (kept alive forever)

inline uint32_t rd32(uintptr_t a) { return *reinterpret_cast<volatile uint32_t*>(a); }
inline void* word(const void* base, size_t off) {
    return *reinterpret_cast<void* const*>(reinterpret_cast<const char*>(base) + off);
}

// A pointer that may legitimately point into HmiMain3D code/.rodata/.data (vtables, typeinfo, RTTI names).
inline bool isCodeData(uintptr_t p) { return p >= 0x00100000u && p < 0x04000000u && (p & 3) == 0; }
// A pointer that may legitimately be a live object (heap/bss/data - QNX user heap can sit high). Wide, but
// every candidate must then pass the code/data checks on its vtable+typeinfo+name (those are the real
// filter), which rejects essentially all garbage.
inline bool isObjPtr(uintptr_t p) { return p >= 0x00100000u && p < 0xf0000000u && (p & 3) == 0; }

// RTTI class name of an object, with every dereference range-guarded so a stray pointer cannot fault.
// Returns "" when the chain does not look like a valid C++ polymorphic object.
const char* safeRttiName(const void* obj) {
    uintptr_t o = reinterpret_cast<uintptr_t>(obj);
    if (!isObjPtr(o)) return "";
    uintptr_t vt = reinterpret_cast<uintptr_t>(word(obj, 0));
    if (!isCodeData(vt)) return "";
    uintptr_t ti = reinterpret_cast<uintptr_t>(*(reinterpret_cast<void* const*>(vt) - 1));
    if (!isCodeData(ti)) return "";
    uintptr_t nm = reinterpret_cast<uintptr_t>(word(reinterpret_cast<void*>(ti), 4));
    if (!isCodeData(nm)) return "";
    return reinterpret_cast<const char*>(nm);
}

// CHmiActivityMainMenu is an embedded subobject of CHmiHomeApplication at this fixed offset (reversed:
// CHmiHomeApplication primary vtable @0x2b48e50; its slot-2 registrar FUN_002f2d40 calls
// registerPlayer(this+0x50, app) -> focus_problem.md §7d). IHmiHome resolves to the CHmiHomeApplication.
const size_t kHomeToMainMenu = 0x50;

void* findMainMenu(void* resolver) {
    void* home = resolvePlugin(resolver, "IHmiHome");
    if (!home) { nbtPluginLog("player: IHmiHome not resolvable"); return 0; }

    // Primary path: the pinned offset. RTTI self-check keeps a wrong pointer from being used.
    void* pinned = reinterpret_cast<char*>(home) + kHomeToMainMenu;
    if (strcmp(safeRttiName(pinned), kMainMenuRtti) == 0) {
        nbtPluginLog("player: mainMenu @ %p (IHmiHome %p + 0x%x, pinned)", pinned, home, (unsigned)kHomeToMainMenu);
        return pinned;
    }

    // Fallback: if the resolver handed back an interface subobject at a non-zero offset, the mainMenu is a
    // small delta away. Bounded, range-guarded scan; log the real delta so the pin can be corrected.
    nbtPluginLog("player: %s not at IHmiHome+0x%x (rtti '%s') - scanning window", kMainMenuRtti,
                 (unsigned)kHomeToMainMenu, safeRttiName(pinned));
    for (int off = -0x80; off <= 0x140; off += 4) {
        void* cand = reinterpret_cast<char*>(home) + off;
        if (strcmp(safeRttiName(cand), kMainMenuRtti) == 0) {
            nbtPluginLog("player: mainMenu @ %p (IHmiHome %p %+d) - correct kHomeToMainMenu to %+d", cand, home, off, off);
            return cand;
        }
    }
    return 0;
}

} // namespace

bool checkPlayerFirmware() {
    for (size_t i = 0; i < sizeof(kChecks) / sizeof(kChecks[0]); ++i) {
        const CodeCheck& c = kChecks[i];
        if (rd32(c.addr) != c.w0 || rd32(c.addr + 4) != c.w1) {
            nbtPluginLog("player firmware check FAILED: %s @0x%08x (got 0x%08x 0x%08x)",
                         c.what, c.addr, rd32(c.addr), rd32(c.addr + 4));
            return false;
        }
    }
    s_checked = true;
    nbtPluginLog("player firmware check OK");
    return true;
}

int registerProjectionPlayer(void* resolver) {
    if (s_descriptor) return -1;                 // already registered
    if (!s_checked && !checkPlayerFirmware()) return -1;

    void* mm = findMainMenu(resolver);
    if (!mm) { nbtPluginLog("player: CHmiActivityMainMenu not located yet - will retry"); return 0; }
    s_mainMenu = mm;

    // Build a genuine CHmiCarPlayApplication descriptor (all 29 vtable slots native: type 0x10,
    // focusId 0x2535, stateId 0x2739). The ctor takes only `this`.
    void* desc = ::operator new(kDescSize);
    memset(desc, 0, kDescSize);
    reinterpret_cast<DescCtorFn>(static_cast<uintptr_t>(kDescCtor))(desc);
    const char* rn = safeRttiName(desc);
    if (strcmp(rn, kDescRtti) != 0) {
        nbtPluginLog("player: descriptor RTTI '%s' != '%s' - abort (not registering)", rn, kDescRtti);
        ::operator delete(desc);                 // not registered anywhere yet, safe to free
        return -1;
    }

    RegisterPlayerFn registerPlayer = reinterpret_cast<RegisterPlayerFn>(static_cast<uintptr_t>(kRegisterPlayer));
    int rc = registerPlayer(mm, desc);           // hangs an internal wrapper (focusId 0x2535) in mm+0x178
    s_descriptor = desc;                          // registry now holds a raw pointer to it -> keep alive
    nbtPluginLog("player: registerPlayer(mainMenu %p, desc %p) -> %d", mm, desc, rc);

    // registerPlayer auto-activates only if the CarPlay service reports available; on a car not coded for
    // CarPlay that service is usually absent, so drive the activation ourselves.
    setProjectionActive(true);
    return 1;
}

int setProjectionActive(bool active) {
    if (!s_checked) return -1;
    if (!s_mainMenu || !s_descriptor) { nbtPluginLog("player: setProjectionActive(%d) - no registered player", active); return 0; }
    reinterpret_cast<OnCarPlayStateFn>(static_cast<uintptr_t>(kOnCarPlayState))(s_mainMenu, active ? 1 : 0);
    nbtPluginLog("player: onCarPlayStateChanged(mainMenu %p, %d) called", s_mainMenu, active ? 1 : 0);
    return 1;
}

} // namespace hmi
} // namespace nbt

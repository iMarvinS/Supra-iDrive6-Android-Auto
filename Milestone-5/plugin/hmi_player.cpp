// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Weg-B projection-player registration through iDrive's own services. Runs on the HMI thread.
//
// Model (reversed from HmiMain3D, documented in aa-supra/focus_problem.md §7d):
//   - CHmiActivityMainMenu::registerPlayer(self, app): for app type 0x10 (CarPlay) and with VACodingFeature
//     0x5153 set, it builds an internal wrapper from `app`, hangs it in the registry, and (if the CarPlay
//     service reports available) auto-calls onCarPlayStateChanged. We supply `app` = a real
//     CHmiCarPlayApplication descriptor built with iDrive's own constructor so all its vtable slots are native.
//   - onCarPlayStateChanged(self, available): looks up the wrapper by focusId 0x2535 and drives its activate
//     slot - a state-machine poke, NOT a projection-surface grab, so it is safe without a live CarPlay session.
//
// BINARY-INDEPENDENT: no fixed function addresses, no fixed offsets. Everything is located at runtime from
// stable semantic anchors (HMI trace strings + RTTI class names), the same technique as the input gate:
//   - registerPlayer           : via its unique assert text "Player of type(%d) already registered..."
//                                 ("registerPlayer" alone is a substring of "unregisterPlayer")
//   - onCarPlayStateChanged     : via the string "onCarPlayStateChanged(), CP available: %s"
//   - CHmiCarPlayApplication ctor : via RTTI "22CHmiCarPlayApplication" -> its primary vtable (offset-to-top
//                                  0) -> the function that installs that vtable at [this+0] and never reads a
//                                  second argument (so never a copy ctor)
//   - the CHmiActivityMainMenu instance : RTTI scan "20CHmiActivityMainMenu" off IHmiHome
// The descriptor is over-allocated so a larger-than-expected object can never overflow, and its RTTI is
// re-checked after construction; if any step fails nothing HMI-mutating happens.
#include "hmi_player.hpp"
#include "hmi_services.hpp"
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <new>
#ifdef __QNX__
#include <sys/elf.h>
#endif

void nbtPluginLog(const char* fmt, ...);   // defined in nbt_aad_plugin.cpp

namespace nbt {
namespace hmi {

namespace {

const char* const kDescRtti     = "22CHmiCarPlayApplication";
const char* const kMainMenuRtti = "20CHmiActivityMainMenu";
const size_t      kDescAlloc    = 0x200;   // over-allocate: far larger than any plausible descriptor size

typedef int   (*RegisterPlayerFn)(void* self, void* app);
typedef void  (*OnCarPlayStateFn)(void* self, char available);
typedef void* (*DescCtorFn)(void* self);

// ---- loaded-image ranges (discovered at runtime, no fixed base) -----------------------------------------
struct Image {
    uintptr_t reLo, reHi;              // executable segment: code + rodata (strings, literal pools, names)
    uintptr_t modLo, modHi;           // whole module span (RTTI pointer validation)
    uintptr_t segLo[6], segHi[6];     // file-backed PT_LOAD ranges (word search; vtables may sit in RW)
    int nseg;
    Image() : reLo(0), reHi(0), modLo(0), modHi(0), nseg(0) {}
};

inline bool inCode(const Image& im, uintptr_t a, size_t n) { return im.reLo && a >= im.reLo && a + n <= im.reHi; }
inline bool inMod(const Image& im, uintptr_t a, size_t n) { return im.modLo && a >= im.modLo && a + n <= im.modHi; }
inline uint32_t rdc(uintptr_t a) { return *reinterpret_cast<const volatile uint32_t*>(a); }
inline bool rd32(const Image& im, uintptr_t a, uint32_t& out) {
    if ((a & 3) || !inCode(im, a, 4)) return false;
    out = rdc(a);
    return true;
}

bool findImage(const void* anchor, Image& im) {
#ifdef __QNX__
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (!dladdr(const_cast<void*>(anchor), &info) || !info.dli_fbase) { nbtPluginLog("player: dladdr failed"); return false; }
    const unsigned char* base = reinterpret_cast<const unsigned char*>(info.dli_fbase);
    if (base[0] != 0x7f || base[1] != 'E' || base[2] != 'L' || base[3] != 'F') { nbtPluginLog("player: no ELF header at base"); return false; }
    const Elf32_Ehdr* eh = reinterpret_cast<const Elf32_Ehdr*>(base);
    const Elf32_Phdr* ph = reinterpret_cast<const Elf32_Phdr*>(base + eh->e_phoff);
    uintptr_t bias = 0; bool haveBias = false;
    for (int i = 0; i < eh->e_phnum; ++i)
        if (ph[i].p_type == PT_LOAD && ph[i].p_offset == 0) { bias = (uintptr_t)base - (uintptr_t)ph[i].p_vaddr; haveBias = true; break; }
    if (!haveBias) { nbtPluginLog("player: no PT_LOAD@0"); return false; }
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        uintptr_t lo = bias + (uintptr_t)ph[i].p_vaddr;
        uintptr_t hiFile = lo + (uintptr_t)ph[i].p_filesz;
        uintptr_t hiMem = lo + (uintptr_t)ph[i].p_memsz;
        if (!im.modLo || lo < im.modLo) im.modLo = lo;
        if (hiMem > im.modHi) im.modHi = hiMem;
        if ((ph[i].p_flags & PF_X) && !im.reLo) { im.reLo = lo; im.reHi = hiFile; }
        if (im.nseg < 6) { im.segLo[im.nseg] = lo; im.segHi[im.nseg] = hiFile; ++im.nseg; }
    }
    if (!im.reLo) { nbtPluginLog("player: no executable segment"); return false; }
    return true;
#else
    (void)anchor; (void)im; return false;
#endif
}

// RTTI name of an object: vtable/typeinfo may live anywhere in the module, the name string in rodata.
const char* safeRtti(const Image& im, const void* obj) {
    uintptr_t o = reinterpret_cast<uintptr_t>(obj);
    if ((o & 3) || o < 0x1000) return "";
    uint32_t vt = rdc(o);                                   // caller guarantees obj is readable
    if ((vt & 3) || !inMod(im, vt, 4) || !inMod(im, vt - 4, 4)) return "";
    uint32_t ti = rdc(vt - 4);
    if ((ti & 3) || !inMod(im, ti + 4, 4)) return "";
    uint32_t nm = rdc(ti + 4);
    if (!inCode(im, nm, 1)) return "";
    return reinterpret_cast<const char*>(nm);
}
bool offsetToTopZero(const Image& im, uintptr_t vtap) {
    return inMod(im, vtap - 8, 4) && rdc(vtap - 8) == 0;
}

// ---- searches within the image --------------------------------------------------------------------------
uintptr_t findCStr(const Image& im, const char* needle) {   // start of the NUL-terminated string in rodata
    size_t n = strlen(needle);
    if (!n || !im.reLo) return 0;
    const char* base = reinterpret_cast<const char*>(im.reLo);
    size_t len = im.reHi - im.reLo;
    char c0 = needle[0];
    for (size_t i = 0; i + n <= len; ++i) {
        if (base[i] != c0 || memcmp(base + i, needle, n) != 0) continue;
        size_t s = i;
        while (s > 0 && base[s - 1] != '\0') --s;
        return im.reLo + s;
    }
    return 0;
}
uintptr_t findLitExec(const Image& im, uint32_t target, uintptr_t startAfter) {   // literal pools live in .text
    uintptr_t a = (startAfter < im.reLo ? im.reLo : ((startAfter + 3) & ~(uintptr_t)3));
    for (; a + 4 <= im.reHi; a += 4) if (rdc(a) == target) return a;
    return 0;
}
uintptr_t findWordMod(const Image& im, uint32_t target, int seg, uintptr_t startAfter) {   // any file-backed segment
    for (; seg < im.nseg; ++seg) {
        uintptr_t a = (startAfter > im.segLo[seg] ? ((startAfter + 3) & ~(uintptr_t)3) : im.segLo[seg]);
        for (; a + 4 <= im.segHi[seg]; a += 4) if (rdc(a) == target) return a;
        startAfter = 0;
    }
    return 0;
}

// ---- minimal ARM (A32) decoders -------------------------------------------------------------------------
uintptr_t ldrLiteralTarget(uint32_t w, uintptr_t pc) {
    if (((w >> 26) & 3) != 1 || ((w >> 25) & 1) != 0 || ((w >> 22) & 1) != 0 || ((w >> 20) & 1) != 1 || ((w >> 16) & 0xF) != 15) return 0;
    uint32_t imm = w & 0xFFF;
    return ((w >> 23) & 1) ? pc + 8 + imm : pc + 8 - imm;
}
bool isPushLr(uint32_t w) {
    return ((w >> 25) & 7) == 4 && ((w >> 24) & 1) == 1 && ((w >> 23) & 1) == 0 && ((w >> 21) & 1) == 1 &&
           ((w >> 20) & 1) == 0 && ((w >> 16) & 0xF) == 13 && (w & (1u << 14));
}
bool isReturn(uint32_t w) { return (w & 0x0fff8000) == 0x08bd8000 || w == 0xe12fff1e; }   // LDM sp!,{..pc} / bx lr
bool isStrToBase0(uint32_t w, int rt) {                                                     // STR rt,[Rn,#0]
    return ((w >> 26) & 3) == 1 && ((w >> 25) & 1) == 0 && ((w >> 22) & 1) == 0 && ((w >> 20) & 1) == 0 &&
           ((w >> 23) & 1) == 1 && (w & 0xFFF) == 0 && (int)((w >> 12) & 0xF) == rt && ((w >> 16) & 0xF) != 15;
}
bool isLoadFromR1(uint32_t w) { return ((w >> 26) & 3) == 1 && ((w >> 20) & 1) == 1 && ((w >> 16) & 0xF) == 1; }

// ---- derive a function from a trace string it loads -----------------------------------------------------
uintptr_t deriveFuncByString(const Image& im, const char* str) {
    uintptr_t sv = findCStr(im, str);
    if (!sv) { nbtPluginLog("player: string not found: '%s'", str); return 0; }
    for (uintptr_t lit = findLitExec(im, (uint32_t)sv, im.reLo); lit; lit = findLitExec(im, (uint32_t)sv, lit + 4)) {
        uintptr_t ldr = 0;
        for (uintptr_t a = lit - 4; lit - a <= 0x800; a -= 4) {
            uint32_t w; if (!rd32(im, a, w)) break;
            if (ldrLiteralTarget(w, a) == lit) { ldr = a; break; }
            if (a < im.reLo + 4) break;
        }
        if (!ldr) continue;
        for (uintptr_t a = ldr; ldr - a <= 0x2500; a -= 4) {
            uint32_t w; if (!rd32(im, a, w)) break;
            if (isPushLr(w)) return a;
            if (a < im.reLo + 4) break;
        }
    }
    nbtPluginLog("player: no function derived from '%s'", str);
    return 0;
}

// ---- find a class's primary vtable (offset-to-top 0) from its RTTI name ----------------------------------
uint32_t findPrimaryVtable(const Image& im, const char* rttiName) {
    // the typeinfo *name* string lives in rodata, NUL-delimited and unique
    char bounded[64];
    size_t n = strlen(rttiName);
    if (n + 2 > sizeof(bounded)) return 0;
    bounded[0] = '\0'; memcpy(bounded + 1, rttiName, n + 1);    // "\0name\0"
    // locate "\0name\0" then the name start
    const char* base = reinterpret_cast<const char*>(im.reLo);
    size_t len = im.reHi - im.reLo;
    uintptr_t nameAddr = 0;
    for (size_t i = 0; i + n + 2 <= len; ++i)
        if (memcmp(base + i, bounded, n + 2) == 0) { nameAddr = im.reLo + i + 1; break; }
    if (!nameAddr) { nbtPluginLog("player: RTTI name '%s' not found", rttiName); return 0; }
    // typeinfo: a word == nameAddr sits at typeinfo+4 -> typeinfo = loc-4; vtable: word == typeinfo at vtable-4
    for (uintptr_t nloc = findWordMod(im, (uint32_t)nameAddr, 0, 0); nloc;
         nloc = findWordMod(im, (uint32_t)nameAddr, 0, nloc + 4)) {
        uintptr_t ti = nloc - 4;
        for (uintptr_t vloc = findWordMod(im, (uint32_t)ti, 0, 0); vloc;
             vloc = findWordMod(im, (uint32_t)ti, 0, vloc + 4)) {
            uint32_t vtap = (uint32_t)(vloc + 4);
            if (offsetToTopZero(im, vtap)) return vtap;                 // primary (complete-object) vtable
        }
    }
    return 0;
}

// ---- find the constructor: installs `vtap` at [this+0] and never reads its 2nd arg (so not a copy ctor) --
uintptr_t deriveCtor(const Image& im, uint32_t vtap) {
    uintptr_t best = 0;
    for (uintptr_t lit = findLitExec(im, vtap, im.reLo); lit; lit = findLitExec(im, vtap, lit + 4)) {
        // LDR that loads this vtable literal
        uintptr_t ldr = 0; uint32_t ldrw = 0;
        for (uintptr_t a = lit - 4; lit - a <= 0x200; a -= 4) {
            uint32_t w; if (!rd32(im, a, w)) break;
            if (ldrLiteralTarget(w, a) == lit) { ldr = a; ldrw = w; break; }
            if (a < im.reLo + 4) break;
        }
        if (!ldr) continue;
        int rt = (int)((ldrw >> 12) & 0xF);
        bool installs = false;
        for (uintptr_t a = ldr; a <= ldr + 0x24; a += 4) { uint32_t w; if (!rd32(im, a, w)) break; if (isStrToBase0(w, rt)) { installs = true; break; } }
        if (!installs) continue;
        uintptr_t fs = 0;
        for (uintptr_t a = ldr; ldr - a <= 0x200; a -= 4) { uint32_t w; if (!rd32(im, a, w)) break; if (isPushLr(w)) { fs = a; break; } if (a < im.reLo + 4) break; }
        if (!fs) continue;
        bool copy = false;                                             // reject copy ctors (read from 2nd arg r1)
        for (uintptr_t a = fs; a < fs + 0x200; a += 4) { uint32_t w; if (!rd32(im, a, w)) break; if (isLoadFromR1(w)) { copy = true; break; } if (isReturn(w)) break; }
        if (copy) continue;
        if (!best || fs < best) best = fs;                             // lowest-address installer = complete-object ctor
    }
    return best;
}

// ---- state ----------------------------------------------------------------------------------------------
bool  s_ready = false;
Image s_im;
RegisterPlayerFn s_registerPlayer = 0;
OnCarPlayStateFn s_onCarPlayState = 0;
DescCtorFn       s_descCtor       = 0;
uint32_t         s_descVtap       = 0;
void* s_mainMenu = 0;
void* s_descriptor = 0;

// CHmiActivityMainMenu is an embedded subobject of CHmiHomeApplication; locate it by RTTI (no pinned offset).
void* findMainMenu(void* resolver) {
    void* home = resolvePlugin(resolver, "IHmiHome");
    if (!home) { nbtPluginLog("player: IHmiHome not resolvable"); return 0; }
    for (int off = -0x80; off <= 0x200; off += 4) {
        void* cand = reinterpret_cast<char*>(home) + off;
        if (strcmp(safeRtti(s_im, cand), kMainMenuRtti) == 0) {
            nbtPluginLog("player: mainMenu @ %p (IHmiHome %p %+d)", cand, home, off);
            return cand;
        }
    }
    nbtPluginLog("player: %s not found near IHmiHome %p", kMainMenuRtti, home);
    return 0;
}

} // namespace

// Derive every HmiMain3D target the player path needs. Replaces the old fixed-address firmware check.
bool checkPlayerFirmware() {
    if (s_ready) return true;
    // Anchor the image on an exported HmiMain3D symbol (the same marker the plugin uses for "inside HmiMain3D").
    void* anchor = dlsym(RTLD_DEFAULT, "_ZN10THmiStringItEC1ItvEEPKc");
    if (!anchor) { nbtPluginLog("player: not inside HmiMain3D (anchor symbol missing)"); return false; }
    if (!findImage(anchor, s_im)) return false;
    nbtPluginLog("player: code [%08lx,%08lx) module [%08lx,%08lx)", (unsigned long)s_im.reLo,
                 (unsigned long)s_im.reHi, (unsigned long)s_im.modLo, (unsigned long)s_im.modHi);

    // "registerPlayer" alone is a substring of "unregisterPlayer"; use registerPlayer's unique assert text.
    uintptr_t reg = deriveFuncByString(s_im, "Player of type");
    uintptr_t ocp = deriveFuncByString(s_im, "onCarPlayStateChanged(), CP available");
    s_descVtap = findPrimaryVtable(s_im, kDescRtti);
    uintptr_t ctor = s_descVtap ? deriveCtor(s_im, s_descVtap) : 0;
    if (!reg || !ocp || !s_descVtap || !ctor) {
        nbtPluginLog("player: derivation incomplete (registerPlayer=%p onCarPlayState=%p vtable=0x%x ctor=%p) - disabled",
                     (void*)reg, (void*)ocp, (unsigned)s_descVtap, (void*)ctor);
        return false;
    }
    s_registerPlayer = reinterpret_cast<RegisterPlayerFn>(reg);
    s_onCarPlayState = reinterpret_cast<OnCarPlayStateFn>(ocp);
    s_descCtor       = reinterpret_cast<DescCtorFn>(ctor);
    s_ready = true;
    nbtPluginLog("player derivation OK: registerPlayer %p, onCarPlayStateChanged %p, ctor %p, vtable 0x%x",
                 (void*)reg, (void*)ocp, (void*)ctor, (unsigned)s_descVtap);
    return true;
}

int registerProjectionPlayer(void* resolver) {
    if (s_descriptor) return -1;                 // already registered
    if (!s_ready && !checkPlayerFirmware()) return -1;

    void* mm = findMainMenu(resolver);
    if (!mm) { nbtPluginLog("player: CHmiActivityMainMenu not located yet - will retry"); return 0; }
    s_mainMenu = mm;

    // Over-allocate and zero, then run iDrive's own constructor. Over-allocation means a larger-than-expected
    // object can never overflow; the RTTI re-check catches a wrong/incomplete construction before we register.
    void* desc = ::operator new(kDescAlloc);
    memset(desc, 0, kDescAlloc);
    s_descCtor(desc);
    const char* rn = safeRtti(s_im, desc);
    if (strcmp(rn, kDescRtti) != 0) {
        nbtPluginLog("player: descriptor RTTI '%s' != '%s' - abort (not registering)", rn, kDescRtti);
        ::operator delete(desc);
        return -1;
    }

    int rc = s_registerPlayer(mm, desc);          // hangs an internal wrapper (focusId 0x2535) in the registry
    s_descriptor = desc;                          // registry now holds a raw pointer -> keep alive forever
    nbtPluginLog("player: registerPlayer(mainMenu %p, desc %p) -> %d", mm, desc, rc);

    setProjectionActive(true);                    // drive activation ourselves (CarPlay service usually absent)
    return 1;
}

int setProjectionActive(bool active) {
    if (!s_ready) return -1;
    if (!s_mainMenu || !s_descriptor) { nbtPluginLog("player: setProjectionActive(%d) - no registered player", active); return 0; }
    s_onCarPlayState(s_mainMenu, active ? 1 : 0);
    nbtPluginLog("player: onCarPlayStateChanged(mainMenu %p, %d) called", s_mainMenu, active ? 1 : 0);
    return 1;
}

} // namespace hmi
} // namespace nbt

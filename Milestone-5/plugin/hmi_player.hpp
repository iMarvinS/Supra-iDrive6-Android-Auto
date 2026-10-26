// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// "Weg B": register a projection player with CHmiActivityMainMenu so the native HMI suppresses iDrive
// knob/button input while Android Auto is foreground (see ../../../aa-supra/focus_problem.md §3-§7d).
// Must be driven on the HMI thread (from the processing-unit timer, like the menu entry).
#ifndef NBT_AAD_HMI_PLAYER_HPP
#define NBT_AAD_HMI_PLAYER_HPP

namespace nbt {
namespace hmi {

// Verify the (non-exported) HmiMain3D functions the player path uses against this firmware build.
// Call once before registerProjectionPlayer / setProjectionActive. false (and logs) on any mismatch.
bool checkPlayerFirmware();

// Build a real CHmiCarPlayApplication descriptor with iDrive's own ctor, register it with the
// CHmiActivityMainMenu player registry (registerPlayer, type 0x10 / focusId 0x2535), then activate
// suppression (onCarPlayStateChanged). Requires the 0x5153 IVA gate set first (see setVariantIvaFlag).
//   1 = registered (+ activation attempted)
//   0 = mainMenu instance not found yet (safe to retry later)
//  -1 = firmware/RTTI check failed, or already registered (do not retry)
// The CHmiActivityMainMenu is reached from the resolver via IHmiHome + a pinned offset (see findMainMenu).
int registerProjectionPlayer(void* resolver);

// Toggle native-input suppression on the already-registered player (onCarPlayStateChanged(active)).
//   1 = toggled, 0 = no registered player / mainMenu, -1 = firmware check not passed.
int setProjectionActive(bool active);

} // namespace hmi
} // namespace nbt

#endif

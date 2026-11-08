# TF2VR Seated Controls

An unofficial seated-play add-on for CircuitLord's **Titanfall 2 VR** mod, designed around Quest controllers.

**Experimental:** loading and native hooks have been verified in a live Proton session. The latest Y-button fix and right-side secondary holster still need in-headset confirmation.

## Controls

- **Tap Y:** swap primary/secondary while holding a weapon.
- **Hold Y for 0.55 seconds:** open the menu with the default right-handed layout.
- **Secondary holster:** grab the same secondary weapon from either hip. An owned Arc Tool keeps priority at the right hip. Disable the extra point with `[holsters] secondary_right=0`.
- **Grenades:** larger chest pickup area, approximately 27 cm, with pickup feedback. Throwing still uses the original gesture.
- **Magazines:** additional grab points in front of your lower torso, with vibration feedback and no visible pouch models.
- **Manual reload:** more forgiving magazine alignment and insertion; reloading remains manual.
- **Crouch:** flick the right stick down to toggle. Press A to stand and then jump.

## Install

Requires the original TF2VR mod: **stable 1.0.25** or **multiplayer beta 1.1.32**. Other core builds keep their original controls.

1. Download the package ZIP from [Releases](https://github.com/ethanfel/TF2VRSeatedControls/releases).
2. Close the game. If upgrading, unregister and remove the old add-on first.
3. Extract into `<Titanfall 2>/TF2VR/packages/`, keeping the package's folder intact.
4. Launch once, quit, then launch again to activate the OpenXR layer.

You can register before launching with `TF2VRSeatedControlsSetup.exe register` from the package folder. **On Linux, run the helper in the game's Wine/Proton prefix.** No replacement core files or new controller bindings are needed.

## Settings and removal

Edit `plugins/layer/TF2VRSeatedControls.ini` inside the installed package, then restart. See the [commented settings file](config/TF2VRSeatedControls.ini) for grenade reach, magazine positions, reload tolerance, and individual toggles.

To remove: close the game, run `TF2VRSeatedControlsSetup.exe unregister` in the same prefix, then delete the package folder.

If something fails, include the add-on's `plugins/layer/TF2VRSeatedControls.log`, your TF2VR version, and your VR runtime in an issue.

## Build and credits

See [BUILD.md](BUILD.md) for source builds and checks.

Bootstrap code is adapted from Nighteyes' [TF2VRBetterBindings](https://github.com/Fdrph/TF2VRBetterBindings); native hooks use [MinHook](https://github.com/TsudaKageyu/minhook). See [third-party notices](THIRD-PARTY-NOTICES.md).

Copyright (c) 2026 Ethanfel and contributors. Licensed under the [GNU General Public License, version 3 only](LICENSE) (`GPL-3.0-only`). Third-party components retain their original licenses; see the [third-party notices](THIRD-PARTY-NOTICES.md).

Unaffiliated with CircuitLord, Respawn, EA, or Northstar.

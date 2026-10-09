# Changelog

## 0.3.2 — исправление установщика — 2026-10-10

- Исправлена установка на RE4 с 4GB-патчем.
- Оформление, музыка и игровая часть без изменений.

## 0.3.2 - 2026-10-09

- Redesigned the installer with a dark theme, drawn block/forest artwork, readable folder fields, responsive layout and .NET Framework 4.8 DPI support.
- Embedded the supplied RIVER SOLO audio with an on/off button; playback ends when the window closes and is absent from CLI operations.
- Clarified the displayed error for a missing game folder versus an unsupported executable. The original version check remains unchanged.
- The installation engine, native DLL, guest JAR, Fabric API, installation/uninstallation behavior and game mechanics are unchanged from 0.3.1 alpha. Installer2 is a presentation/audio update only.
- Added the DPI configuration and audio asset to the explicit release/source allowlists. The interface and music toggle were reviewed before publication.

## 0.3.1 - 2026-10-08

- F10 switches to native RE4 camera, controls and player collision, releases held Minecraft input, hides Minecraft drawing/lights, and reconnects at Leon's current position on return. A duplicated raw/legacy key event that instantly switched back was fixed; both switches were confirmed in gameplay.
- Minecraft HUD and world drawing stop during native movie/event/death cutscene flags. Every campaign cutscene is not yet tested.
- Conservative triangle rasterization keeps the native floor continuous for small physical props, including exactly on a region boundary. The same floor feeds block support checks for doors. Unit checks pass; broad room/item testing remains.
- Unchanged floor packets are cached, reducing repeated guest collision allocations. Runtime logs confirm skipped unchanged regions; large explosions can still be expensive.
- Placed torch/glowstone light records create native point lights on the game thread, with cleanup on removal, room change and F10. Native manager allocation is verified; materials/visual effects require further scene testing.
- Public WinForms installer/uninstaller, initial-file backups, rollback on copy failure, and preserved Minecraft worlds. Synthetic installation/update/uninstall/error tests pass.
- Free icon font, third-party notices, source instructions, Russian guide and explicit source-package allowlist.

## Earlier 0.3 work

- World projection follows the native queued camera and uses native world depth; near-plane clipping reduces sprint flicker.
- Explicit D3D texture state fixes white third-person armor. Color was confirmed in gameplay.
- Rendered arrows/items, scene shading and simplified projected block shadows.
- Native NPC movement is checked against Minecraft solid voxels; fast movement, sliding, landing and escape from overlapping blocks are tested.
- Lethal Minecraft hits set native damage latches so ordinary RE4 death initialization can run. Every enemy/death type is not verified.

The release remains an alpha. It does not claim complete Minecraft world interaction or a fully tested RE4 campaign.

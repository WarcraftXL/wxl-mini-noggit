# wxl-mini-noggit

An in-client map editor for the 3.3.5a client, built as a WarcraftXL module. It draws an ImGui overlay on
top of the running game, lets you select a placed doodad by clicking it, and moves it with a 3D transform
gizmo. Think of it as a tiny Noggit that lives inside the client instead of editing files offline.

> This is unstable and ships as an example of what a module can do, not as a finished tool.

## Using it

1. Enter the world, then press **Insert** to toggle the editor overlay.
2. Switch to the Noggit toolbar and click a doodad to select it. A wireframe box marks the selection.
3. Drag the gizmo to move, rotate, or scale the selected doodad. Changes are live in the world.

## How it works

- The overlay is ImGui, drawn each frame from the `OnEndScene` event; the gizmo is ImGuizmo.
- Selection projects each nearby doodad's real model bounds (read from its loaded model header) to the
  screen and picks the one under the cursor.
- Moving a doodad writes its live world matrix, so the change shows immediately. Saving placements back to
  the map files is not implemented yet.

## Acknowledgements

Thanks to the **[Duskhaven](https://git.duskhaven.net/Duskhaven)** team and their `dusk-tswow` client editor, whose in-client doodad editor was a useful reference for the engine-native way to select and place objects under the cursor. The two editors take different routes *(this module builds on ImGui/ImGuizmo and the WarcraftXL event SDK)*, but their work helped confirm the approach.

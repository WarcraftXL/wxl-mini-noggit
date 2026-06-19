// wxl-mini-noggit: an in-client ImGui overlay, the seed of a map / placement editor.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "events/Event.hpp"
#include "events/EventScript.hpp"

namespace wxl::scripts::mininoggit
{
    class MiniNoggit final : public wxl::events::EventScript
    {
    public:
        MiniNoggit();

        void OnEndScene(const wxl::events::EndSceneArgs& a);

    private:
        // One-time ImGui context + backend setup, deferred to the first frame with a live device.
        void EnsureInit(void* device);
        // The main inspector panel.
        void DrawPanel();
        // The Noggit-mode tool window (active only in edit mode).
        void DrawNoggitToolbar();
        // Right-click context menu on the selected doodad (gizmo mode submenu + deselect).
        void DrawContextMenu();
        // Wireframe box around the selected doodad, so the picked model is unambiguous.
        void DrawSelectionBox();
        // The 3D transform gizmo over the selected doodad (translate / rotate / scale).
        void DrawGizmo();
        // Resolve a screen-space click into the selected doodad.
        void DoPick(int sx, int sy);

        bool  init_     = false;
        bool  noggit_   = false;   // edit mode: shows the toolbar + click-to-select + gizmo
        void* selected_ = nullptr; // selected placed doodad (CMapDoodad), stable across frames
        // Active gizmo op, stored as a raw int to keep ImGuizmo's enum out of this header.
        // Default = TRANSLATE (ImGuizmo TRANSLATE_X|Y|Z = 1|2|4 = 7).
        int   gizmoOp_  = 7;
    };
}

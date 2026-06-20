// wxl-mini-noggit: ImGui overlay setup, window-input routing, and the inspector panel.
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

#include "MiniNoggit.hpp"

#include "core/Logger.hpp"
#include "game/Binding.hpp"
#include "game/camera/Camera.hpp"
#include "game/doodad/Doodad.hpp"
#include "game/ui/Ui.hpp"
#include "game/world/Loading.hpp"
#include "game/world/World.hpp"

#include <windows.h>
#include <d3d9.h>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "ImGuizmo.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace wxl::scripts::mininoggit
{
    namespace ev    = wxl::events;
    namespace world = wxl::game::world;

    namespace
    {
        HWND    g_hwnd        = nullptr;
        WNDPROC g_origWndProc = nullptr;
        bool    g_visible     = false;
        bool    g_noggit      = false;
        bool    g_pickPending = false;
        bool    g_menuPending = false; // a right-click asked for the doodad context menu
        int     g_pickX = 0, g_pickY = 0;
        bool    g_gizmoBusy   = false; // cursor is over / dragging the gizmo (set each frame)

        // The top-level visible window owned by this process. More robust than a window-class lookup.
        BOOL CALLBACK PickWindow(HWND h, LPARAM out)
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid == GetCurrentProcessId() && GetWindow(h, GW_OWNER) == nullptr && IsWindowVisible(h))
            {
                *reinterpret_cast<HWND*>(out) = h;
                return FALSE;
            }
            return TRUE;
        }

        HWND FindGameWindow()
        {
            HWND h = nullptr;
            EnumWindows(&PickWindow, reinterpret_cast<LPARAM>(&h));
            if (!h) h = FindWindowA("GxWindowClass", nullptr);
            return h;
        }

        // Window-input router: Insert toggles the panel; while open, messages the panel consumes are
        // swallowed so the game does not also react to them.
        LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
        {
            if (m == WM_KEYDOWN && w == VK_INSERT) { g_visible = !g_visible; return 0; }
            if (g_visible)
            {
                ImGui_ImplWin32_WndProcHandler(h, m, w, l);
                const ImGuiIO& io = ImGui::GetIO();
                // Edit mode: a left click in the world requests a doodad pick.
                if (g_noggit && m == WM_LBUTTONDOWN && !io.WantCaptureMouse)
                {
                    if (!g_gizmoBusy)
                    {
                        g_pickX = static_cast<short>(LOWORD(l));
                        g_pickY = static_cast<short>(HIWORD(l));
                        g_pickPending = true;
                    }
                    return 0;
                }
                // Edit mode: right click picks the doodad under the cursor and opens its context menu.
                if (g_noggit && m == WM_RBUTTONDOWN && !io.WantCaptureMouse)
                {
                    g_pickX = static_cast<short>(LOWORD(l));
                    g_pickY = static_cast<short>(HIWORD(l));
                    g_pickPending = true;
                    g_menuPending = true;
                    return 0;
                }
                // Swallow what the panel consumes (clicks, wheel, keys) but let WM_MOUSEMOVE through, so
                // WoW keeps driving its own native cursor and no second (ImGui) cursor appears.
                if (io.WantCaptureMouse && m >= WM_MOUSEFIRST && m <= WM_MOUSELAST && m != WM_MOUSEMOVE) return 0;
                if (io.WantCaptureKeyboard && ((m >= WM_KEYFIRST && m <= WM_KEYLAST) || m == WM_CHAR))   return 0;
            }
            return CallWindowProcA(g_origWndProc, h, m, w, l);
        }
    }

    MiniNoggit::MiniNoggit()
    {
        on<&MiniNoggit::OnEndScene>(ev::Event::OnEndScene);
    }

    void MiniNoggit::EnsureInit(void* device)
    {
        if (init_) return;
        g_hwnd = FindGameWindow();
        if (!g_hwnd || !device)
        {
            static bool warned = false;
            if (!warned) { warned = true; WLOG_WARN("mini-noggit: init deferred (hwnd=%p device=%p)", g_hwnd, device); }
            return;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX9_Init(reinterpret_cast<IDirect3DDevice9*>(device));
        ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

        g_origWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
        init_ = true;
        WLOG_INFO("mini-noggit: ImGui ready (hwnd=%p), Insert toggles the panel", g_hwnd);
    }

    void MiniNoggit::OnEndScene(const ev::EndSceneArgs& a)
    {
        EnsureInit(a.device);
        g_noggit = noggit_; // keep the input router in sync even when the toolbar is not drawn
        if (init_ && g_visible)
        {
            // Do not draw ImGui's software cursor: WoW's own cursor stays the single visible cursor.
            ImGui::GetIO().MouseDrawCursor = false;
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();
            // Resolve a pending world click into a doodad now that DisplaySize is valid for this frame.
            if (noggit_ && g_pickPending) { g_pickPending = false; DoPick(g_pickX, g_pickY); }
            if (noggit_ && g_menuPending) { g_menuPending = false; ImGui::OpenPopup("doodad_ctx"); }
            DrawPanel();
            if (noggit_)
            {
                DrawNoggitToolbar();
                DrawSelectionBox(); // wireframe first so the gizmo draws on top
                DrawGizmo();
                DrawContextMenu();
            }
            else g_gizmoBusy = false;
            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }
    }

    void MiniNoggit::DrawPanel()
    {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
        ImGui::Begin("WarcraftXL - Mini Noggit  (Insert to close)");

        ImGui::TextDisabled("%.1f FPS", io.Framerate);
        ImGui::Checkbox("Noggit mode (free edit)", &noggit_);
        ImGui::Separator();

        if (ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Player GUID : 0x%016llX", world::ActivePlayerGuid());
            ImGui::Text("Mouseover   : 0x%016llX", world::MouseoverGuid());
            ImGui::Text("Target      : 0x%016llX", world::TargetGuid());
            float pos[3];
            world::FocusPos(pos);
            ImGui::Text("Focus pos   : %.2f  %.2f  %.2f", pos[0], pos[1], pos[2]);
            ImGui::Text("Load active : %s", world::LoadActive() ? "yes" : "no");
        }

        if (ImGui::CollapsingHeader("Binding catalog"))
        {
            const auto catalog = wxl::game::Catalog();
            ImGui::Text("%d bindings", static_cast<int>(catalog.size()));
            if (ImGui::BeginTable("bindings", 3,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Address");
                ImGui::TableSetupColumn("Signature");
                ImGui::TableHeadersRow();
                for (const auto& b : catalog)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(b.name);
                    ImGui::TableNextColumn(); ImGui::Text("0x%08X", static_cast<unsigned>(b.address));
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(b.signature);
                }
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Map (placement editing)"))
        {
            ImGui::TextDisabled("WIP: ADT / WMO / M2 placement editing");
            ImGui::BulletText("reads wxl::game::adt / wxl::game::wmo");
            ImGui::BulletText("selection + GetObject from wxl::game::world");
        }

        ImGui::End();
    }

    namespace
    {
        // ImGuizmo operation bitmasks
        constexpr int kOpTranslate = ImGuizmo::TRANSLATE;
        constexpr int kOpRotate    = ImGuizmo::ROTATE;
        constexpr int kOpScale     = ImGuizmo::SCALE;
    }

    // Edit-mode tool window: left-click a doodad to select it, right-click it for the gizmo menu. The gizmo
    // itself is drawn over the world (DrawGizmo); this window just mirrors the mode and state.
    void MiniNoggit::DrawNoggitToolbar()
    {
        namespace doodad = wxl::game::doodad;

        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(480, 24), ImGuiCond_FirstUseEver);
        ImGui::Begin("Noggit - Tools");

        ImGui::TextDisabled("Left-click: select   Right-click: menu");
        ImGui::Separator();

        // Drop the selection if it no longer reads as a valid doodad (chunk unloaded / freed).
        if (selected_ && !doodad::IsValid(selected_)) selected_ = nullptr;

        if (selected_)
        {
            float p[3];
            doodad::Position(selected_, p);
            char name[260];
            const bool named = doodad::ModelName(selected_, name, sizeof name);
            ImGui::Text("Model: %s", named ? name : "(loading...)");
            ImGui::Text("Selected 0x%08X   scale %.2f",
                        static_cast<unsigned>(reinterpret_cast<uintptr_t>(selected_)), doodad::Scale(selected_));
            ImGui::Text("Pos  %.2f  %.2f  %.2f", p[0], p[1], p[2]);

            ImGui::TextDisabled("Gizmo");
            if (ImGui::RadioButton("Position",    gizmoOp_ == kOpTranslate)) gizmoOp_ = kOpTranslate;
            ImGui::SameLine();
            if (ImGui::RadioButton("Orientation", gizmoOp_ == kOpRotate))    gizmoOp_ = kOpRotate;
            ImGui::SameLine();
            if (ImGui::RadioButton("Size",        gizmoOp_ == kOpScale))     gizmoOp_ = kOpScale;

            if (ImGui::Button("Deselect")) selected_ = nullptr;
        }
        else
        {
            ImGui::TextDisabled("Nothing selected.");
        }

        ImGui::End();
    }

    // Right-click context menu on the selected doodad: choose the gizmo mode or deselect.
    void MiniNoggit::DrawContextMenu()
    {
        if (ImGui::BeginPopup("doodad_ctx"))
        {
            if (selected_)
            {
                if (ImGui::BeginMenu("Gizmo"))
                {
                    if (ImGui::MenuItem("Position",    nullptr, gizmoOp_ == kOpTranslate)) gizmoOp_ = kOpTranslate;
                    if (ImGui::MenuItem("Orientation", nullptr, gizmoOp_ == kOpRotate))    gizmoOp_ = kOpRotate;
                    if (ImGui::MenuItem("Size",        nullptr, gizmoOp_ == kOpScale))     gizmoOp_ = kOpScale;
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Deselect")) selected_ = nullptr;
            }
            else
            {
                ImGui::TextDisabled("Nothing under cursor.");
            }
            ImGui::EndPopup();
        }
    }

    // Project a world point through the camera's view-projection. Returns false if behind the camera. (thanks Claude for this)
    static bool WorldToScreen(const float* vp, const float w[3], float W, float H, ImVec2& out)
    {
        float clip[4];
        for (int j = 0; j < 4; ++j)
            clip[j] = w[0] * vp[j] + w[1] * vp[4 + j] + w[2] * vp[8 + j] + vp[12 + j];
        if (clip[3] <= 0.001f) return false;
        out.x = (clip[0] / clip[3] * 0.5f + 0.5f) * W;
        out.y = (0.5f - clip[1] / clip[3] * 0.5f) * H;
        return true;
    }

    // Transform a model-local point by a row-major (row-vector) instance matrix into world space. (thanks Claude for this)
    static void LocalToWorld(const float m[16], const float l[3], float w[3])
    {
        w[0] = l[0] * m[0] + l[1] * m[4] + l[2] * m[8]  + m[12];
        w[1] = l[0] * m[1] + l[1] * m[5] + l[2] * m[9]  + m[13];
        w[2] = l[0] * m[2] + l[1] * m[6] + l[2] * m[10] + m[14];
    }

    // Wireframe box around the selected doodad, built from the model's real local AABB (the MD20 header
    // bounds) transformed by the LIVE instance matrix, so it tracks the model's position / rotation / scale
    // and fits the actual mesh. Falls back to a small origin marker when the header bounds are not readable.
    void MiniNoggit::DrawSelectionBox()
    {
        namespace doodad = wxl::game::doodad;
        namespace camera = wxl::game::camera;
        if (!selected_) return;

        float m[16];
        if (!doodad::WorldMatrix(selected_, m)) return; // model not loaded yet

        // Real model-local extents from the MD20 header, or a small origin marker if not yet available.
        float lo[3], hi[3];
        if (!doodad::LocalBounds(selected_, lo, hi))
        {
            lo[0] = -2.0f; lo[1] = -2.0f; lo[2] = 0.0f;
            hi[0] =  2.0f; hi[1] =  2.0f; hi[2] = 6.0f;
        }

        const float* vp = camera::ViewProj();
        const ImGuiIO& io = ImGui::GetIO();
        const float W = io.DisplaySize.x, H = io.DisplaySize.y;

        ImVec2 s[8];
        bool ok[8];
        for (int k = 0; k < 8; ++k)
        {
            const float l[3] = { (k & 1) ? hi[0] : lo[0],
                                 (k & 2) ? hi[1] : lo[1],
                                 (k & 4) ? hi[2] : lo[2] };
            float w[3];
            LocalToWorld(m, l, w);
            ok[k] = WorldToScreen(vp, w, W, H, s[k]);
        }

        // 12 edges of the box (pairs of corner indices that differ in exactly one axis bit). (thanks Claude for this)
        static const int edges[12][2] = {
            {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const ImU32 col = IM_COL32(255, 210, 40, 230);
        for (auto& e : edges)
            if (ok[e[0]] && ok[e[1]]) dl->AddLine(s[e[0]], s[e[1]], col, 1.6f);
    }

    // The 3D transform gizmo over the selected doodad. Feeds the engine's row-major view/projection and the
    // doodad world matrix straight to ImGuizmo (same row-vector convention), writes the result back live.
    void MiniNoggit::DrawGizmo()
    {
        namespace doodad = wxl::game::doodad;
        namespace camera = wxl::game::camera;

        g_gizmoBusy = false;
        if (!selected_) return;

        float model[16];
        if (!doodad::WorldMatrix(selected_, model)) return;

        const ImGuiIO& io = ImGui::GetIO();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
        ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

        ImGuizmo::Manipulate(camera::View(), camera::Projection(),
                             static_cast<ImGuizmo::OPERATION>(gizmoOp_), ImGuizmo::WORLD, model);

        g_gizmoBusy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        if (ImGuizmo::IsUsing()) doodad::SetWorldMatrix(selected_, model);
    }

    // Resolve a screen click into a doodad: for each near-camera doodad, transform its real model-local AABB
    // by the live instance matrix, project the 8 world corners to the screen, and select the nearest one whose
    // projected rectangle contains the cursor. Rotation/scale are handled (all 8 corners are projected) and it
    // needs no matrix inverse and no engine call.
    void MiniNoggit::DoPick(int sx, int sy)
    {
        namespace doodad = wxl::game::doodad;
        namespace camera = wxl::game::camera;

        float camPos[3];
        camera::Position(camPos);
        void* list[512];
        const int n = doodad::EnumerateAround(camPos, 33.3f, list, 512);

        const float* vp = camera::ViewProj();
        const ImGuiIO& io = ImGui::GetIO();
        const float W = io.DisplaySize.x, H = io.DisplaySize.y;
        const float cx = static_cast<float>(sx), cy = static_cast<float>(sy);

        void* best = nullptr;
        float bestDepth = 1e18f, bestSpanX = 0.0f, bestSpanY = 0.0f;
        int boxed = 0; // doodads with a usable model AABB, to confirm the bounds are real
        for (int i = 0; i < n; ++i)
        {
            float m[16], lo[3], hi[3];
            if (!doodad::WorldMatrix(list[i], m)) continue;   // model still loading
            if (!doodad::LocalBounds(list[i], lo, hi)) continue; // no readable header bounds
            ++boxed;

            float rminx = 1e9f, rminy = 1e9f, rmaxx = -1e9f, rmaxy = -1e9f, depth = 1e18f;
            int frontCorners = 0;
            for (int k = 0; k < 8; ++k)
            {
                const float l[3] = { (k & 1) ? hi[0] : lo[0],
                                     (k & 2) ? hi[1] : lo[1],
                                     (k & 4) ? hi[2] : lo[2] };
                float c[3];
                LocalToWorld(m, l, c);
                float clip[4];
                for (int j = 0; j < 4; ++j)
                    clip[j] = c[0] * vp[j] + c[1] * vp[4 + j] + c[2] * vp[8 + j] + vp[12 + j];
                if (clip[3] <= 0.001f) continue;
                ++frontCorners;
                const float px = (clip[0] / clip[3] * 0.5f + 0.5f) * W;
                const float py = (0.5f - clip[1] / clip[3] * 0.5f) * H;
                rminx = (px < rminx) ? px : rminx;
                rmaxx = (px > rmaxx) ? px : rmaxx;
                rminy = (py < rminy) ? py : rminy;
                rmaxy = (py > rmaxy) ? py : rmaxy;
                depth = (clip[3] < depth) ? clip[3] : depth;
            }
            if (frontCorners < 4) continue; // box mostly behind the camera
            if (cx >= rminx && cx <= rmaxx && cy >= rminy && cy <= rmaxy && depth < bestDepth)
            {
                bestDepth = depth; best = list[i];
                bestSpanX = rmaxx - rminx; bestSpanY = rmaxy - rminy;
            }
        }
        WLOG_INFO("pick: cursor=%d,%d doodads=%d boxed=%d selected=%s span=%.0fx%.0f",
                  sx, sy, n, boxed, best ? "yes" : "no", bestSpanX, bestSpanY);
        if (best) selected_ = best;
    }

    // Self-registration: the file-scope instance binds its handlers at DLL load via the EventScript ctor.
    MiniNoggit g_miniNoggit;
}

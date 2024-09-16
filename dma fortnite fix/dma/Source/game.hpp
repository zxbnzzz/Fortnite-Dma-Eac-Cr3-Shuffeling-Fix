#include <Pch.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"
#include <util.hpp>
#include <settings.hpp>
#include <render.hpp>
#include <offsets.hpp>

struct EntityData {
    Vector2 bottom2d;
    Vector2 head2d;
    int box_width, box_height;
    float distance;
    uintptr_t mesh;
    Vector3 Velocity;
    Vector3 ReticleLocation;
    float boxLeft, boxRight;
};
std::vector<EntityData> entities;
std::atomic<bool> running(true);
std::atomic<int> player_count(0);
std::mutex data_mutex;

void bases() {
    while (running) {
        __int64 va_text = 0;
        for (int i = 0; i < 25; i++) {
            if (mem.Read<__int32>(cache::base + (i * 0x1000) + 0x250) == 0x260E020B) {
                va_text = cache::base + ((i + 1) * 0x1000);
            }
        }
        cache::uworld = mem.Read<__int64>(offsets::UWORLD + va_text);
        cache::game_instance = mem.Read<uintptr_t>(cache::uworld + offsets::GAME_INSTANCE);
        cache::local_players = mem.Read<uintptr_t>(mem.Read<uintptr_t>(cache::game_instance + offsets::LOCAL_PLAYERS));
        cache::player_controller = mem.Read<uintptr_t>(cache::local_players + offsets::PLAYER_CONTROLLER);
        cache::local_pawn = mem.Read<uintptr_t>(cache::player_controller + offsets::LOCAL_PAWN);

        if (cache::local_pawn) {
            cache::root_component = mem.Read<uintptr_t>(cache::local_pawn + offsets::ROOT_COMPONENT);
            cache::relative_location = mem.Read<Vector3>(cache::root_component + offsets::RELATIVE_LOCATION);
            cache::player_state = mem.Read<uintptr_t>(cache::local_pawn + offsets::PLAYER_STATE);
            cache::my_team_id = mem.Read<int>(cache::player_state + offsets::TEAM_INDEX);
        }

        cache::game_state = mem.Read<uintptr_t>(cache::uworld + offsets::GAME_STATE);
        cache::player_array = mem.Read<uintptr_t>(cache::game_state + offsets::PLAYER_ARRAY);
        player_count.store(mem.Read<int>(cache::game_state + (offsets::PLAYER_ARRAY + sizeof(uintptr_t))));
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    }
}

void actorloop() {
    while (running) {
        std::vector<EntityData> temp_entities;
        int count = player_count.load();
        temp_entities.reserve(count);

        for (int i = 0; i < count; i++) {
            uintptr_t player_state = mem.Read<uintptr_t>(cache::player_array + (i * sizeof(uintptr_t)));
            if (!player_state) continue;

            int player_team_id = mem.Read<int>(player_state + offsets::TEAM_INDEX);
            if (player_team_id == cache::my_team_id) continue;

            uintptr_t pawn_private = mem.Read<uintptr_t>(player_state + offsets::PAWN_PRIVATE);
            if (!pawn_private || pawn_private == cache::local_pawn) continue;

            uintptr_t mesh = mem.Read<uintptr_t>(pawn_private + offsets::MESH);
            if (!mesh) continue;

            Vector3 Velocity = mem.Read<Vector3>(cache::root_component + 0x168);

            Vector3 ReticleLocation = mem.Read<Vector3>(cache::player_controller + offsets::LocationUnderReticle);

            EntityData entity;
            entity.mesh = mesh;
            entity.Velocity = Velocity;
            entity.ReticleLocation = ReticleLocation;
            cache::player_count = count;
            temp_entities.push_back(entity);
        }
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            entities = std::move(temp_entities);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool IsShootable(Vector3 lur, Vector3 wl) {
    return (lur.x >= wl.x - 20 && lur.x <= wl.x + 20 && lur.y >= wl.y - 20 && lur.y <= wl.y + 20);
}

void draw_entities() {
    std::lock_guard<std::mutex> lock(data_mutex);

    float closest_distance = FLT_MAX;
    cache::closest_mesh = NULL;

    for (const auto& entity : entities) {
        Vector3 head3d = get_entity_bone(entity.mesh, bone::BONE_HEAD);
        Vector2 head2d = project_world_to_screen(head3d);
        Vector3 bottom3d = get_entity_bone(entity.mesh, 0);
        Vector2 bottom2d = project_world_to_screen(bottom3d);

        int box_height = abs(head2d.y - bottom2d.y);
        int box_width = static_cast<int>(box_height * 0.50f);
        float distance = cache::relative_location.distance(bottom3d) / 100.0f;

        ImColor box_color = is_visible(entity.mesh)
            ? ImColor(settings::visuals::boxColor[0], settings::visuals::boxColor[1], settings::visuals::boxColor[2], settings::visuals::boxColor[3])
            : ImColor(settings::visuals::boxColor2[0], settings::visuals::boxColor2[1], settings::visuals::boxColor2[2], settings::visuals::boxColor2[3]);

        if (settings::visuals::box) {
            draw_cornered_box(head2d.x - (box_width / 2), head2d.y, box_width, box_height, box_color, 1);
        }
        if (settings::visuals::fill_box) {
            ImColor fill_color = box_color;
            fill_color.Value.w = 0.5f;
            draw_filled_rect(head2d.x - (box_width / 2), head2d.y, box_width, box_height, fill_color);
        }
        if (settings::visuals::line) {
            draw_line(bottom2d, box_color);
        }
        if (settings::visuals::skeleton) {
            skeleton(entity.mesh, box_color);
        }
        if (settings::visuals::distance) {
            draw_distance(bottom2d, distance, ImColor(250, 250, 250, 250));
        }

        float fov_radius = settings::aimbot::fov;
        Vector2 screen_center = Vector2(settings::screen_center_x, settings::screen_center_y);

        double dx = head2d.x - screen_center.x;
        double dy = head2d.y - screen_center.y;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist <= fov_radius && dist < closest_distance) {
            cache::closest_distance = dist;
            cache::closest_mesh = entity.mesh;
        }

        if (settings::aimbot::enable && cache::closest_mesh) {
            if (cache::player_count > 1) {
                if (is_visible(entity.mesh)) {
                Vector3 Predictor = Prediction(head3d, entity.Velocity, cache::closest_distance, 70000);
                Vector2 hitbox_screen_predict = project_world_to_screen(Predictor);
                move(hitbox_screen_predict.x, hitbox_screen_predict.y);
                }
            }
        }
            if (settings::aimbot::triggerbot) {
            if (IsShootable(entity.ReticleLocation, head3d)) {
               kmBox::kmclick();
            }
            else
            {
                int isdown = 0;
                kmNet_mouse_left(isdown);
            }
        }
    }
}



WPARAM render_loop() {
    ZeroMemory(&messager, sizeof(MSG));
    while (messager.message != WM_QUIT) {
        if (PeekMessage(&messager, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&messager);
            DispatchMessage(&messager);
        }
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        POINT p;
        GetCursorPos(&p);
        io.MousePos.x = static_cast<float>(p.x);
        io.MousePos.y = static_cast<float>(p.y);
        io.MouseDown[0] = GetAsyncKeyState(VK_LBUTTON) != 0;
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        draw_entities();
        render_menu();
        ImGui::EndFrame();
        p_device->SetRenderState(D3DRS_ZENABLE, FALSE);
        p_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        p_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        p_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        if (p_device->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            p_device->EndScene();
        }
        HRESULT result = p_device->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST && p_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
            p_device->Reset(&p_params);
            ImGui_ImplDX9_CreateDeviceObjects();
        }
    }
    return messager.wParam;
}

bool init() {
    create_overlay();
    return SUCCEEDED(directx_init());
}
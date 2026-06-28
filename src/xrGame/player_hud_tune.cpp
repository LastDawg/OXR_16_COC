#include "StdAfx.h"
#include "player_hud.h"
#include "Level.h"
#include "debug_renderer.h"
#include "xrEngine/xr_input.h"
#include "HUDManager.h"
#include "HudItem.h"
#include "xrEngine/Effector.h"
#include "xrEngine/CameraManager.h"
#include "xrEngine/FDemoRecord.h"
#include "xrUICore/ui_base.h"
#include "debug_renderer.h"
#include "xrEngine/GameFont.h"
#include "player_hud_tune.h"

extern ENGINE_API float psHUD_FOV;

CHudTuner::CHudTuner()
{
    ImGui::SetCurrentContext(Device.GetImGuiContext());
    paused = fsimilar(Device.time_factor(), EPS);
}

bool CHudTuner::is_active() const
{
    return is_open() && Device.editor().IsActiveState();
}

void CHudTuner::ResetToDefaultValues()
{
    if (current_hud_item)
    {
        current_hud_item->reload_measures();
        curr_measures = current_hud_item->m_measures;
    }
    else
    {
        Fvector zero = { 0, 0, 0 };
        curr_measures.m_hands_attach[0] = zero;
        curr_measures.m_hands_attach[1] = zero;
        curr_measures.m_hands_offset[0][0] = zero;
        curr_measures.m_hands_offset[1][0] = zero;
        curr_measures.m_hands_offset[0][1] = zero;
        curr_measures.m_hands_offset[1][1] = zero;
        curr_measures.m_hands_offset[0][2] = zero;
        curr_measures.m_hands_offset[1][2] = zero;
        curr_measures.m_item_attach[0] = zero;
        curr_measures.m_item_attach[1] = zero;
        curr_measures.m_fire_point_offset = zero;
        curr_measures.m_fire_point2_offset = zero;
        curr_measures.m_shell_point_offset = zero;
    }

    new_measures = curr_measures;
}

void CHudTuner::UpdateValues()
{
    if (current_hud_item)
    {
        current_hud_item->m_measures = new_measures;
        // Запоминаем текущие правки для этой секции в памяти
        session_cache[current_hud_item->m_sect_name] = new_measures; 
    }
}

void CHudTuner::on_tool_frame()
{
    bool can_debug = strstr(Core.Params, "-dbg") || strstr(Core.Params, "-dev");
    if (!can_debug) return;

    if (!get_open_state())
    {
        if (paused)
        {
            paused = false;
            Device.time_factor(1.0f);
        }
        return;
    }

    if (!g_player_hud) return;

    auto calcColumnCount = [](float columnWidth) -> int
    {
        float windowWidth = ImGui::GetWindowWidth();
        return _max(1, static_cast<int>(windowWidth / columnWidth));
    };


auto hud_item = g_player_hud->attached_item(current_hud_idx);
    if (current_hud_item != hud_item)
    {
        current_hud_item = hud_item;
        
        if (current_hud_item)
        {
            // Проверяем, есть ли у нас в памяти уже настроенные данные для этой пушки
            auto it = session_cache.find(current_hud_item->m_sect_name);
            if (it != session_cache.end())
            {
                // Если есть — берем их из памяти
                new_measures = it->second;
                current_hud_item->m_measures = new_measures;
            }
            else
            {
                // Если нет — загружаем стандартные из LTX
                ResetToDefaultValues();
            }
        }
    }

    if (ImGui::Begin(tool_name(), &get_open_state(), get_default_window_flags()))
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::RadioButton("Pause", paused))
            {
                paused = !paused;
                Device.time_factor(paused ? EPS : 1.0f);
            }
            ImGui::EndMenuBar();
        }

        if (ImGui::CollapsingHeader("Main Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginCombo("Hud Item Mode", hud_item_mode[current_hud_idx]))
            {
                for (const auto& [idx, value] : hud_item_mode)
                {
                    if (ImGui::Selectable(value, current_hud_idx == idx))
                    {
                        current_hud_idx = idx;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::LabelText("Current item", "%s", hud_item ? hud_item->m_sect_name.c_str() : "none");

            ImGui::SliderFloat("HUD FOV", &psHUD_FOV, 0.1f, 1.0f);

            ImGui::NewLine();

            ImGui::SliderFloat("Position step", &_delta_pos, 0.0000001f, 0.001f, "%.7f");
            ImGui::SliderFloat("Rotation step", &_delta_rot, 0.000001f, 0.1f, "%.6f");

            ImGui::DragFloat3(hud_adj_modes[HUD_POS], (float*)&new_measures.m_hands_attach[0], _delta_pos, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[HUD_ROT], (float*)&new_measures.m_hands_attach[1], _delta_rot, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[HUD_POS_AIM], (float*)&new_measures.m_hands_offset[0][1], _delta_pos, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[HUD_ROT_AIM], (float*)&new_measures.m_hands_offset[1][1], _delta_rot, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[HUD_POS_GL], (float*)&new_measures.m_hands_offset[0][2], _delta_pos, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[HUD_ROT_GL], (float*)&new_measures.m_hands_offset[1][2], _delta_rot, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[ITEM_POS], (float*)&new_measures.m_item_attach[0], _delta_pos, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[ITEM_ROT], (float*)&new_measures.m_item_attach[1], _delta_rot, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[FIRE_POINT], (float*)&new_measures.m_fire_point_offset, _delta_pos, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[FIRE_POINT_2], (float*)&new_measures.m_fire_point2_offset, _delta_pos, 0.f, 0.f, "%.7f");
            ImGui::DragFloat3(hud_adj_modes[SHELL_POINT], (float*)&new_measures.m_shell_point_offset, _delta_pos, 0.f, 0.f, "%.7f");

            UpdateValues();

            string128 selectable;

            if (current_hud_item)
            {
                bool is_16x9 = UI().is_widescreen();
                shared_str m_sect_name = current_hud_item->m_sect_name;

                ImGuiIO& io = ImGui::GetIO();

if (ImGui::Button("Copy formatted values to clipboard"))
                {
                    // --- 1. Стандартная логика (Копирование в буфер обмена) ---
                    ImGui::LogToClipboard();
                    xr_sprintf(selectable, "[%s]\n", m_sect_name.c_str());
                    ImGui::LogText("%s", selectable);
                    
                    bool is_16x9 = UI().is_widescreen();
                    const char* s = is_16x9 ? "_16x9" : "";

                    // Функция-хелпер для форматирования строк (чтобы не дублировать код для файла и буфера)
                    auto LogValue = [&](const char* name, const char* suffix, Fvector& v) {
                        string128 line;
                        xr_sprintf(line, "%s%s = %f,%f,%f\n", name, suffix, v.x, v.y, v.z);
                        ImGui::LogText("%s", line);
                    };

                    LogValue("hands_position", s, new_measures.m_hands_attach[0]);
                    LogValue("hands_orientation", s, new_measures.m_hands_attach[1]);
                    LogValue("aim_hud_offset_pos", s, new_measures.m_hands_offset[0][1]);
                    LogValue("aim_hud_offset_rot", s, new_measures.m_hands_offset[1][1]);
                    LogValue("gl_hud_offset_pos", s, new_measures.m_hands_offset[0][2]);
                    LogValue("gl_hud_offset_rot", s, new_measures.m_hands_offset[1][2]);
                    LogValue("item_position", "", new_measures.m_item_attach[0]);
                    LogValue("item_orientation", "", new_measures.m_item_attach[1]);
                    LogValue("fire_point", "", new_measures.m_fire_point_offset);
                    LogValue("fire_point2", "", new_measures.m_fire_point2_offset);
                    LogValue("shell_point", "", new_measures.m_shell_point_offset);
                    
                    ImGui::LogFinish();

                    string_path file_path;
                    FS.update_path(file_path, "$app_data_root$", "player_hud_tune.ltx");

                    FILE* F = fopen(file_path, "a"); // Открываем для дозаписи
                    if (F)
                    {
                        fprintf(F, "[%s] ; Сохранено: %f\n", m_sect_name.c_str(), Device.fTimeGlobal);
                        fprintf(F, "hands_position%s    = %f,%f,%f\n", s, new_measures.m_hands_attach[0].x, new_measures.m_hands_attach[0].y, new_measures.m_hands_attach[0].z);
                        fprintf(F, "hands_orientation%s = %f,%f,%f\n", s, new_measures.m_hands_attach[1].x, new_measures.m_hands_attach[1].y, new_measures.m_hands_attach[1].z);
                        fprintf(F, "aim_hud_offset_pos%s = %f,%f,%f\n", s, new_measures.m_hands_offset[0][1].x, new_measures.m_hands_offset[0][1].y, new_measures.m_hands_offset[0][1].z);
                        fprintf(F, "aim_hud_offset_rot%s = %f,%f,%f\n", s, new_measures.m_hands_offset[1][1].x, new_measures.m_hands_offset[1][1].y, new_measures.m_hands_offset[1][1].z);
                        fprintf(F, "gl_hud_offset_pos%s  = %f,%f,%f\n", s, new_measures.m_hands_offset[0][2].x, new_measures.m_hands_offset[0][2].y, new_measures.m_hands_offset[0][2].z);
                        fprintf(F, "gl_hud_offset_rot%s  = %f,%f,%f\n", s, new_measures.m_hands_offset[1][2].x, new_measures.m_hands_offset[1][2].y, new_measures.m_hands_offset[1][2].z);
                        fprintf(F, "item_position       = %f,%f,%f\n", new_measures.m_item_attach[0].x, new_measures.m_item_attach[0].y, new_measures.m_item_attach[0].z);
                        fprintf(F, "item_orientation    = %f,%f,%f\n", new_measures.m_item_attach[1].x, new_measures.m_item_attach[1].y, new_measures.m_item_attach[1].z);
                        fprintf(F, "fire_point          = %f,%f,%f\n", new_measures.m_fire_point_offset.x, new_measures.m_fire_point_offset.y, new_measures.m_fire_point_offset.z);
                        fprintf(F, "fire_point2         = %f,%f,%f\n", new_measures.m_fire_point2_offset.x, new_measures.m_fire_point2_offset.y, new_measures.m_fire_point2_offset.z);
                        fprintf(F, "shell_point         = %f,%f,%f\n", new_measures.m_shell_point_offset.x, new_measures.m_shell_point_offset.y, new_measures.m_shell_point_offset.z);
                        fprintf(F, "\n");
                        fclose(F);
                    }
                }

                ImGui::NewLine();

#ifdef DEBUG
                firedeps fd;
                current_hud_item->setup_firedeps(fd);
                collide::rq_result& RQ = HUD().GetCurrentRayQuery();

                CDebugRenderer& render = Level().debug_renderer();

                ImGui::SliderFloat("Debug Point Size", &debug_point_size, 0.00005f, 1.f, "%.5f");

                if (ImGui::BeginTable("Show Debug Widgets", calcColumnCount(210.f)))
                {
                    ImGui::TableNextColumn();
                    if (ImGui::RadioButton("Draw Fire Point", draw_fp)) { draw_fp = !draw_fp; };
                    ImGui::TableNextColumn();
                    if (ImGui::RadioButton("Draw Fire Point (GL)", draw_fp2)) { draw_fp2 = !draw_fp2; }
                    ImGui::TableNextColumn();
                    if (ImGui::RadioButton("Draw Fire Direction", draw_fd)) { draw_fd = !draw_fd; }
                    ImGui::TableNextColumn();
                    if (ImGui::RadioButton("Draw Fire Direction (GL)", draw_fd2)) { draw_fd2 = !draw_fd2; }
                    ImGui::TableNextColumn();
                    if (ImGui::RadioButton("Draw Shell Point", draw_sp)) { draw_sp = !draw_sp; }
                    ImGui::EndTable();
                }

                if (draw_fp)
                {
                    Fvector point;
                    point.set(fd.vLastFP);
                    current_hud_item->m_parent_hud_item->TransformPosFromWorldToHud(point);
                    render.draw_aabb(point, debug_point_size, debug_point_size, debug_point_size, color_xrgb(255, 0, 0));
                }

                if (draw_fp2)
                {
                    Fvector point;
                    point.set(fd.vLastFP2);
                    current_hud_item->m_parent_hud_item->TransformPosFromWorldToHud(point);
                    render.draw_aabb(point, debug_point_size, debug_point_size, debug_point_size, color_xrgb(255, 0, 0));
                }

                if (draw_fd)
                {
                    Fvector point;
                    Fvector dir;
                    point.set(fd.vLastFP);
                    dir.set(fd.vLastFD);
                    current_hud_item->m_parent_hud_item->TransformPosFromWorldToHud(point);
                    current_hud_item->m_parent_hud_item->TransformDirFromWorldToHud(dir);

                    Fvector parallelPoint;
                    parallelPoint.set(point);
                    parallelPoint.mad(dir, RQ.range);
                    render.draw_aabb(parallelPoint, debug_point_size, debug_point_size, debug_point_size, color_xrgb(255, 0, 0));
                    render.draw_line(Fidentity, point, parallelPoint, color_xrgb(255, 0, 0));
                }

                if (draw_fd2)
                {
                    Fvector point;
                    Fvector dir;
                    point.set(fd.vLastFP2);
                    dir.set(fd.vLastFD);
                    current_hud_item->m_parent_hud_item->TransformPosFromWorldToHud(point);
                    current_hud_item->m_parent_hud_item->TransformDirFromWorldToHud(dir);

                    Fvector parallelPoint;
                    parallelPoint.set(point);
                    parallelPoint.mad(dir, RQ.range);
                    render.draw_aabb(parallelPoint, debug_point_size, debug_point_size, debug_point_size, color_xrgb(255, 0, 0));
                    render.draw_line(Fidentity, point, parallelPoint, color_xrgb(255, 0, 0));
                }

                if (draw_sp)
                {
                    Fvector point;
                    point.set(fd.vLastSP);
                    current_hud_item->m_parent_hud_item->TransformPosFromWorldToHud(point);
                    render.draw_aabb(point, debug_point_size, debug_point_size, debug_point_size, color_xrgb(255, 0, 0));
                }
#endif // DEBUG
            }

            if (ImGui::Button("Reset to default values"))
            {
                if (current_hud_item)
                {
                    // Удаляем пушку из кэша сессии
                    auto it = session_cache.find(current_hud_item->m_sect_name);
                    if (it != session_cache.end()) session_cache.erase(it);
                }
                ResetToDefaultValues();
            }
        }

        ImGui::NewLine();

        if (current_hud_item && ImGui::CollapsingHeader("Bone and Animation Debugging", ImGuiTreeNodeFlags_DefaultOpen))
        {
            IKinematics* ik = current_hud_item->m_model;
            ImGui::Text("Bone Count = %i", ik->LL_BoneCount());
            ImGui::Text("Root Bone = %s, ID: %i", ik->LL_BoneName_dbg(ik->LL_GetBoneRoot()), ik->LL_GetBoneRoot());

            if (ImGui::BeginTable("Bone Visibility", calcColumnCount(125.f)))
            {
                for (const auto& [bone_name, bone_id] : *ik->LL_Bones())
                {
                    if (bone_id == ik->LL_GetBoneRoot())
                        continue;

                    ImGui::TableNextColumn();
                    bool visible = ik->LL_GetBoneVisible(bone_id);
                    if (ImGui::RadioButton(bone_name.c_str(), visible)) { visible = !visible; };
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip("Bone Name = %s, ID: %i", bone_name.c_str(), bone_id);
                    }
                    ik->LL_SetBoneVisible(bone_id, visible, FALSE);
                }
                ImGui::EndTable();
            }

            ImGui::NewLine();
            if (ImGui::BeginTable("Animations", calcColumnCount(125.f)))
            {
                for (const auto& [anim_name, motion] : current_hud_item->m_hand_motions.m_anims)
                {
                    ImGui::TableNextColumn();
                    if (ImGui::Button(anim_name.c_str()))
                    {
                        current_hud_item->m_parent_hud_item->PlayHUDMotion_noCB(anim_name, false);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip("%s = %s, %s", anim_name.c_str(), motion.m_base_name.c_str(), motion.m_additional_name.c_str());
                    }
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

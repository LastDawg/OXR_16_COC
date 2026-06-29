#include "StdAfx.h"
#include "player_hud.h"
#include "HudItem.h"
#include "xrUICore/ui_base.h"
#include "Actor.h"
#include "physic_item.h"
#include "static_cast_checked.hpp"
#include "ActorEffector.h"
#include "WeaponMagazinedWGrenade.h" // XXX: move somewhere
#include "GamePersistent.h"

player_hud* g_player_hud = nullptr;
extern ENGINE_API shared_str current_player_hud_sect;

// --#SM+
#define PITCH_OFFSET_R		   0.0f		//0.017f Насколько сильно ствол смещается вбок (влево) при вертикальных поворотах камеры	--#SM+#--
#define PITCH_OFFSET_N		   0.0f		//0.012f Насколько сильно ствол поднимается\опускается при вертикальных поворотах камеры	--#SM+#--
#define PITCH_OFFSET_D		   0.02f    // Насколько сильно ствол приближается\отдаляется при вертикальных поворотах камеры --#SM+#--
#define PITCH_LOW_LIMIT		   -PI      // Минимальное значение pitch при использовании совместно с PITCH_OFFSET_N			--#SM+#--
#define TENDTO_SPEED           1.0f     // Модификатор силы инерции (больше - чувствительней)
#define TENDTO_SPEED_AIM       1.0f     // (Для прицеливания)
#define TENDTO_SPEED_RET       5.0f     // Модификатор силы отката инерции (больше - быстрее)
#define TENDTO_SPEED_RET_AIM   5.0f     // (Для прицеливания)
#define INERT_MIN_ANGLE        0.0f     // Минимальная сила наклона, необходимая для старта инерции
#define INERT_MIN_ANGLE_AIM    3.5f     // (Для прицеливания)

// Пределы смещения при инерции (лево / право / верх / низ)
#define ORIGIN_OFFSET          0.04f,  0.04f,  0.04f, 0.02f 
#define ORIGIN_OFFSET_AIM      0.015f, 0.015f, 0.01f, 0.005f   

// Outdated - old inertion
#define TENDTO_SPEED_OLD       5.f      // Скорость нормализации положения ствола
#define TENDTO_SPEED_AIM_OLD   8.f      // (Для прицеливания)
#define ORIGIN_OFFSET_OLD     -0.05f    // Фактор влияния инерции на положение ствола (чем меньше, тем маштабней инерция)
#define ORIGIN_OFFSET_AIM_OLD -0.03f    // (Для прицеливания)

float CalcMotionSpeed(const shared_str& anim_name, const float anim_speed)
{
    // Apply custom animation speeds / configuration only for singleplayer games.
    // Fast reloading / showing / hiding animation does not seem fair.
    if (IsGameTypeSingle())
        return anim_speed;
    else
        return (anim_name == "anm_show" || anim_name == "anm_hide") ? 2.0f : 1.0f;
}

const player_hud_motion* player_hud_motion_container::find_motion(const shared_str& name) const
{
    const auto it = m_anims.find(name);
    return it != m_anims.end() ? &it->second : nullptr;
}

void player_hud_motion_container::load(IKinematicsAnimated* model, const shared_str& sect)
{
    const CInifile::Sect& _sect = pSettings->r_section(sect);

    for (const auto& [name, anm] : _sect.Data)
    {
        if (0 == strncmp(name.c_str(), "anm_",  sizeof("anm_")  - 1) ||
            0 == strncmp(name.c_str(), "anim_", sizeof("anim_") - 1))
        {
            player_hud_motion pm;

            if (_GetItemCount(anm.c_str()) == 1)
            {
                pm.m_base_name = anm;
                pm.m_additional_name = anm;
                pm.m_anim_speed = 1.f;
            }
            else
            {
                R_ASSERT2(_GetItemCount(anm.c_str()) <= 3, anm.c_str());
                string512 str_item;
                _GetItem(anm.c_str(), 0, str_item);
                pm.m_base_name = str_item;

                _GetItem(anm.c_str(), 1, str_item);
                pm.m_additional_name = xr_strlen(str_item) > 0 ? str_item : pm.m_base_name;

                _GetItem(anm.c_str(), 2, str_item);
                pm.m_anim_speed = xr_strlen(str_item) > 0 ? atof(str_item) : 1.f;
            }

            // and load all motions for it
            for (u32 i = 0; i <= 8; ++i)
            {
                string512 buff;
                if (i == 0)
                    xr_strcpy(buff, pm.m_base_name.c_str());
                else
                    xr_sprintf(buff, "%s%d", pm.m_base_name.c_str(), i);

                MotionID motion_ID = model->ID_Cycle_Safe(buff);


				if (!motion_ID.valid() && i == 0)
                {
                    motion_ID = model->ID_Cycle_Safe("hand_idle_doun");
                }

                if (motion_ID.valid())
                {
                    pm.m_animations.emplace_back(motion_descr{ std::move(motion_ID), buff });
#ifdef DEBUG
//					Msg(" alias=[%s] base=[%s] name=[%s]",pm.m_alias_name.c_str(), pm.m_base_name.c_str(), buff);
#endif // #ifdef DEBUG
                }
            }
            VERIFY2(pm.m_animations.size(), make_string("motion not found [%s]", pm.m_base_name.c_str()).c_str());

            m_anims.emplace(name, std::move(pm));
        }
    }
}

Fvector& attachable_hud_item::hands_attach_pos() { return m_measures.m_hands_attach[0]; }
Fvector& attachable_hud_item::hands_attach_rot() { return m_measures.m_hands_attach[1]; }

Fvector& attachable_hud_item::hands_offset_pos()
{
    const u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
    return m_measures.m_hands_offset[0][idx];
}

Fvector& attachable_hud_item::hands_offset_rot()
{
    u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
    return m_measures.m_hands_offset[1][idx];
}

void attachable_hud_item::set_bone_visible(const shared_str& bone_name, BOOL bVisibility, BOOL bSilent)
{
    const u16 bone_id = m_model->LL_BoneID(bone_name);
    if (bone_id == BI_NONE)
    {
        if (bSilent)
            return;

        // Вектор для хранения уникальных пар (модель + кость), о которых уже ругались
        static xr_vector<std::pair<const char*, const char*>> warned_bones;

        auto cache_pair = std::make_pair(m_visual_name.c_str(), bone_name.c_str());

        if (std::find(warned_bones.begin(), warned_bones.end(), cache_pair) == warned_bones.end())
        {
            warned_bones.push_back(cache_pair);
            Msg("! WARNING: model [%s] has no bone [%s]. Ignored.", m_visual_name.c_str(), bone_name.c_str());
        }
        return;
    }
    const BOOL bVisibleNow = m_model->LL_GetBoneVisible(bone_id);
    if (bVisibleNow != bVisibility)
        m_model->LL_SetBoneVisible(bone_id, bVisibility, TRUE);
}

void attachable_hud_item::update(bool bForce)
{
    if (!bForce && m_upd_firedeps_frame == Device.dwFrame)
        return;

    const bool is_16x9 = UICore::is_widescreen();

    if (m_measures.m_prop_flags.test(hud_item_measures::e_16x9_mode_now) != is_16x9)
    {
        reload_measures();
    }

    if (GamePersistent().GetHudTuner().is_active())
        m_measures.update(m_attach_offset);

    m_parent->calc_transform(m_attach_place_idx, m_attach_offset, m_item_transform);
    m_upd_firedeps_frame = Device.dwFrame;

    if (IKinematicsAnimated* ka = m_model->dcast_PKinematicsAnimated())
    {
        ka->UpdateTracks();
        ka->dcast_PKinematics()->CalculateBones_Invalidate();
        ka->dcast_PKinematics()->CalculateBones(TRUE);
    }
}

void attachable_hud_item::update_hud_additional(Fmatrix& trans) const
{
    if (m_parent_hud_item)
    {
        m_parent_hud_item->UpdateHudAdditional(trans);
    }
}

void attachable_hud_item::setup_firedeps(firedeps& fd)
{
    update(false);
    // fire point&direction
    if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point))
    {
        Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone);
        fire_mat.transform_tiny(fd.vLastFP, m_measures.m_fire_point_offset);
        m_item_transform.transform_tiny(fd.vLastFP);

        fd.vLastFD.set(0.f, 0.f, 1.f);
        m_item_transform.transform_dir(fd.vLastFD);
        VERIFY(_valid(fd.vLastFD));
        VERIFY(_valid(fd.vLastFD));

        fd.m_FireParticlesXForm.identity();
        fd.m_FireParticlesXForm.k.set(fd.vLastFD);
        Fvector::generate_orthonormal_basis_normalized(
            fd.m_FireParticlesXForm.k, fd.m_FireParticlesXForm.j, fd.m_FireParticlesXForm.i);
        VERIFY(_valid(fd.m_FireParticlesXForm));
    }

    if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point2))
    {
        Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone2);
        fire_mat.transform_tiny(fd.vLastFP2, m_measures.m_fire_point2_offset);
        m_item_transform.transform_tiny(fd.vLastFP2);
        VERIFY(_valid(fd.vLastFP2));
        VERIFY(_valid(fd.vLastFP2));
    }

    if (m_measures.m_prop_flags.test(hud_item_measures::e_shell_point))
    {
        Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_shell_bone);
        fire_mat.transform_tiny(fd.vLastSP, m_measures.m_shell_point_offset);
        m_item_transform.transform_tiny(fd.vLastSP);
        VERIFY(_valid(fd.vLastSP));
        VERIFY(_valid(fd.vLastSP));
    }
}

bool attachable_hud_item::need_renderable() const { return m_parent_hud_item->need_renderable(); }

void attachable_hud_item::render(u32 context_id, IRenderable* root)
{
    GEnv.Render->add_Visual(context_id, root, m_model->dcast_RenderVisual(), m_item_transform);
    m_parent_hud_item->render_hud_mode();
}

bool attachable_hud_item::render_item_ui_query() const { return m_parent_hud_item->render_item_3d_ui_query(); }
void attachable_hud_item::render_item_ui() const { m_parent_hud_item->render_item_3d_ui(); }

float CalculateHudAspectX(float x, bool source_is_16x9)
{
    float current_aspect = Device.fASPECT;
    if (current_aspect < 0.1f) current_aspect = 1.333f;

    float target_aspect  = 16.0f / 9.0f; // 1.777
    float square_aspect  = 4.0f / 3.0f;  // 1.333

    if (source_is_16x9)
        return x * (current_aspect / target_aspect);
    else
        return x * (current_aspect / square_aspect);
}

Fvector SafeLoadVector(const shared_str& sect, const char* base_name, bool is_16x9, const CInifile* config = pSettings)
{
    Fvector res = { 0.f, 0.f, 0.f };
    string128 name_normal, name_16x9;
    xr_strcpy(name_normal, base_name);
    xr_sprintf(name_16x9, "%s_16x9", base_name);

    bool has_normal = config->line_exist(sect, name_normal);
    bool has_16x9   = config->line_exist(sect, name_16x9);

    if (is_16x9)
    {
        if (has_16x9) 
        {
            return config->r_fvector3(sect, name_16x9);
        }
        else if (has_normal)
        {
            res = config->r_fvector3(sect, name_normal);
            res.x = CalculateHudAspectX(res.x, false); 
            return res;
        }
    }
    else
    {
        if (has_normal) 
        {
            return config->r_fvector3(sect, name_normal);
        }
        else if (has_16x9)
        {
            res = config->r_fvector3(sect, name_16x9);
            res.x = CalculateHudAspectX(res.x, true);
            return res;
        }
    }

    return res;
}

Fmatrix hud_item_measures::load(const shared_str& sect_name, IKinematics* K)
{
    const bool is_16x9 = UICore::is_widescreen();
    m_prop_flags.set(e_16x9_mode_now, is_16x9);

    // Безопасная загрузка рук
    m_hands_attach[0] = SafeLoadVector(sect_name, "hands_position", is_16x9);
    m_hands_attach[1] = SafeLoadVector(sect_name, "hands_orientation", is_16x9);

    m_item_attach[0] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, "item_position", Fvector().set(0,0,0));
    m_item_attach[1] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, "item_orientation", Fvector().set(0,0,0));

    Fmatrix attach_offset;
    update(attach_offset);

    // Загрузка костей (fire, shell)
    shared_str bone_name;
    m_prop_flags.set(e_fire_point, pSettings->line_exist(sect_name, "fire_bone"));
    if (m_prop_flags.test(e_fire_point)) {
        bone_name = pSettings->r_string(sect_name, "fire_bone");
        m_fire_bone = K->LL_BoneID(bone_name);
        m_fire_point_offset = pSettings->r_fvector3(sect_name, "fire_point");
    }

    m_prop_flags.set(e_fire_point2, pSettings->line_exist(sect_name, "fire_bone2"));
    if (m_prop_flags.test(e_fire_point2)) {
        bone_name = pSettings->r_string(sect_name, "fire_bone2");
        m_fire_bone2 = K->LL_BoneID(bone_name);
        m_fire_point2_offset = pSettings->r_fvector3(sect_name, "fire_point2");
    }

    m_prop_flags.set(e_shell_point, pSettings->line_exist(sect_name, "shell_bone"));
    if (m_prop_flags.test(e_shell_point)) {
        bone_name = pSettings->r_string(sect_name, "shell_bone");
        m_shell_bone = K->LL_BoneID(bone_name);
        m_shell_point_offset = pSettings->r_fvector3(sect_name, "shell_point");
    }

    // Загрузка прицеливания (Aim)
    m_hands_offset[0][0] = {0,0,0}; // Idle
    m_hands_offset[1][0] = {0,0,0};

    m_hands_offset[0][1] = SafeLoadVector(sect_name, "aim_hud_offset_pos", is_16x9);
    m_hands_offset[1][1] = SafeLoadVector(sect_name, "aim_hud_offset_rot", is_16x9);

    // Подствольник (GL)
    m_hands_offset[0][2] = SafeLoadVector(sect_name, "gl_hud_offset_pos", is_16x9);
    m_hands_offset[1][2] = SafeLoadVector(sect_name, "gl_hud_offset_rot", is_16x9);

    // Коллизия (Collision)
    if (READ_IF_EXISTS(pSettings, r_bool, sect_name, "hud_collision_enabled", false)) 
    {
        m_collision_offset[0] = SafeLoadVector(sect_name, "hud_collision_offset_pos", is_16x9);
        m_collision_offset[1] = SafeLoadVector(sect_name, "hud_collision_offset_rot", is_16x9);
    }

    load_inertion_params(sect_name);
    return attach_offset;
}

Fmatrix hud_item_measures::load_monolithic(const shared_str& sect_name, IKinematics* K, CHudItem* owner)
{
    const bool is_16x9 = UICore::is_widescreen();
    m_prop_flags.set(e_16x9_mode_now, is_16x9);

    // 1. Позиция и ориентация предмета (используем SafeLoadVector для авто-аспекта)
    m_item_attach[0] = SafeLoadVector(sect_name, "position", is_16x9);
    m_item_attach[1] = SafeLoadVector(sect_name, "orientation", is_16x9);

    Fmatrix attach_offset;
    update(attach_offset);

    // 2. Работа с костями оружия
    if (auto* wpn = smart_cast<CWeapon*>(owner))
    {
        // Безопасный поиск кости огня
        if (pSettings->line_exist(sect_name, "fire_bone")) 
        {
            cpcstr fire_bone_name = pSettings->r_string(sect_name, "fire_bone");
            m_fire_bone = K->LL_BoneID(fire_bone_name);
            if (m_fire_bone == BI_NONE) {
                Msg("! [HUD-SAFE] Bone [%s] not found in [%s]. Using root bone.", fire_bone_name, sect_name.c_str());
                m_fire_bone = K->LL_GetBoneRoot();
            }
        }
        else {
            m_fire_bone = K->LL_GetBoneRoot();
        }

        m_fire_bone2 = m_fire_bone;
        m_shell_bone = m_fire_bone;

        m_fire_point_offset = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, "fire_point", Fvector().set(0,0,0));
        m_fire_point2_offset = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, "fire_point2", m_fire_point_offset);

        if (pSettings->line_exist(owner->object().cNameSect(), "shell_particles"))
            m_shell_point_offset = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, "shell_point", Fvector().set(0,0,0));
        else
            m_shell_point_offset.set(0, 0, 0);

        m_hands_offset[0][0] = {0,0,0};
        m_hands_offset[1][0] = {0,0,0};

        if (wpn->IsZoomEnabled())
        {
            // Умная лямбда для загрузки зум-оффсетов (позиция + вращение)
            auto load_zoom_safe = [&](const char* prefix, int idx) 
            {
                string256 pos_name;
                strconcat(sizeof(pos_name), pos_name, prefix, "zoom_offset");
                
                // Загружаем позицию с авто-скейлом X под 21:9 / 4:3
                m_hands_offset[0][idx] = SafeLoadVector(sect_name, pos_name, is_16x9);

                // Загружаем ротацию (в монолите это 3 отдельных флоата)
                // Проверяем наличие _16x9 суффикса и для них
                auto GetRot = [&](const char* axis) {
                    string128 rot_name;
                    xr_sprintf(rot_name, "%szoom_rotate_%s%s", prefix, axis, is_16x9 ? "_16x9" : "");
                    if (!pSettings->line_exist(sect_name, rot_name))
                        xr_sprintf(rot_name, "%szoom_rotate_%s", prefix, axis);
                    return READ_IF_EXISTS(pSettings, r_float, sect_name, rot_name, 0.f);
                };

                m_hands_offset[1][idx].x = GetRot("x");
                m_hands_offset[1][idx].y = GetRot("y");
                m_hands_offset[1][idx].z = GetRot("z");
            };

            load_zoom_safe("", 1); // Обычный прицел
            if (smart_cast<CWeaponMagazinedWGrenade*>(wpn))
            {
                load_zoom_safe("grenade_", 2); // Прицел подствольника
                if (wpn->GrenadeLauncherAttachable())
                    load_zoom_safe("grenade_normal_", 1);
            }
        }
    }
    else
    {
        m_fire_bone = m_fire_bone2 = m_shell_bone = BI_NONE;
        m_fire_point_offset = m_fire_point2_offset = m_shell_point_offset = {};
    }

    load_inertion_params(sect_name);
    return attach_offset;
}

void hud_item_measures::load_inertion_params(const shared_str& sect_name)
{
	//Загрузка параметров инерции
	m_inertion_params.m_pitch_offset_r = READ_IF_EXISTS(pSettings, r_float, sect_name, "pitch_offset_right", PITCH_OFFSET_R);
	m_inertion_params.m_pitch_offset_n = READ_IF_EXISTS(pSettings, r_float, sect_name, "pitch_offset_up", PITCH_OFFSET_N);
	m_inertion_params.m_pitch_offset_d = READ_IF_EXISTS(pSettings, r_float, sect_name, "pitch_offset_forward", PITCH_OFFSET_D);
	m_inertion_params.m_pitch_low_limit = READ_IF_EXISTS(pSettings, r_float, sect_name, "pitch_offset_up_low_limit", PITCH_LOW_LIMIT);

	m_inertion_params.m_origin_offset = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_origin_offset", ORIGIN_OFFSET_OLD);
	m_inertion_params.m_origin_offset_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_origin_aim_offset", ORIGIN_OFFSET_AIM_OLD);
	m_inertion_params.m_tendto_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_speed", TENDTO_SPEED);
	m_inertion_params.m_tendto_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_aim_speed", TENDTO_SPEED_AIM);

	m_inertion_params.m_tendto_ret_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_ret_speed", TENDTO_SPEED_RET);
	m_inertion_params.m_tendto_ret_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_ret_aim_speed", TENDTO_SPEED_RET_AIM);

	m_inertion_params.m_min_angle = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_min_angle", INERT_MIN_ANGLE);
	m_inertion_params.m_min_angle_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_min_angle_aim", INERT_MIN_ANGLE_AIM);

	m_inertion_params.m_offset_LRUD = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "inertion_offset_LRUD", Fvector4().set(ORIGIN_OFFSET));
	m_inertion_params.m_offset_LRUD_aim = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "inertion_offset_LRUD_aim", Fvector4().set(ORIGIN_OFFSET_AIM));

	// Загрузка параметров смещения при стрельбе
    m_shooting_params.bShootShake = READ_IF_EXISTS(pSettings, r_bool, sect_name, "shooting_hud_effect", false);
    m_shooting_params.m_shot_max_offset_LRUD = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "shooting_max_LRUD", Fvector4().set(0, 0, 0, 0));
    m_shooting_params.m_shot_max_offset_LRUD_aim = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "shooting_max_LRUD_aim", Fvector4().set(0, 0, 0, 0));
    m_shooting_params.m_shot_offset_BACKW = READ_IF_EXISTS(pSettings, r_fvector2, sect_name, "shooting_backward_offset", Fvector2().set(0, 0));
    m_shooting_params.m_ret_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "shooting_ret_speed", 1.0f);
    m_shooting_params.m_ret_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "shooting_ret_aim_speed", 1.0f);
    m_shooting_params.m_min_LRUD_power = READ_IF_EXISTS(pSettings, r_float, sect_name, "shooting_min_LRUD_power", 0.0f);
}

void hud_item_measures::update(Fmatrix& attach_offset)
{
    Fvector ypr = m_item_attach[1];
    ypr.mul(PI / 180.f);
    attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
    attach_offset.translate_over(m_item_attach[0]);
}

attachable_hud_item::~attachable_hud_item()
{
    IRenderVisual* v = m_model->dcast_RenderVisual();
    GEnv.Render->model_Delete(v);
}

attachable_hud_item::attachable_hud_item(player_hud* parent, const shared_str& sect_name, IKinematicsAnimated* hands_model)
    : m_parent(parent), m_sect_name(sect_name)
{
    // Visual
    if (pSettings->line_exist(m_sect_name, "item_visual"))
    {
        m_monolithic = false;
        m_visual_name = pSettings->r_string(m_sect_name, "item_visual");
    }
    else if (pSettings->line_exist(m_sect_name, "visual"))
    {
        m_monolithic = true;
        m_visual_name = pSettings->r_string(m_sect_name, "visual");
    }
    R_ASSERT3(!m_visual_name.empty(), "Missing 'item_visual' from weapon hud section.", m_sect_name.c_str());

    m_model = smart_cast<IKinematics*>(GEnv.Render->model_Create(m_visual_name.c_str()));

    m_attach_place_idx = pSettings->read_if_exists<u16>(m_sect_name, "attach_place_idx", 0);

    IKinematicsAnimated* animatedHudItem;
    if (!m_monolithic && hands_model)
        animatedHudItem = hands_model;
    else
        animatedHudItem = smart_cast<IKinematicsAnimated*>(m_model);

    m_hand_motions.load(animatedHudItem, m_sect_name);
    reload_measures();
}

void attachable_hud_item::reload_measures()
{
    if (m_monolithic)
        m_attach_offset = m_measures.load_monolithic(m_sect_name, m_model, m_parent_hud_item);
    else
        m_attach_offset = m_measures.load(m_sect_name, m_model);
}

u32 attachable_hud_item::anim_play(const shared_str& anm_name_b, BOOL bMixIn, const CMotionDef*& md, u8& rnd_idx)
{
    string256 anim_name_r;
    const bool is_16x9 = UICore::is_widescreen();
    xr_sprintf(anim_name_r, "%s%s", anm_name_b.c_str(), m_attach_place_idx == 1 && is_16x9 ? "_16x9" : "");

    const player_hud_motion* anm = m_hand_motions.find_motion(anim_name_r);
    R_ASSERT2(anm, make_string("model [%s] has no motion alias defined [%s]", m_sect_name.c_str(), anim_name_r).c_str());
    R_ASSERT2(anm->m_animations.size(), make_string("model [%s] has no motion defined in motion_alias [%s]",
                                            m_visual_name.c_str(), anim_name_r)
                                            .c_str());

    const float speed = CalcMotionSpeed(anm->m_base_name, anm->m_anim_speed);

    rnd_idx = (u8)Random.randI(anm->m_animations.size());
    const motion_descr& M = anm->m_animations[rnd_idx];

    IKinematicsAnimated* ka = smart_cast<IKinematicsAnimated*>(m_model);
    const u32 ret = m_parent->anim_play(m_attach_place_idx, M.mid, bMixIn, md, speed);

    if (ka)
    {
        shared_str item_anm_name;
        if (anm->m_base_name != anm->m_additional_name)
            item_anm_name = anm->m_additional_name;
        else
            item_anm_name = M.name;

        MotionID M2 = ka->ID_Cycle_Safe(item_anm_name);
        if (!M2.valid())
            M2 = ka->ID_Cycle_Safe("idle");
        else if (bDebug)
            Msg("playing item animation [%s]", item_anm_name.c_str());

        R_ASSERT3(M2.valid(), "model has no motion [idle] ", m_visual_name.c_str());

        if (!m_monolithic)
        {
            const u16 root_id = m_model->LL_GetBoneRoot();
            CBoneInstance& root_binst = m_model->LL_GetBoneInstance(root_id);
            root_binst.set_callback_overwrite(TRUE);
            root_binst.mTransform.identity();
        }

        const u16 pc = ka->partitions().count();
        for (u16 pid = 0; pid < pc; ++pid)
        {
            CBlend* B = ka->PlayCycle(pid, M2, bMixIn);
            R_ASSERT(B);
            B->speed *= speed;
        }

        m_model->CalculateBones_Invalidate();
    }

    R_ASSERT2(m_parent_hud_item, "parent hud item is NULL");
    CPhysicItem& parent_object = m_parent_hud_item->object();
    // R_ASSERT2		(parent_object, "object has no parent actor");
    // IGameObject*		parent_object = static_cast_checked<IGameObject*>(&m_parent_hud_item->object());

    if (IsGameTypeSingle() && parent_object.H_Parent() == Level().CurrentControlEntity())
    {
        CActor* current_actor = static_cast_checked<CActor*>(Level().CurrentControlEntity());
        VERIFY(current_actor);

        string_path ce_path;
        string_path anm_name;
        strconcat(anm_name, "camera_effects" DELIMITER "weapon" DELIMITER, M.name.c_str(), ".anm");
        if (FS.exist(ce_path, "$game_anims$", anm_name))
        {
            CEffectorCam* ec = current_actor->Cameras().GetCamEffector(eCEWeaponAction);
            if (ec)
                current_actor->Cameras().RemoveCamEffector(eCEWeaponAction);

            CAnimatorCamEffector* e = xr_new<CAnimatorCamEffector>();
            e->SetType(eCEWeaponAction);
            e->SetHudAffect(false);
            e->SetCyclic(false);
            e->Start(anm_name);
            current_actor->Cameras().AddCamEffector(e);
        }
    }
    return ret;
}

player_hud::~player_hud()
{
    if (m_model)
    {
        IRenderVisual* v = m_model->dcast_RenderVisual();
        GEnv.Render->model_Delete(v);
    }

    if (m_model_2)
    {
        IRenderVisual* v = m_model_2->dcast_RenderVisual();
        GEnv.Render->model_Delete(v);
    }

    for (auto& [name, item] : m_pool)
    {
        xr_delete(item);
    }
    m_pool.clear();
}

void player_hud::Thumb0Callback(CBoneInstance* B)
{
    player_hud* P = static_cast<player_hud*>(B->callback_param());

    Fvector& target = P->target_thumb0rot;
    Fvector& current = P->thumb0rot;

    if (!target.similar(current))
    {
        Fvector diff[2];
        diff[0] = target;
        diff[0].sub(current);
        diff[0].mul(Device.fTimeDelta / .1f);
        current.add(diff[0]);
    }
    else
        current.set(target);

    Fmatrix rotation;
    rotation.identity();
    rotation.rotateX(current.x);

    Fmatrix rotation_y;
    rotation_y.identity();
    rotation_y.rotateY(current.y);
    rotation.mulA_43(rotation_y);

    rotation_y.identity();
    rotation_y.rotateZ(current.z);
    rotation.mulA_43(rotation_y);

    B->mTransform.mulB_43(rotation);
}

void player_hud::Thumb01Callback(CBoneInstance* B)
{
    player_hud* P = static_cast<player_hud*>(B->callback_param());

    Fvector& target = P->target_thumb01rot;
    Fvector& current = P->thumb01rot;

    if (!target.similar(current))
    {
        Fvector diff[2];
        diff[0] = target;
        diff[0].sub(current);
        diff[0].mul(Device.fTimeDelta / .1f);
        current.add(diff[0]);
    }
    else
        current.set(target);

    Fmatrix rotation;
    rotation.identity();
    rotation.rotateX(current.x);

    Fmatrix rotation_y;
    rotation_y.identity();
    rotation_y.rotateY(current.y);
    rotation.mulA_43(rotation_y);

    rotation_y.identity();
    rotation_y.rotateZ(current.z);
    rotation.mulA_43(rotation_y);

    B->mTransform.mulB_43(rotation);
}

void player_hud::Thumb02Callback(CBoneInstance* B)
{
    player_hud* P = static_cast<player_hud*>(B->callback_param());

    Fvector& target = P->target_thumb02rot;
    Fvector& current = P->thumb02rot;

    if (!target.similar(current))
    {
        Fvector diff[2];
        diff[0] = target;
        diff[0].sub(current);
        diff[0].mul(Device.fTimeDelta / .1f);
        current.add(diff[0]);
    }
    else
        current.set(target);

    Fmatrix rotation;
    rotation.identity();
    rotation.rotateX(current.x);

    Fmatrix rotation_y;
    rotation_y.identity();
    rotation_y.rotateY(current.y);
    rotation.mulA_43(rotation_y);

    rotation_y.identity();
    rotation_y.rotateZ(current.z);
    rotation.mulA_43(rotation_y);

    B->mTransform.mulB_43(rotation);
}

void player_hud::load(const shared_str& player_hud_sect)
{
    if (player_hud_sect == m_sect_name)
        return;

    // 1. Сначала отцепляем предметы от рук, чтобы движок не пытался их рендерить
    m_attached_items[0] = nullptr;
    m_attached_items[1] = nullptr;

    // 2. Теперь безопасно удаляем объекты из пула
    for (auto& it : m_pool)
    {
        xr_delete(it.second);
    }
    m_pool.clear();

    m_sect_name = player_hud_sect;
    const bool b_reload = m_model != nullptr;

    if (m_model)
    {
        IRenderVisual* v = m_model->dcast_RenderVisual();
        GEnv.Render->model_Delete(v);
    }

    if (m_model_2)
    {
        IRenderVisual* v = m_model_2->dcast_RenderVisual();
        GEnv.Render->model_Delete(v);
    }

    if (!pSettings->section_exist(m_sect_name))
    {
        return;
    }

    const shared_str& model_name = pSettings->r_string(m_sect_name, "visual");
    m_model = smart_cast<IKinematicsAnimated*>(GEnv.Render->model_Create(model_name.c_str()));
    
    shared_str model_2_name = pSettings->line_exist(m_sect_name, "visual_2") ? pSettings->r_string(m_sect_name, "visual_2") : model_name;
    m_model_2 = smart_cast<IKinematicsAnimated*>(GEnv.Render->model_Create(model_2_name.c_str()));

    load_ancors();

    // RTT PDA
    script_anim_part = u8(-1);
    reset_thumb(true);

	u16 r_finger0 = m_model->dcast_PKinematics()->LL_BoneID("r_finger0");
    u16 r_finger01 = m_model->dcast_PKinematics()->LL_BoneID("r_finger01");
    u16 r_finger02 = m_model->dcast_PKinematics()->LL_BoneID("r_finger02");

    m_model->dcast_PKinematics()->LL_GetBoneInstance(r_finger0).set_callback(bctCustom, Thumb0Callback, this);
    m_model->dcast_PKinematics()->LL_GetBoneInstance(r_finger01).set_callback(bctCustom, Thumb01Callback, this);
    m_model->dcast_PKinematics()->LL_GetBoneInstance(r_finger02).set_callback(bctCustom, Thumb02Callback, this);

    // Скрытие рук (Разделение)
    u16 l_arm = m_model->dcast_PKinematics()->LL_BoneID("l_clavicle");
    u16 r_arm = m_model_2->dcast_PKinematics()->LL_BoneID("r_clavicle");
    m_model->dcast_PKinematics()->LL_SetBoneVisible(l_arm, FALSE, TRUE);
    m_model_2->dcast_PKinematics()->LL_SetBoneVisible(r_arm, FALSE, TRUE);

    if (!b_reload)
    {
        m_model->PlayCycle("hand_idle_doun");
        m_model_2->PlayCycle("hand_idle_doun");
    }
    else
    {
        if (m_attached_items[1]) m_attached_items[1]->m_parent_hud_item->on_a_hud_attach();
        if (m_attached_items[0]) m_attached_items[0]->m_parent_hud_item->on_a_hud_attach();
    }

    m_model->dcast_PKinematics()->CalculateBones_Invalidate();
    m_model->dcast_PKinematics()->CalculateBones(TRUE);
    m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
    m_model_2->dcast_PKinematics()->CalculateBones(TRUE);
}

void player_hud::load_ancors()
{
    const CInifile::Sect& _sect = pSettings->r_section(m_sect_name);
    for (const auto& [name, bone] : _sect.Data)
    {
        if (0 == strncmp(name.c_str(), "ancor_", sizeof("ancor_") - 1))
        {
            m_ancors.emplace_back(m_model->dcast_PKinematics()->LL_BoneID(bone));
        }
    }
}

bool player_hud::render_item_ui_query() const
{
    bool res = false;
    if (m_attached_items[0])
        res |= m_attached_items[0]->render_item_ui_query();

    if (m_attached_items[1])
        res |= m_attached_items[1]->render_item_ui_query();

    return res;
}

void player_hud::render_item_ui() const
{
    if (m_attached_items[0])
        m_attached_items[0]->render_item_ui();

    if (m_attached_items[1])
        m_attached_items[1]->render_item_ui();
}

void player_hud::render_hud(u32 context_id, IRenderable* root)
{
    attachable_hud_item* item0 = m_attached_items[0];
    attachable_hud_item* item1 = m_attached_items[1];

    if (!item0 && !item1)
        return;

	bool b_r0 = ((item0 && item0->need_renderable()) /*|| script_anim_part == 0 || script_anim_part == 2*/);
    bool b_r1 = ((item1 && item1->need_renderable()) /*|| script_anim_part == 1 || script_anim_part == 2*/);

    if (!b_r0 && !b_r1)
        return;

    if (m_model)
        GEnv.Render->add_Visual(context_id, root, m_model->dcast_RenderVisual(), m_transform);

    if (m_model_2)
        GEnv.Render->add_Visual(context_id, root, m_model_2->dcast_RenderVisual(), m_transform_2);

    if (item0)
        item0->render(context_id, root);

    if (item1)
        item1->render(context_id, root);
}

#include "xrCore/Animation/Motion.hpp"

u32 player_hud::motion_length(const shared_str& anim_name, const shared_str& hud_name, const CMotionDef*& md)
{
    const float speed = CalcMotionSpeed(anim_name, 1.0f);
    attachable_hud_item* pi = create_hud_item(hud_name);
    const player_hud_motion* pm = pi->m_hand_motions.find_motion(anim_name);

    if (!pm)
        return 100; // ms TEMPORARY
    R_ASSERT2(pm,
        make_string("hudItem model [%s] has no motion with alias [%s]", hud_name.c_str(), anim_name.c_str()).c_str());
    IKinematicsAnimated* model = pi->m_monolithic ? smart_cast<IKinematicsAnimated*>(pi->m_model) : nullptr;
    return motion_length(pm->m_animations[0].mid, md, speed, model);
}

u32 player_hud::motion_length(const MotionID& M, const CMotionDef*& md, float speed, IKinematicsAnimated* itemModel) const
{
    IKinematicsAnimated* model = itemModel ? itemModel : m_model;
    md = model->LL_GetMotionDef(M);
    VERIFY(md);
    if (md->flags & esmStopAtEnd)
    {
        CMotion* motion = model->LL_GetRootMotion(M);
        return iFloor(0.5f + 1000.f * motion->GetLength() / (md->Dequantize(md->speed) * speed));
    }
    return 0;
}

void player_hud::update(const Fmatrix& cam_trans)
{
    Fmatrix trans = cam_trans;

    if (psHUD_Flags.test(HUD_LEFT_HANDED))
    {
        trans.m[0][0] = -trans.m[0][0]; trans.m[0][1] = -trans.m[0][1];
        trans.m[0][2] = -trans.m[0][2]; trans.m[0][3] = -trans.m[0][3];
    }

    update_inertion(trans);

    if (m_attached_items[0])
    {
        m_attached_items[0]->update_hud_additional(trans);
    }
    else if (m_attached_items[1])
    {
        m_attached_items[1]->update_hud_additional(trans);
    }

    Fmatrix trans_2 = trans;

    // override hand offset for single hand animation
    /*if (script_anim_offset_factor != 0.f)
    {
        if (script_anim_part == 2 || (!m_attached_items[0] && !m_attached_items[1]))
        {
            m1pos = script_anim_offset[0];
            m2pos = script_anim_offset[0];
            m1rot = script_anim_offset[1];
            m2rot = script_anim_offset[1];
            trans = trans_b;
            trans_2 = trans_b;
        }
        else
        {
            Fvector& hand_pos = script_anim_part == 0 ? m1pos : m2pos;
            Fvector& hand_rot = script_anim_part == 0 ? m1rot : m2rot;
            hand_pos.lerp(script_anim_part == 0 ? m1pos : m2pos, script_anim_offset[0], script_anim_offset_factor);
            hand_rot.lerp(script_anim_part == 0 ? m1rot : m2rot, script_anim_offset[1], script_anim_offset_factor);
            if (script_anim_part == 0)
            {
                trans_b.inertion(trans, script_anim_offset_factor);
                trans = trans_b;
            }
            else
            {
                trans_b.inertion(trans_2, script_anim_offset_factor);
                trans_2 = trans_b;
            }
        }
    }*/

    Fvector m1rot = attach_rot(0);
    Fvector m2rot = attach_rot(1);
    
    m1rot.mul(PI / 180.f);
    m_attach_offset.setHPB(m1rot.x, m1rot.y, m1rot.z);
    m_attach_offset.translate_over(attach_pos(0));

    m2rot.mul(PI / 180.f);
    m_attach_offset_2.setHPB(m2rot.x, m2rot.y, m2rot.z);
    m_attach_offset_2.translate_over(attach_pos(1));

    m_transform.mul(trans, m_attach_offset);
    m_transform_2.mul(trans_2, m_attach_offset_2);

    m_model->UpdateTracks();
    m_model->dcast_PKinematics()->CalculateBones_Invalidate();
    m_model->dcast_PKinematics()->CalculateBones(TRUE);

    m_model_2->UpdateTracks();
    m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
    m_model_2->dcast_PKinematics()->CalculateBones(TRUE);

    if (m_attached_items[0]) m_attached_items[0]->update(true);
    if (m_attached_items[1]) m_attached_items[1]->update(true);
}

u32 player_hud::anim_play(u16 part, const MotionID& M, BOOL bMixIn, const CMotionDef*& md, float speed, u16 override_part)
{
    u16 part_id = u16(-1);
    if (attached_item(0) && attached_item(1))
        part_id = m_model->partitions().part_id((part == 0) ? "right_hand" : "left_hand");

    if (override_part != u16(-1))
        part_id = override_part;

#ifdef DEBUG
    if (M.valid())
    {
        Msg("* [HUD-DEBUG] Playing Anim: Slot[%d] ID[%d]. PartID: %d", M.slot, M.idx, part);
    }
#endif

    for (u8 pid = 0; pid < 3; ++pid)
    {
        if (part_id == u16(-1))
        {
            if (pid == 0 || pid == 2) 
            {
                CBlend* B = m_model->PlayCycle(pid, M, bMixIn);
                if (B) B->speed *= speed;
            }
            if (pid == 0 || pid == 1) 
            {
                CBlend* B = m_model_2->PlayCycle(pid, M, bMixIn);
                if (B) B->speed *= speed;
            }
        }
        else if (pid == 0 || pid == part_id)
        {
            if (part_id == 2 || part_id == 0)
            {
                CBlend* B = m_model->PlayCycle(pid, M, bMixIn);
                if (B) B->speed *= speed;
            }
            else
            {
                CBlend* B = m_model_2->PlayCycle(pid, M, bMixIn);
                if (B) B->speed *= speed;
            }
        }
    }

    m_model->dcast_PKinematics()->CalculateBones_Invalidate();
    m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();

    return motion_length(M, md, speed, nullptr); 
}

void player_hud::update_additional(Fmatrix& trans) const
{
    if (m_attached_items[0])
        m_attached_items[0]->update_hud_additional(trans);

    if (m_attached_items[1])
        m_attached_items[1]->update_hud_additional(trans);
}

void player_hud::update_inertion(Fmatrix& trans) const
{
    if (inertion_allowed())
    {
        attachable_hud_item* pMainHud = m_attached_items[0];

        Fmatrix xform;
        Fvector& origin = trans.c;
        xform = trans;

        static Fvector st_last_dir = {0, 0, 0};

        // load params
        hud_item_measures::inertion_params inertion_data;

        if (pMainHud)
        { // Загружаем параметры инерции из основного худа
            inertion_data.m_pitch_offset_r = pMainHud->m_measures.m_inertion_params.m_pitch_offset_r;
            inertion_data.m_pitch_offset_n = pMainHud->m_measures.m_inertion_params.m_pitch_offset_n;
            inertion_data.m_pitch_offset_d = pMainHud->m_measures.m_inertion_params.m_pitch_offset_d;
            inertion_data.m_pitch_low_limit = pMainHud->m_measures.m_inertion_params.m_pitch_low_limit;
            inertion_data.m_origin_offset = pMainHud->m_measures.m_inertion_params.m_origin_offset;
            inertion_data.m_origin_offset_aim = pMainHud->m_measures.m_inertion_params.m_origin_offset_aim;
            inertion_data.m_offset_LRUD = pMainHud->m_measures.m_inertion_params.m_offset_LRUD;
            inertion_data.m_offset_LRUD_aim = pMainHud->m_measures.m_inertion_params.m_offset_LRUD_aim;
            inertion_data.m_tendto_speed = pMainHud->m_measures.m_inertion_params.m_tendto_speed;
            inertion_data.m_tendto_speed_aim = pMainHud->m_measures.m_inertion_params.m_tendto_speed_aim;
            inertion_data.m_tendto_ret_speed = pMainHud->m_measures.m_inertion_params.m_tendto_ret_speed;
            inertion_data.m_tendto_ret_speed_aim = pMainHud->m_measures.m_inertion_params.m_tendto_ret_speed_aim;
            inertion_data.m_min_angle = pMainHud->m_measures.m_inertion_params.m_min_angle;
            inertion_data.m_min_angle_aim = pMainHud->m_measures.m_inertion_params.m_min_angle_aim;
        }
        else
        { // Загружаем дефолтные параметры инерции
            inertion_data.m_pitch_offset_r = PITCH_OFFSET_R;
            inertion_data.m_pitch_offset_n = PITCH_OFFSET_N;
            inertion_data.m_pitch_offset_d = PITCH_OFFSET_D;
            inertion_data.m_pitch_low_limit = PITCH_LOW_LIMIT;
            inertion_data.m_origin_offset = ORIGIN_OFFSET_OLD;
            inertion_data.m_origin_offset_aim = ORIGIN_OFFSET_AIM_OLD;

            inertion_data.m_offset_LRUD.set(ORIGIN_OFFSET);
            inertion_data.m_offset_LRUD_aim.set(ORIGIN_OFFSET_AIM);

            inertion_data.m_tendto_speed = TENDTO_SPEED;
            inertion_data.m_tendto_speed_aim = TENDTO_SPEED_AIM;
            inertion_data.m_tendto_ret_speed = TENDTO_SPEED_RET;
            inertion_data.m_tendto_ret_speed_aim = TENDTO_SPEED_RET_AIM;
            inertion_data.m_min_angle = INERT_MIN_ANGLE;
            inertion_data.m_min_angle_aim = INERT_MIN_ANGLE_AIM;
        }

        // pitch compensation
        float pitch = angle_normalize_signed(xform.k.getP());

        if (pMainHud != NULL)
            pitch *= pMainHud->m_parent_hud_item->GetInertionFactor();

        // Отдаление\приближение
        origin.mad(xform.k, -pitch * inertion_data.m_pitch_offset_d);

        // Сдвиг в противоположную часть экрана
        origin.mad(xform.i, -pitch * inertion_data.m_pitch_offset_r);

        // Подьём\опускание
        clamp(pitch, inertion_data.m_pitch_low_limit, PI);
        origin.mad(xform.j, -pitch * inertion_data.m_pitch_offset_n);
    }
}

attachable_hud_item* player_hud::create_hud_item(const shared_str& sect)
{
    current_player_hud_sect = sect;
    auto& item = m_pool[sect];

    if (!item)
        item = xr_new<attachable_hud_item>(this, sect, m_model);

    return item;
}

bool player_hud::allow_activation(CHudItem* item) const
{
    if (m_attached_items[1])
        return m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);
    else
        return true;
}

void player_hud::attach_item(CHudItem* item)
{
    attachable_hud_item* pi = create_hud_item(item->HudSection());
    const int item_idx = pi->m_attach_place_idx;

    if (m_attached_items[item_idx] != pi || pi->m_parent_hud_item != item)
    {
        if (m_attached_items[item_idx])
            m_attached_items[item_idx]->m_parent_hud_item->on_b_hud_detach();

        m_attached_items[item_idx] = pi;
        pi->m_parent_hud_item = item;
        pi->reload_measures();

        if (item_idx == 0 && m_attached_items[1])
            m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);

        item->on_a_hud_attach();
    }
    pi->m_parent_hud_item = item;
}

void player_hud::detach_item_idx(u16 idx)
{
    if (nullptr == attached_item(idx))
        return;

    m_attached_items[idx]->m_parent_hud_item->on_b_hud_detach();
    m_attached_items[idx]->m_parent_hud_item = nullptr;
    m_attached_items[idx] = nullptr;

    if (idx == 1)
    {
        if (m_attached_items[0])
            re_sync_anim(2);
        else
            m_model_2->PlayCycle("hand_idle_doun");
    }
    else if (idx == 0)
    {
        if (m_attached_items[1])
        {
            const player_hud_motion* pm = m_attached_items[1]->m_hand_motions.find_motion("anm_idle");
            if (pm)
            {
                const motion_descr& M = pm->m_animations[0];
                m_model->PlayCycle(0, M.mid, false);
                m_model->PlayCycle(2, M.mid, false);
            }
        }
        else
        {
            m_model->PlayCycle("hand_idle_doun");
            m_model_2->PlayCycle("hand_idle_doun");
        }
    }

    if (!m_attached_items[0] && !m_attached_items[1])
    {
        m_model->PlayCycle("hand_idle_doun");
        m_model_2->PlayCycle("hand_idle_doun");
    }
}

void player_hud::detach_item(CHudItem* item)
{
    if (nullptr == item->HudItemData())
        return;

    const u16 item_idx = item->HudItemData()->m_attach_place_idx;

    if (m_attached_items[item_idx] == item->HudItemData())
    {
        detach_item_idx(item_idx);
    }
}

void player_hud::calc_transform(u16 attach_slot_idx, const Fmatrix& offset, Fmatrix& result) const
{
    const attachable_hud_item* item = m_attached_items[attach_slot_idx];
    
    // Выбираем нужную модель и нужную матрицу трансформации в зависимости от слота
    IKinematics* kin = (attach_slot_idx == 0) ? m_model->dcast_PKinematics() : m_model_2->dcast_PKinematics();
    const Fmatrix& parent_trans = (attach_slot_idx == 0) ? m_transform : m_transform_2;

    if (item && !item->m_monolithic)
    {
        const Fmatrix ancor_m = kin->LL_GetTransform(m_ancors[attach_slot_idx]);
        result.mul(parent_trans, ancor_m);
        result.mulB_43(offset);
    }
    else
    {
        result.mul(parent_trans, offset);
        VERIFY(!fis_zero(DET(result)));
    }
}

bool player_hud::inertion_allowed() const
{
    if (const attachable_hud_item* hi = m_attached_items[0])
    {
        return hi->m_parent_hud_item->HudInertionEnabled() && hi->m_parent_hud_item->HudInertionAllowed();
    }
    return true;
}

void player_hud::OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd) const
{
    CHudItem* hudItem0 = m_attached_items[0] ? m_attached_items[0]->m_parent_hud_item : nullptr;
    CHudItem* hudItem1 = m_attached_items[1] ? m_attached_items[1]->m_parent_hud_item : nullptr;

    if (cmd == 0)
    {
        if (hudItem0 && hudItem0->GetState() == CHUDState::eIdle)
            hudItem0->PlayAnimIdle();

        if (hudItem1 && hudItem1->GetState() == CHUDState::eIdle)
            hudItem1->PlayAnimIdle();
    }
    else
    {
        if (hudItem0)
            hudItem0->OnMovementChanged(cmd);

        if (hudItem1)
            hudItem1->OnMovementChanged(cmd);
    }
}

const Fvector& player_hud::attach_rot(u8 part) const
{
    if (m_attached_items[part])
        return m_attached_items[part]->hands_attach_rot();
    else if (m_attached_items[!part]) // Если в текущем слоте пусто, берем ротацию из того, что в другой руке
        return m_attached_items[!part]->hands_attach_rot();

    static Fvector zero = { 0.f, 0.f, 0.f };
    return zero;
}

const Fvector& player_hud::attach_pos(u8 part) const
{
    if (m_attached_items[part])
        return m_attached_items[part]->hands_attach_pos();
    else if (m_attached_items[!part])
        return m_attached_items[!part]->hands_attach_pos();

    static Fvector zero = { 0.f, 0.f, 0.f };
    return zero;
}

void player_hud::re_sync_anim(u8 part)
{
    u32 bc = part == 1 ? m_model_2->LL_PartBlendsCount(part) : m_model->LL_PartBlendsCount(part);
    for (u32 bidx = 0; bidx < bc; ++bidx)
    {
        CBlend* BR = part == 1 ? m_model_2->LL_PartBlend(part, bidx) : m_model->LL_PartBlend(part, bidx);
        if (!BR) continue;

        MotionID M = BR->motionID;
        u16 pc = m_model->partitions().count(); // партиции на обеих руках одинаковые

        for (u16 pid = 0; pid < pc; ++pid)
        {
            if (pid == 0) // Базовый цикл (тело анимации)
            {
                CBlend* B = m_model->PlayCycle(0, M, TRUE);
                B->timeCurrent = BR->timeCurrent;
                B->speed = BR->speed;

                B = m_model_2->PlayCycle(0, M, TRUE);
                B->timeCurrent = BR->timeCurrent;
                B->speed = BR->speed;
            }
            else if (pid != part) // Синхронизируем другую руку
            {
                CBlend* B = part == 1 ? m_model->PlayCycle(pid, M, TRUE) : m_model_2->PlayCycle(pid, M, TRUE);
                B->timeCurrent = BR->timeCurrent;
                B->speed = BR->speed;
            }
        }
    }
}

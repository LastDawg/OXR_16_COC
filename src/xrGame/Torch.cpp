#include "StdAfx.h"
#include "Torch.h"
#include "Entity.h"
#include "Actor.h"
#include "xrEngine/LightAnimLibrary.h"
#include "xrPhysics/PhysicsShell.h"
#include "xrServer_Objects_ALife_Items.h"
#include "ai_sounds.h"

#include "Level.h"
#include "Include/xrRender/Kinematics.h"
#include "xrEngine/CameraBase.h"
#include "xrEngine/xr_collide_form.h"
#include "Inventory.h"
#include "game_base_space.h"

#include "UIGameCustom.h"
#include "CustomOutfit.h"
#include "ActorHelmet.h"

static const float TORCH_INERTION_CLAMP = PI_DIV_6;
static const float TORCH_INERTION_SPEED_MAX = 7.5f;
static const float TORCH_INERTION_SPEED_MIN = 0.5f;
static Fvector TORCH_OFFSET = {-0.2f, +0.1f, -0.3f};
static const Fvector OMNI_OFFSET = {-0.2f, +0.1f, -0.1f};
static const float OPTIMIZATION_DISTANCE = 100.f;

ENGINE_API extern int ps_r__ShaderNVG;

CTorch::CTorch()
    : fBrightness(1.f), lanim(nullptr), guid_bone(BI_NONE),
      m_delta_h(0), m_switched_on(false),
      light_render(GEnv.Render->light_create()),
      light_omni(GEnv.Render->light_create()),
      glow_render(GEnv.Render->glow_create())
{
    m_prev_hp.set(0, 0);

    light_render->set_type(IRender_Light::SPOT);
    light_render->set_shadow(true);
    light_omni->set_type(IRender_Light::POINT);
    light_omni->set_shadow(false);

	m_torch_offset = TORCH_OFFSET;
    m_omni_offset = OMNI_OFFSET;
    m_torch_inertion_speed_max = TORCH_INERTION_SPEED_MAX;
    m_torch_inertion_speed_min = TORCH_INERTION_SPEED_MIN;

    m_NightVisionType = 0;
    m_fNightVisionLumFactor = 0.0f;
}

CTorch::~CTorch()
{
    light_render.destroy();
    light_omni.destroy();
    glow_render.destroy();
}

void CTorch::OnMoveToSlot(const SInvItemPlace& prev)
{
    CInventoryOwner* owner = smart_cast<CInventoryOwner*>(H_Parent());
    if (owner && !owner->attached(this))
    {
        owner->attach(this->cast_inventory_item());
    }

    if (m_pInventory && (prev.type == eItemPlaceSlot))
    {
        CActor* pActor = smart_cast<CActor*>(H_Parent());
        if (pActor)
        {
            if (pActor->GetNightVisionStatus())
                pActor->SwitchNightVision(true, false);
        }
    }
}
void CTorch::OnMoveToRuck(const SInvItemPlace& prev)
{
    if (prev.type == eItemPlaceSlot)
    {
        Switch(false);
    }

    if (m_pInventory && (prev.type == eItemPlaceSlot))
    {
        CActor* pActor = smart_cast<CActor*>(H_Parent());
        if (pActor)
        {
            pActor->SwitchNightVision(false);
        }
    }
}

inline bool CTorch::can_use_dynamic_lights()
{
    if (!H_Parent())
        return (true);

    CInventoryOwner* owner = smart_cast<CInventoryOwner*>(H_Parent());
    if (!owner)
        return (true);

    return (owner->can_use_dynamic_lights());
}

void CTorch::Load(LPCSTR section)
{
    inherited::Load(section);
    light_trace_bone = pSettings->r_string(section, "light_trace_bone");

    m_light_section = READ_IF_EXISTS(pSettings, r_string, section, "light_section", "torch_definition");

    m_torch_offset = READ_IF_EXISTS(pSettings, r_fvector3, section, "torch_offset", TORCH_OFFSET);
    m_omni_offset = READ_IF_EXISTS(pSettings, r_fvector3, section, "omni_offset", OMNI_OFFSET);
    m_torch_inertion_speed_max = READ_IF_EXISTS(pSettings, r_float, section, "torch_inertion_speed_max", TORCH_INERTION_SPEED_MAX);
    m_torch_inertion_speed_min = READ_IF_EXISTS(pSettings, r_float, section, "torch_inertion_speed_min", TORCH_INERTION_SPEED_MIN);

    if (pSettings->line_exist(section, "snd_turn_on"))
        m_sounds.LoadSound(section, "snd_turn_on", "sndTurnOn", false, SOUND_TYPE_ITEM_USING);
    if (pSettings->line_exist(section, "snd_turn_off"))
        m_sounds.LoadSound(section, "snd_turn_off", "sndTurnOff", false, SOUND_TYPE_ITEM_USING);

	m_fDecayRate = READ_IF_EXISTS(pSettings, r_float, section, "power_decay_rate", 0.f);
    m_fPassiveDecayRate = READ_IF_EXISTS(pSettings, r_float, section, "passive_decay_rate", 0.f);

    m_bTorchModeEnabled   = READ_IF_EXISTS(pSettings, r_bool, section, "torch_allowed", true);
    m_bNightVisionEnabled = READ_IF_EXISTS(pSettings, r_bool, section, "night_vision_allowed", false);

    if (pSettings->line_exist(section, "nightvision_sect"))
        m_NightVisionSect = pSettings->r_string(section, "nightvision_sect");
    else
        m_NightVisionSect = "";

	m_sShaderNightVisionSect = READ_IF_EXISTS(pSettings, r_string, section, "shader_nightvision_sect", "shader_nightvision_default");
	m_NightVisionType = READ_IF_EXISTS(pSettings, r_u32, m_sShaderNightVisionSect, "shader_nightvision_type", 0);
	m_fNightVisionLumFactor = READ_IF_EXISTS(pSettings, r_float, m_sShaderNightVisionSect, "shader_nightvision_lum_factor", 0.0f);

	// Disabling shift by x and z axes for 1st render,
    // because we don't have dynamic lighting in it.
    if (GEnv.Render->GenerationIsR1())
    {
        m_torch_offset.x = 0;
        m_torch_offset.z = 0;
    }
}

void CTorch::Switch()
{
    if (!m_bTorchModeEnabled)   
        return;

    if (OnClient())
        return;

    bool bActive = !m_switched_on;

    Switch(bActive);
}

void CTorch::Switch(bool light_on)
{
    CActor* pActor = smart_cast<CActor*>(H_Parent());
    if (pActor)
    {
        if (light_on && !m_switched_on)
        {
            if (m_sounds.FindSoundItem("SndTurnOn", false))
                m_sounds.PlaySound("SndTurnOn", pActor->Position(), NULL, !!pActor->HUDview());
        }
        else if (!light_on && m_switched_on)
        {
            if (m_sounds.FindSoundItem("SndTurnOff", false))
                m_sounds.PlaySound("SndTurnOff", pActor->Position(), NULL, !!pActor->HUDview());
        }
    }

    m_switched_on = light_on;
    if (can_use_dynamic_lights())
    {
        light_render->set_active(light_on);

        // CActor *pA = smart_cast<CActor *>(H_Parent());
        // if(!pA)
        light_omni->set_active(light_on);
    }
    glow_render->set_active(light_on);

    if (light_trace_bone.c_str())
    {
        IKinematics* pVisual = smart_cast<IKinematics*>(Visual());
        VERIFY(pVisual);
        u16 bi = pVisual->LL_BoneID(light_trace_bone);

        pVisual->LL_SetBoneVisible(bi, light_on, TRUE);
        pVisual->CalculateBones(TRUE);
    }
}
bool CTorch::torch_active() const 
{ 
    return (m_switched_on); 
}

bool CTorch::net_Spawn(CSE_Abstract* DC)
{
    CSE_Abstract* e = (CSE_Abstract*)(DC);
    CSE_ALifeItemTorch* torch = smart_cast<CSE_ALifeItemTorch*>(e);
    R_ASSERT(torch);
    cNameVisual_set(torch->get_visual());

    R_ASSERT(!GetCForm());
    R_ASSERT(smart_cast<IKinematics*>(Visual()));
    CForm = xr_new<CCF_Skeleton>(this);

    if (!inherited::net_Spawn(DC))
        return (FALSE);

    bool b_r2 = GEnv.Render->GenerationIsR2OrHigher();

    IKinematics* K = smart_cast<IKinematics*>(Visual());
    CInifile* pUserData = K->LL_UserData();
    R_ASSERT3(pUserData, "Empty Torch user data!", torch->get_visual());
    lanim = LALib.FindItem(pUserData->r_string(m_light_section, "color_animator"));
    guid_bone = K->LL_BoneID(pUserData->r_string(m_light_section, "guide_bone"));
    VERIFY(guid_bone != BI_NONE);

    Fcolor clr = pUserData->r_fcolor(m_light_section, (b_r2) ? "color_r2" : "color");
    fBrightness = clr.intensity();
    float range = pUserData->r_float(m_light_section, (b_r2) ? "range_r2" : "range");
    light_render->set_color(clr);
    light_render->set_range(range);

    if (b_r2)
    {
        bool useVolumetric = pUserData->read_if_exists<bool>(m_light_section, "volumetric_enabled", false);
        light_render->set_volumetric(useVolumetric);
        if (useVolumetric)
        {
            float volQuality = pUserData->read_if_exists<float>(m_light_section, "volumetric_quality", 1.f);
            clamp(volQuality, 0.f, 1.f);
            light_render->set_volumetric_quality(volQuality);

            float volIntensity = pUserData->read_if_exists<float>(m_light_section, "volumetric_intensity", 1.f);
            clamp(volIntensity, 0.f, 10.f);
            light_render->set_volumetric_intensity(volIntensity);

            float volDistance = pUserData->read_if_exists<float>(m_light_section, "volumetric_distance", 1.f);
            clamp(volDistance, 0.f, 1.f);
            light_render->set_volumetric_distance(volDistance);
        }
    }

    Fcolor clr_o = pUserData->r_fcolor(m_light_section, (b_r2) ? "omni_color_r2" : "omni_color");
    float range_o = pUserData->r_float(m_light_section, (b_r2) ? "omni_range_r2" : "omni_range");
    light_omni->set_color(clr_o);
    light_omni->set_range(range_o);

    light_render->set_cone(deg2rad(pUserData->r_float(m_light_section, "spot_angle")));
    light_render->set_texture(pUserData->r_string(m_light_section, "spot_texture"));

    glow_render->set_texture(pUserData->r_string(m_light_section, "glow_texture"));
    glow_render->set_color(clr);
    glow_render->set_radius(pUserData->r_float(m_light_section, "glow_radius"));

    light_render->set_type((IRender_Light::LT)(READ_IF_EXISTS(pUserData, r_u8, m_light_section, "type", 2)));
    light_omni->set_type((IRender_Light::LT)(READ_IF_EXISTS(pUserData, r_u8, m_light_section, "omni_type", 1)));

    //включить/выключить фонарик
    Switch(torch->m_active);
    VERIFY(!torch->m_active || (torch->ID_Parent != 0xffff));

    m_delta_h = PI_DIV_2 - atan((range * 0.5f) / _abs(m_torch_offset.x));

    return (TRUE);
}

void CTorch::net_Destroy()
{
    Switch(false);

    inherited::net_Destroy();
}

void CTorch::OnH_A_Chield()
{
    inherited::OnH_A_Chield();
    m_focus.set(Position());
}

void CTorch::OnH_B_Independent(bool just_before_destroy)
{
    inherited::OnH_B_Independent(just_before_destroy);

    Switch(false);
}

void CTorch::UpdateCL()
{
    inherited::UpdateCL();

    UpdatePower();

    if (!m_switched_on)
        return;

    CBoneInstance& BI = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(guid_bone);
    Fmatrix M;

    if (H_Parent())
    {
        CActor* actor = smart_cast<CActor*>(H_Parent());
        if (actor)
            smart_cast<IKinematics*>(H_Parent()->Visual())->CalculateBones_Invalidate();

        if (H_Parent()->XFORM().c.distance_to_sqr(Device.vCameraPosition) < _sqr(OPTIMIZATION_DISTANCE) ||
            GameID() != eGameIDSingle)
        {
            // near camera
            smart_cast<IKinematics*>(H_Parent()->Visual())->CalculateBones();
            M.mul_43(XFORM(), BI.mTransform);
        }
        else
        {
            // approximately the same
            M = H_Parent()->XFORM();
            H_Parent()->Center(M.c);
            M.c.y += H_Parent()->Radius() * 2.f / 3.f;
        }

        if (actor)
        {
			m_prev_hp.x = angle_inertion_var(m_prev_hp.x, -actor->cam_Active()->yaw, m_torch_inertion_speed_min, m_torch_inertion_speed_max, TORCH_INERTION_CLAMP, Device.fTimeDelta);
		    m_prev_hp.y = angle_inertion_var(m_prev_hp.y, -actor->cam_Active()->pitch, m_torch_inertion_speed_min, m_torch_inertion_speed_max, TORCH_INERTION_CLAMP, Device.fTimeDelta);

            Fvector dir, right, up;
            dir.setHP(m_prev_hp.x + m_delta_h, m_prev_hp.y);
            Fvector::generate_orthonormal_basis_normalized(dir, up, right);

            if (true)
            {
                Fvector offset = M.c;
                offset.mad(M.i, m_torch_offset.x);
                offset.mad(M.j, m_torch_offset.y);
                offset.mad(M.k, m_torch_offset.z);
                light_render->set_position(offset);

                if (true /*false*/)
                {
                    offset = M.c;
                    offset.mad(M.i, m_omni_offset.x);
                    offset.mad(M.j, m_omni_offset.y);
                    offset.mad(M.k, m_omni_offset.z);
                    light_omni->set_position(offset);
                }
            } // if (true)
            glow_render->set_position(M.c);

            if (true)
            {
                light_render->set_rotation(dir, right);

                if (true /*false*/)
                {
                    light_omni->set_rotation(dir, right);
                }
            } // if (true)
            glow_render->set_direction(dir);

        } // if(actor)
        else
        {
            if (can_use_dynamic_lights())
            {
                light_render->set_position(M.c);
                light_render->set_rotation(M.k, M.i);

                Fvector offset = M.c;
                offset.mad(M.i, m_omni_offset.x);
                offset.mad(M.j, m_omni_offset.y);
                offset.mad(M.k, m_omni_offset.z);
                light_omni->set_position(M.c);
                light_omni->set_rotation(M.k, M.i);
            } // if (can_use_dynamic_lights())

            glow_render->set_position(M.c);
            glow_render->set_direction(M.k);
        }
    } // if(HParent())
    else
    {
        if (getVisible() && m_pPhysicsShell)
        {
            M.mul(XFORM(), BI.mTransform);

            m_switched_on = false;
            light_render->set_active(false);
            light_omni->set_active(false);
            glow_render->set_active(false);
        } // if (getVisible() && m_pPhysicsShell)
    }

    if (!m_switched_on)
        return;

    // calc color animator
    if (!lanim)
        return;

    int frame;
    // возвращает в формате BGR
    u32 clr = lanim->CalculateBGR(Device.fTimeGlobal, frame);

    Fcolor fclr;
    fclr.set((float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f);
    fclr.mul_rgb(fBrightness / 255.f);
    if (can_use_dynamic_lights())
    {
        light_render->set_color(fclr);
        light_omni->set_color(fclr);
    }
    glow_render->set_color(fclr);
}

void CTorch::create_physic_shell() { CPhysicsShellHolder::create_physic_shell(); }
void CTorch::activate_physic_shell() { CPhysicsShellHolder::activate_physic_shell(); }
void CTorch::setup_physic_shell() { CPhysicsShellHolder::setup_physic_shell(); }
void CTorch::net_Export(NET_Packet& P)
{
    inherited::net_Export(P);
    //	P.w_u8						(m_switched_on ? 1 : 0);

    u8 F = 0;
    F |= (m_switched_on ? eTorchActive : 0);

    const CActor* pA = smart_cast<const CActor*>(H_Parent());
    if (pA)
    {
        if (pA->attached(this))
            F |= eAttached;
    }
    P.w_u8(F);
    //	Msg("CTorch::net_export - NV[%d]", m_bNightVisionOn);
}

void CTorch::net_Import(NET_Packet& P)
{
    inherited::net_Import(P);

    u8 F = P.r_u8();
    bool new_m_switched_on = !!(F & eTorchActive);

    if (new_m_switched_on != m_switched_on)
        Switch(new_m_switched_on);
}
bool CTorch::can_be_attached() const
{
    const CActor* pA = smart_cast<const CActor*>(H_Parent());
    if (pA)
        return pA->inventory().InSlot(this);
    else
        return true;
}

void CTorch::afterDetach()
{
    inherited::afterDetach();

    Switch(false);

    CActor* pActor = smart_cast<CActor*>(H_Parent());
    if (pActor)
    {
        pActor->SwitchNightVision(false);
    }
}

void CTorch::enable(bool value)
{
    inherited::enable(value);

    if (!enabled() && m_switched_on)
        Switch(false);
}

void CTorch::UpdatePower()
{
    CActor* pActor = smart_cast<CActor*>(H_Parent());

    if (IsUsingCondition())
    {
        fBrightness = this->GetCondition();
    }

    if (m_switched_on && IsUsingCondition() && GetCondition() > 0.0)
        this->ChangeCondition(-m_fDecayRate * Device.fTimeDelta);

    if (!m_switched_on && IsUsingCondition() && GetCondition() > 0.0 && pActor && !pActor->m_bTorchNightVision)
        this->ChangeCondition(-m_fPassiveDecayRate * Device.fTimeDelta);

    if (IsUsingCondition() && GetCondition() <= 0.0 && pActor && pActor->m_bTorchNightVision)
        pActor->SwitchNightVision(false);

    if (pActor && pActor->m_bTorchNightVision)
        this->ChangeCondition(-m_fDecayRate * Device.fTimeDelta);

    if (IsUsingCondition() && GetCondition() <= 0.0)
        Switch(false);
}

bool CTorch::install_upgrade_impl(LPCSTR section, bool test)
{
    LPCSTR str;

    // Msg("Torch Upgrade");
    bool result = inherited::install_upgrade_impl(section, test);

    bool result2 = process_if_exists_set(section, "nightvision_sect", &CInifile::r_string, str, test);
    if (result2 && !test)
    {
        m_NightVisionSect._set(str);
    }
    result |= result2;

	result2 = process_if_exists_set(section, "shader_nightvision_sect", &CInifile::r_string, str, test);
    if (result2 && !test)
    {
        m_sShaderNightVisionSect._set(str);
        m_NightVisionType = READ_IF_EXISTS(pSettings, r_u32, m_sShaderNightVisionSect, "shader_nightvision_type", 0);
        m_fNightVisionLumFactor = READ_IF_EXISTS(pSettings, r_float, m_sShaderNightVisionSect, "shader_nightvision_lum_factor", 0.0f);
    }
    result |= result2;

    result |= process_if_exists(section, "passive_decay_rate", &CInifile::r_float, m_fPassiveDecayRate, test);
    result |= process_if_exists(section, "power_decay_rate", &CInifile::r_float, m_fDecayRate, test);
    result |= process_if_exists(section, "inv_weight", &CInifile::r_float, m_weight, test);

    bool value = m_bTorchModeEnabled;
    bool result3 = process_if_exists_set(section, "torch_allowed", &CInifile::r_bool, value, test);
    if (result3 && !test)
    {
        m_bTorchModeEnabled = !!value;
    }
    result |= result3;

    return result;
}

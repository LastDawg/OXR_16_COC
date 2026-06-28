#include "StdAfx.h"
#include "CustomDetector.h"
#include "ui/ArtefactDetectorUI.h"
#include "HUDManager.h"
#include "Inventory.h"
#include "Level.h"
#include "map_manager.h"
#include "ActorEffector.h"
#include "Actor.h"
#include "xrUICore/Windows/UIWindow.h"
#include "player_hud.h"
#include "Weapon.h"

ITEM_INFO::ITEM_INFO() : snd_time(0), cur_period(0)
{
    pParticle = nullptr;
    curr_ref = nullptr;
}

ITEM_INFO::~ITEM_INFO()
{
    if (pParticle)
        CParticlesObject::Destroy(pParticle);
}

bool CCustomDetector::CheckCompatibilityInt(CHudItem* itm, u16* slot_to_activate)
{
    if (itm == nullptr)
        return true;

    CInventoryItem& iitm = itm->item();
    u32 slot = iitm.BaseSlot();
    bool bres = (slot == INV_SLOT_2 || slot == KNIFE_SLOT || slot == BOLT_SLOT);
    if (!bres && slot_to_activate)
    {
        *slot_to_activate = NO_ACTIVE_SLOT;
        if (m_pInventory->ItemFromSlot(BOLT_SLOT))
            *slot_to_activate = BOLT_SLOT;

        if (m_pInventory->ItemFromSlot(KNIFE_SLOT))
            *slot_to_activate = KNIFE_SLOT;

        if (m_pInventory->ItemFromSlot(INV_SLOT_3) && m_pInventory->ItemFromSlot(INV_SLOT_3)->BaseSlot() != INV_SLOT_3)
            *slot_to_activate = INV_SLOT_3;

        if (m_pInventory->ItemFromSlot(INV_SLOT_2) && m_pInventory->ItemFromSlot(INV_SLOT_2)->BaseSlot() != INV_SLOT_3)
            *slot_to_activate = INV_SLOT_2;

        if (*slot_to_activate != NO_ACTIVE_SLOT)
            bres = true;
    }

    if (itm->GetState() != CHUDState::eShowing)
        bres = bres && !itm->IsPending();

    if (bres)
    {
        CWeapon* W = smart_cast<CWeapon*>(itm);
        if (W)
            bres = bres && (W->GetState() != CHUDState::eBore) && (W->GetState() != CWeapon::eReload) &&
                (W->GetState() != CWeapon::eSwitch) && !W->IsZoomed();
    }
    return bres;
}

bool CCustomDetector::CheckCompatibility(CHudItem* itm)
{
    if (!inherited::CheckCompatibility(itm))
        return false;

    if (!CheckCompatibilityInt(itm, NULL))
    {
        HideDetector(true);
        return false;
    }
    return true;
}

void CCustomDetector::HideDetector(bool bFastMode)
{
    if (GetState() == eIdle)
        ToggleDetector(bFastMode);
}

void CCustomDetector::ShowDetector(bool bFastMode)
{
    if (GetState() == eHidden)
        ToggleDetector(bFastMode);
}

void CCustomDetector::ToggleDetector(bool bFastMode)
{
    m_bNeedActivation = false;
    m_bFastAnimMode = bFastMode;

    if (GetState() == eHidden)
    {
        PIItem iitem = m_pInventory->ActiveItem();
        CHudItem* itm = (iitem) ? iitem->cast_hud_item() : NULL;
        u16 slot_to_activate = NO_ACTIVE_SLOT;

        if (CheckCompatibilityInt(itm, &slot_to_activate))
        {
            if (slot_to_activate != NO_ACTIVE_SLOT)
            {
                m_pInventory->Activate(slot_to_activate);
                m_bNeedActivation = true;
            }
            else
            {
                SwitchState(eShowing);
                TurnDetectorInternal(true);
            }
        }
    }
    else if (GetState() == eIdle)
        SwitchState(eHiding);
}

void CCustomDetector::OnStateSwitch(u32 S, u32 oldState)
{
    inherited::OnStateSwitch(S, oldState);

    switch (S)
    {
    case eShowing:
    {
        g_player_hud->attach_item(this);

        // По умолчанию включаем экран
        float initial_factor = 1.f;

        // Но если в анимации есть метки — выключаем его (ждем метку)
        if (CurrentAnimHasMarks())
            initial_factor = 0.f;

        g_pGamePersistent->devices_shader_data.pda_display_factor = initial_factor;

		if (!IsUsingCondition() || (IsUsingCondition() && GetCondition() > 0.0))
            TurnDetectorInternal(true);

        m_sounds.PlaySound(m_bFastAnimMode ? "sndShowFast" : "sndShow", Fvector().set(0, 0, 0), this, true, false);
        PlayHUDMotion(m_bFastAnimMode ? "anm_show_fast" : "anm_show", "anim_show", FALSE /*TRUE*/, this, GetState());
        SetPending(TRUE);
    }
    break;
    case eHiding:
    {
        if (oldState != eHiding)
        {
            m_sounds.PlaySound(m_bFastAnimMode ? "sndHideFast" : "sndHide", Fvector().set(0, 0, 0), this, true, false);
            PlayHUDMotion(m_bFastAnimMode ? "anm_hide_fast" : "anm_hide", "anim_show", FALSE/*TRUE*/, this, GetState());
            SetPending(TRUE);
        }
    }
    break;
    case eIdle:
    {
        PlayAnimIdle();
        SetPending(FALSE);
    }
    break;
    }
}

void CCustomDetector::OnAnimationEnd(u32 state)
{
    inherited::OnAnimationEnd(state);
    switch (state)
    {
    case eShowing:
    {
        SwitchState(eIdle);
    }
    break;
    case eHiding:
    {
        SwitchState(eHidden);
        TurnDetectorInternal(false);
        g_player_hud->detach_item(this);
    }
    break;
    }
}

void CCustomDetector::UpdateXForm() { CInventoryItem::UpdateXForm(); }
void CCustomDetector::OnActiveItem() { return; }
void CCustomDetector::OnHiddenItem() {}
CCustomDetector::CCustomDetector()
{
    m_ui = NULL;
    m_bFastAnimMode = false;
    m_bNeedActivation = false;
}

CCustomDetector::~CCustomDetector()
{
    m_artefacts.destroy();
    TurnDetectorInternal(false);
    xr_delete(m_ui);
}

bool CCustomDetector::net_Spawn(CSE_Abstract* DC)
{
    TurnDetectorInternal(false);
    return (inherited::net_Spawn(DC));
}

void CCustomDetector::Load(LPCSTR section)
{
    m_animation_slot = 7;
    inherited::Load(section);

    m_fAfDetectRadius = pSettings->read_if_exists<float>(section, "af_radius", 30.0f);
    m_fAfVisRadius = pSettings->read_if_exists<float>(section, "af_vis_radius", 2.0f);
    m_fDecayRate = READ_IF_EXISTS(pSettings, r_float, section, "power_decay_rate", 0.f); // Alundaio
    m_fPassiveDecayRate = READ_IF_EXISTS(pSettings, r_float, section, "passive_decay_rate", 0.f);
    m_artefacts.load(section, "af");

    m_sounds.LoadSound(section, "snd_draw", "sndShow");
    m_sounds.LoadSound(section, "snd_holster", "sndHide");
    m_sounds.LoadSound(section, "snd_draw_fast", "sndShowFast");
    m_sounds.LoadSound(section, "snd_holster_fast", "sndHideFast");
}

void CCustomDetector::shedule_Update(u32 dt)
{
    inherited::shedule_Update(dt);

    if (!IsWorking())
        return;

    Position().set(H_Parent()->Position());

    Fvector P;
    P.set(H_Parent()->Position());

    if (IsUsingCondition() && GetCondition() <= 0.01f)
        return;

    m_artefacts.feel_touch_update(P, m_fAfDetectRadius);
}

bool CCustomDetector::IsWorking() { return m_bWorking && H_Parent() && H_Parent() == Level().CurrentViewEntity(); }
void CCustomDetector::UpfateWork()
{
    UpdateAf();
    m_ui->update();
}

void CCustomDetector::UpdateVisibility()
{
    // check visibility
    attachable_hud_item* i0 = g_player_hud->attached_item(0);
    if (i0 && HudItemData())
    {
        bool bClimb = ((Actor()->MovingState() & mcClimb) != 0);
        if (bClimb)
        {
            HideDetector(true);
            m_bNeedActivation = true;
        }
        else
        {
            CWeapon* wpn = smart_cast<CWeapon*>(i0->m_parent_hud_item);
            if (wpn)
            {
                u32 state = wpn->GetState();
                if (wpn->IsZoomed() || state == CWeapon::eReload || state == CWeapon::eSwitch)
                {
                    HideDetector(true);
                    m_bNeedActivation = true;
                }
            }
        }
    }
    else if (m_bNeedActivation)
    {
        attachable_hud_item* i0 = g_player_hud->attached_item(0);
        bool bClimb = ((Actor()->MovingState() & mcClimb) != 0);
        if (!bClimb)
        {
            CHudItem* huditem = (i0) ? i0->m_parent_hud_item : NULL;
            bool bChecked = !huditem || CheckCompatibilityInt(huditem, 0);

            if (bChecked)
                ShowDetector(true);
        }
    }
}

void CCustomDetector::UpdateCL()
{
    inherited::UpdateCL();

    if (H_Parent() != Level().CurrentEntity())
        return;

    UpdateVisibility();
    UpdatePower();

    if (!IsWorking())
        return;
    UpfateWork();
}

void CCustomDetector::OnMotionMark(u32 state, const motion_marks& M)
{
    inherited::OnMotionMark(state, M);

    // Работаем только если включен bool в конфиге
    if (!CurrentAnimHasMarks()) return;

    if (state == eShowing)
    {
        g_pGamePersistent->devices_shader_data.pda_display_factor = 1.f;
    }
    else if (state == eHiding)
    {
        g_pGamePersistent->devices_shader_data.pda_display_factor = 0.f;
    }
}

bool CCustomDetector::CurrentAnimHasMarks()
{
    if (!HudItemData()) return false;
    
    IKinematicsAnimated* ka = smart_cast<IKinematicsAnimated*>(HudItemData()->m_model);
    if (!ka) return false;

    // Получаем текущую играемую анимацию
    for (u16 i = 0; i < ka->partitions().count(); ++i)
    {
        CBlend* B = ka->LL_PartBlend(i, 0);
        if (B)
        {
            CMotionDef* md = ka->LL_GetMotionDef(B->motionID);
            if (md && !md->marks.empty()) 
                return true; // Метки найдены
        }
    }
    return false;
}

void CCustomDetector::UpdatePower()
{
	if (IsUsingCondition())
    {
        if (m_bWorking && GetCondition() <= 0.0)
            TurnDetectorInternal(false);
        else if (!m_bWorking && (GetState() == eIdle) && GetCondition() > 0.0)
            TurnDetectorInternal(true);
    }

    if (m_bWorking && IsUsingCondition() && m_fDecayRate > 0.f)
        this->ChangeCondition(-m_fDecayRate * Device.fTimeDelta);

    if (!m_bWorking && IsUsingCondition() && m_fPassiveDecayRate > 0.f)
        this->ChangeCondition(-m_fPassiveDecayRate * Device.fTimeDelta);

    if (ParentIsActor() && m_bWorking)
    {
        // Если мы в простое ИЛИ если метки не используются — управляем яркостью как обычно
        if (GetState() == eIdle || !CurrentAnimHasMarks())
        {
            if (!IsUsingCondition() || GetCondition() >= 0.01f)
                g_pGamePersistent->devices_shader_data.pda_display_factor = 1.f;
            else
            {
                g_pGamePersistent->devices_shader_data.pda_display_factor = 0.f;
                ResetUI();
            }
        }
    }
}


void CCustomDetector::OnH_A_Chield() 
{ 
    inherited::OnH_A_Chield(); 
}

void CCustomDetector::OnH_B_Independent(bool just_before_destroy)
{
    inherited::OnH_B_Independent(just_before_destroy);

    m_artefacts.clear();

	if (GetState() != eHidden)
	{
		// Detaching hud item and animation stop in OnH_A_Independent
		TurnDetectorInternal(false);
		SwitchState(eHidden);
	}
}

void CCustomDetector::OnMoveToRuck(const SInvItemPlace& prev)
{
    inherited::OnMoveToRuck(prev);
    if (prev.type == eItemPlaceSlot)
    {
        SwitchState(eHidden);
        g_player_hud->detach_item(this);
    }
    TurnDetectorInternal(false);
    StopCurrentAnimWithoutCallback();
}

void CCustomDetector::OnMoveToSlot(const SInvItemPlace& prev) 
{ 
    inherited::OnMoveToSlot(prev); 
}

void CCustomDetector::TurnDetectorInternal(bool b)
{
    m_bWorking = b;

    if (b && m_ui == NULL)
        CreateUI();

    /*else
    {
        xr_delete(m_ui);
    }*/

    UpdateNightVisionMode(b);
}

#include "game_base_space.h"
void CCustomDetector::UpdateNightVisionMode(bool b_on) {}
bool CAfList::feel_touch_contact(IGameObject* O)
{
    TypesMapIt it = m_TypesMap.find(O->cNameSect());

    bool res = (it != m_TypesMap.end());
    if (res)
    {
        CArtefact* pAf = smart_cast<CArtefact*>(O);

        if (pAf->GetAfRank() > m_af_rank)
            res = false;
    }
    return res;
}

bool CCustomDetector::install_upgrade_impl(LPCSTR section, bool test)
{
    // Msg("Detector Upgrade");
    bool result = inherited::install_upgrade_impl(section, test);

    result |= process_if_exists(section, "af_radius", &CInifile::r_float, m_fAfDetectRadius, test);
    result |= process_if_exists(section, "af_vis_radius", &CInifile::r_float, m_fAfVisRadius, test);
    result |= process_if_exists(section, "passive_decay_rate", &CInifile::r_float, m_fPassiveDecayRate, test);
    result |= process_if_exists(section, "power_decay_rate", &CInifile::r_float, m_fDecayRate, test);
    result |= process_if_exists(section, "inv_weight", &CInifile::r_float, m_weight, test);

    return result;
}

#include "StdAfx.h"
#include "ActorBackpack.h"
#include "Actor.h"
#include "Inventory.h"

CBackpack::CBackpack()
{
    m_flags.set(FUsingCondition, false);
}

bool CBackpack::net_Spawn(CSE_Abstract* DC)
{
    //if (IsGameTypeSingle())
        //ReloadBonesProtection();

    BOOL res = inherited::net_Spawn(DC);
    return (res);
}

void CBackpack::Load(pcstr section)
{
    inherited::Load(section);

    m_additional_weight = pSettings->r_float(section, "additional_inventory_weight");
    m_additional_weight2 = pSettings->r_float(section, "additional_inventory_weight2");
    m_fPowerRestoreSpeed = pSettings->read_if_exists<float>(section, "power_restore_speed", 0.0f);
    m_fPowerLoss = pSettings->read_if_exists<float>(section, "power_loss", 1.0f);
    clamp(m_fPowerLoss, EPS, 1.0f);

    m_fJumpSpeed = pSettings->read_if_exists<float>(section, "jump_speed", 1.f);
    m_fWalkAccel = pSettings->read_if_exists<float>(section, "walk_accel", 1.f);
    m_fOverweightWalkK = pSettings->read_if_exists<float>(section, "overweight_walk_accel", 1.f);

    m_flags.set(FUsingCondition, pSettings->read_if_exists<bool>(section, "use_condition", true));
}

/*
void CBackpack::ReloadBonesProtection()
{
    IGameObject* parent = H_Parent();
    if (IsGameTypeSingle())
        parent = smart_cast<IGameObject*>(Level().CurrentViewEntity());

    if (parent && parent->Visual() && m_BonesProtectionSect.size())
        m_boneProtection.reload(m_BonesProtectionSect, smart_cast<IKinematics*>(parent->Visual()));
}
*/

void CBackpack::Hit(float hit_power, ALife::EHitType hit_type)
{
    if (!IsUsingCondition())
        return;
    hit_power *= GetHitImmunity(hit_type);
    ChangeCondition(-hit_power);
}

bool CBackpack::install_upgrade_impl(pcstr section, bool test)
{
    bool result = inherited::install_upgrade_impl(section, test);

    result |= process_if_exists(section, "power_restore_speed", &CInifile::r_float, m_fPowerRestoreSpeed, test);
    result |= process_if_exists(section, "power_loss", &CInifile::r_float, m_fPowerLoss, test);
    clamp(m_fPowerLoss, 0.0f, 1.0f);

    result |= process_if_exists(section, "additional_inventory_weight", &CInifile::r_float, m_additional_weight, test);
    result |= process_if_exists(section, "additional_inventory_weight2", &CInifile::r_float, m_additional_weight2, test);

    return result;
}

void CBackpack::net_Export(NET_Packet& P)
{
    inherited::net_Export(P);
    P.w_float_q8(GetCondition(), 0.0f, 1.0f);
}

void CBackpack::net_Import(NET_Packet& P)
{
    inherited::net_Import(P);
    float _cond;
    P.r_float_q8(_cond, 0.0f, 1.0f);
    SetCondition(_cond);
}

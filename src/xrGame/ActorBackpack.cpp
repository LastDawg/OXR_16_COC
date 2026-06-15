#include "StdAfx.h"
#include "ActorBackpack.h"
#include "Actor.h"
#include "Inventory.h"
#include "BoneProtections.h"
#include "Include/xrRender/Kinematics.h"

CBackpack::CBackpack()
{
   m_flags.set(FUsingCondition, TRUE);
}

bool CBackpack::net_Spawn(CSE_Abstract* DC)
{
    BOOL res = inherited::net_Spawn(DC);
    return (res);
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

void CBackpack::OnH_A_Chield()
{
    inherited::OnH_A_Chield();
}

void CBackpack::Load(pcstr section)
{
    inherited::Load(section);

    // Читаем секцию иммунитетов самого рюкзака
    if (pSettings->line_exist(section, "immunities_sect"))
    {
        LPCSTR imm_sect = pSettings->r_string(section, "immunities_sect");
        CHitImmunity::LoadImmunities(imm_sect, pSettings);
    }

    m_additional_weight  = pSettings->r_float(section, "additional_inventory_weight");
    m_additional_weight2 = pSettings->r_float(section, "additional_inventory_weight2");
    
    m_fPowerLoss = pSettings->read_if_exists<float>(section, "power_loss", 1.0f);
    clamp(m_fPowerLoss, 0.1f, 1.0f);

    m_fHealthRestoreSpeed    = READ_IF_EXISTS(pSettings, r_float, section, "health_restore_speed",    0.0f);
    m_fRadiationRestoreSpeed = READ_IF_EXISTS(pSettings, r_float, section, "radiation_restore_speed", 0.0f);
    m_fSatietyRestoreSpeed   = READ_IF_EXISTS(pSettings, r_float, section, "satiety_restore_speed",   0.0f);
    m_fThirstRestoreSpeed    = READ_IF_EXISTS(pSettings, r_float, section, "thirst_restore_speed",    0.0f);
    m_fPowerRestoreSpeed     = READ_IF_EXISTS(pSettings, r_float, section, "power_restore_speed",     0.0f);
    m_fBleedingRestoreSpeed  = READ_IF_EXISTS(pSettings, r_float, section, "bleeding_restore_speed",  0.0f);

    m_fJumpSpeed       = pSettings->read_if_exists<float>(section, "jump_speed", 1.f);
    m_fWalkAccel       = pSettings->read_if_exists<float>(section, "walk_accel", 1.f);
    m_fOverweightWalkK = pSettings->read_if_exists<float>(section, "overweight_walk_accel", 1.f);

    m_flags.set(FUsingCondition, pSettings->read_if_exists<bool>(section, "use_condition", true));
}

void CBackpack::Hit(float hit_power, ALife::EHitType hit_type)
{
    if (!IsUsingCondition()) return;

    float immunity_factor = CHitImmunity::GetHitImmunity(hit_type);
    
    ChangeCondition(-hit_power * immunity_factor);
}

float CBackpack::HitThroughArmor(float hit_power, s16 element, float ap, bool& add_wound, ALife::EHitType hit_type)
{
    float NewHitPower = hit_power;

    IGameObject* parent = H_Parent();
    if (!parent) return hit_power;
    IKinematics* V = smart_cast<IKinematics*>(parent->Visual());

    if (V) 
    {
        u16 spine  = V->LL_BoneID("bip01_spine");
        u16 spine1 = V->LL_BoneID("bip01_spine1");
        u16 spine2 = V->LL_BoneID("bip01_spine2");

        bool hit_in_back = (element == spine || element == spine1 || element == spine2);
        bool is_anomaly_hit = (hit_type == ALife::eHitTypeBurn || hit_type == ALife::eHitTypeRadiation || 
                               hit_type == ALife::eHitTypeChemicalBurn || hit_type == ALife::eHitTypeShock);

        if (hit_in_back || is_anomaly_hit) 
        {
            Hit(hit_power, hit_type);
        }
    }
    return NewHitPower;
}

bool CBackpack::install_upgrade_impl(pcstr section, bool test)
{
    bool result = inherited::install_upgrade_impl(section, test);

    result |= process_if_exists(section, "health_restore_speed", &CInifile::r_float, m_fHealthRestoreSpeed, test);
    result |= process_if_exists(section, "radiation_restore_speed", &CInifile::r_float, m_fRadiationRestoreSpeed, test);
    result |= process_if_exists(section, "satiety_restore_speed", &CInifile::r_float, m_fSatietyRestoreSpeed, test);
    result |= process_if_exists(section, "thirst_restore_speed", &CInifile::r_float, m_fThirstRestoreSpeed, test);
    result |= process_if_exists(section, "power_restore_speed", &CInifile::r_float, m_fPowerRestoreSpeed, test);
    result |= process_if_exists(section, "bleeding_restore_speed", &CInifile::r_float, m_fBleedingRestoreSpeed, test);
    result |= process_if_exists(section, "power_loss", &CInifile::r_float, m_fPowerLoss, test);
    clamp(m_fPowerLoss, 0.0f, 1.0f);

    result |= process_if_exists(section, "additional_inventory_weight", &CInifile::r_float, m_additional_weight, test);
    result |= process_if_exists(section, "additional_inventory_weight2", &CInifile::r_float, m_additional_weight2, test);
    result |= process_if_exists(section, "jump_speed", &CInifile::r_float, m_fJumpSpeed, test);
    result |= process_if_exists(section, "walk_accel", &CInifile::r_float, m_fWalkAccel, test);

    return result;
}

float CBackpack::GetPowerLoss() const
{
    return m_fPowerLoss;
}

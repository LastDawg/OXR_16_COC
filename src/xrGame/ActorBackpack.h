#pragma once
#include "inventory_item_object.h"

class CBackpack final : public CInventoryItemObject
{
    using inherited = CInventoryItemObject;

public:
    CBackpack();

    void Load(pcstr section) override;
    virtual void OnH_A_Chield();

    virtual void Hit(float P, ALife::EHitType hit_type);
    float HitThroughArmor(float hit_power, s16 element, float ap, bool& add_wound, ALife::EHitType hit_type);

    [[nodiscard]] float GetPowerLoss() const;

public:
    float m_additional_weight;
    float m_additional_weight2;
    float m_fPowerLoss;

    float m_fHealthRestoreSpeed;
    float m_fRadiationRestoreSpeed;
    float m_fSatietyRestoreSpeed;
    float m_fThirstRestoreSpeed;
    float m_fPowerRestoreSpeed;
    float m_fBleedingRestoreSpeed;

    float m_fJumpSpeed;
    float m_fWalkAccel;
    float m_fOverweightWalkK;

    virtual bool net_Spawn(CSE_Abstract* DC);
    virtual void net_Export(NET_Packet& P);
    virtual void net_Import(NET_Packet& P);

protected:
    bool install_upgrade_impl(pcstr section, bool test) override;

private:
    DECLARE_SCRIPT_REGISTER_FUNCTION(CGameObject);
};

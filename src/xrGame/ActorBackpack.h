#pragma once
#include "inventory_item_object.h"

class CBackpack final : public CInventoryItemObject
{
    using inherited = CInventoryItemObject;

public:
    CBackpack();

    void Load(pcstr section) override;

    virtual void Hit(float P, ALife::EHitType hit_type);

public:
    float m_additional_weight;
    float m_additional_weight2;
    float m_fPowerRestoreSpeed;
    float m_fPowerLoss;

    float m_fJumpSpeed;
    float m_fWalkAccel;
    float m_fOverweightWalkK;

    //shared_str m_BonesProtectionSect;

    virtual bool net_Spawn(CSE_Abstract* DC);
    virtual void net_Export(NET_Packet& P);
    virtual void net_Import(NET_Packet& P);

    //void ReloadBonesProtection();

protected:
    bool install_upgrade_impl(pcstr section, bool test) override;
};

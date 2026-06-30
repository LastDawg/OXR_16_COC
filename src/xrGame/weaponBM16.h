#pragma once

#include "WeaponShotgun.h"

class CWeaponBM16 : public CWeaponShotgun
{
    typedef CWeaponShotgun inherited;

public:
    virtual ~CWeaponBM16();
    virtual void Load(LPCSTR section);

protected:
    virtual void PlayAnimShoot();
    virtual void PlayAnimReload();
    virtual void PlayReloadSound();
    virtual void PlayAnimIdle();
    virtual void PlayAnimIdleMoving();
    virtual void PlayAnimIdleSprint();
    virtual void PlayAnimShow();
    virtual void PlayAnimHide();
    virtual void PlayAnimBore();
    virtual void PlayAnimSprintStart() override;
    virtual void PlayAnimSprintEnd() override;

    // Расклинивание
    virtual void switch2_Unmis();
    virtual void PlayAnimUnMisfire();

    // Системные функции
    virtual void Reload() override;
    virtual void OnAnimationEnd(u32 state) override; // Добавлен аргумент state
    virtual void OnStateSwitch(u32 S, u32 oldState) override; // Добавлены аргументы S и oldState

private:
    DECLARE_SCRIPT_REGISTER_FUNCTION(CWeaponShotgun);
};

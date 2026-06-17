#pragma once

namespace xray::render::RENDER_NAMESPACE
{
class CBlender_nightvision : public IBlender
{
public:
    virtual LPCSTR getComment() { return "nightvision"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_nightvision();
    virtual ~CBlender_nightvision();
};

class CBlender_pp_bloom : public IBlender
{
public:
    virtual LPCSTR getComment() { return "Nice bloom bro!"; }
    virtual BOOL canBeDetailed() { return FALSE; }
    virtual BOOL canBeLMAPped() { return FALSE; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_pp_bloom();
    virtual ~CBlender_pp_bloom();
};
} // namespace xray::render::RENDER_NAMESPACE

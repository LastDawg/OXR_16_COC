#pragma once

namespace xray::render::RENDER_NAMESPACE
{
class CBlender_FXAA : public IBlender
{
public:
    LPCSTR getComment() override { return "FXAA"; }
    BOOL canBeDetailed() override { return FALSE; }
    BOOL canBeLMAPped() override { return FALSE; }

    void Compile(CBlender_Compile& C) override;

    CBlender_FXAA();
    virtual ~CBlender_FXAA();
};
} // namespace xray::render::RENDER_NAMESPACE

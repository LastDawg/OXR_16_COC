#pragma once

namespace xray::render::RENDER_NAMESPACE
{
class CBlender_Hud_Blood: public IBlender
{
public:
	virtual		LPCSTR		getComment() { return "Hud Blood"; }
	virtual		BOOL		canBeDetailed() { return FALSE; }
	virtual		BOOL		canBeLMAPped() { return FALSE; }

	virtual		void		Compile(CBlender_Compile& C);

	CBlender_Hud_Blood();
	virtual ~CBlender_Hud_Blood();
};
} // namespace xray::render::RENDER_NAMESPACE

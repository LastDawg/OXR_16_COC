#include "stdafx.h"
#pragma hdrstop

#include "blender_hud_stamina.h"

namespace xray::render::RENDER_NAMESPACE
{
CBlender_Hud_Stamina::CBlender_Hud_Stamina() { description.CLS = 0; }
CBlender_Hud_Stamina::~CBlender_Hud_Stamina() {}

void CBlender_Hud_Stamina::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);
	switch (C.iElement)
	{
		case 0:
			C.r_Pass("stub_notransform_aa_AA", "hud_power", FALSE, FALSE, FALSE);
			C.r_dx11Texture("s_image", r2_RT_generic0);
			C.r_dx11Texture("s_hud_power", "shaders\\hud_mask\\hud_power");
			C.r_dx11Sampler("smp_rtlinear");
			C.r_End();
		break;
	}
}
} // namespace xray::render::RENDER_NAMESPACE

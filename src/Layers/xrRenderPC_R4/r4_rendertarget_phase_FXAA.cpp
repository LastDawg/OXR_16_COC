#include "stdafx.h"
#include "r4_rendertarget.h"

namespace xray::render::RENDER_NAMESPACE
{
void CRenderTarget::phase_fxaa()
{
    u32 Offset = 0;
    const float _w = float(Device.dwWidth);
    const float _h = float(Device.dwHeight);
    const float du = ps_r1_pps_u, dv = ps_r1_pps_v;

    ref_rt dest_rt = RImplementation.o.msaa ? rt_Generic : rt_Color;

    u_setrt(RCache, dest_rt, nullptr, nullptr, get_base_zb());

    FVF::V* pv = (FVF::V*)RImplementation.Vertex.Lock(4, g_fxaa->vb_stride, Offset);
    pv->set(du + 0, dv + float(_h), 0, 0, 1);
    pv++;
    pv->set(du + 0, dv + 0, 0, 0, 0);
    pv++;
    pv->set(du + float(_w), dv + float(_h), 0, 1, 1);
    pv++;
    pv->set(du + float(_w), dv + 0, 0, 1, 0);
    pv++;
    RImplementation.Vertex.Unlock(4, g_fxaa->vb_stride);

    RCache.set_Element(s_fxaa->E[0]);
    RCache.set_Geometry(g_fxaa);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    HW.get_context(CHW::IMM_CTX_ID)->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
}
} // namespace xray::render::RENDER_NAMESPACE

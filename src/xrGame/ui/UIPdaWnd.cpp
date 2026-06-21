#include "StdAfx.h"
#include "UIPdaWnd.h"
#include "PDA.h"

#include "xrUICore/XML/xrUIXmlParser.h"
#include "UIXmlInit.h"
#include "UIInventoryUtilities.h"

#include "Level.h"
#include "UIGameCustom.h"

#include "xrUICore/Static/UIStatic.h"
#include "xrUICore/Windows/UIFrameWindow.h"
#include "xrUICore/TabControl/UITabControl.h"
#include "UIMapWnd.h"
#include "xrUICore/Windows/UIFrameLineWnd.h"
#include "Common/object_broker.h"
#include "UIMessagesWindow.h"
#include "UIMainIngameWnd.h"
#include "xrUICore/TabControl/UITabButton.h"
#include "xrUICore/Static/UIAnimatedStatic.h"

#include "UIHelper.h"
#include "xrUICore/Hint/UIHint.h"
#include "xrUICore/Buttons/UIBtnHint.h"
#include "UITaskWnd.h"
#include "UIFactionWarWnd.h"
#include "UIActorInfo.h"
#include "UIRankingWnd.h"
#include "UILogsWnd.h"
#include "UIScriptWnd.h"
#include "Actor.h"
#include "Inventory.h"
#include "../xrEngine/XR_IOConsole.h"
#include "xrUICore/ProgressBar/UIProgressBar.h"
#include "player_hud.h"
#include "UIGameCustom.h"

constexpr const char* PDA_XML = "pda.xml";

u32 g_pda_info_state = 0;

void RearrangeTabButtons(CUITabControl* pTab);
CDialogHolder* CurrentDialogHolder();

CUIPdaWnd::CUIPdaWnd() : CUIDialogWnd(CUIPdaWnd::GetDebugType())
{
    pUIMapWnd = nullptr;
    pUITaskWnd = nullptr;
    pUIFactionWarWnd = nullptr;
    pUIActorInfo = nullptr;
    pUIRankingWnd = nullptr;
    pUILogsWnd = nullptr;
    m_hint_wnd = nullptr;
    // RTT PDA
    m_battery_bar = nullptr;
    m_power = 0.f;
    joystickrot.set(0.f, 0.f, 0.f);
    target_joystickrot.set(0.f, 0.f, 0.f);
    buttonpress = 0.f;
    target_buttonpress = 0.f;

    last_cursor_pos.set(UI_BASE_WIDTH / 2.f, UI_BASE_HEIGHT / 2.f);
    m_cursor_box.set(117.f, 39.f, UI_BASE_WIDTH - 121.f, UI_BASE_HEIGHT - 37.f);
    Init();
}

CUIPdaWnd::~CUIPdaWnd()
{
    if (pUIMapWnd)
        delete_data(pUIMapWnd);
    if (pUITaskWnd)
        delete_data(pUITaskWnd);
    if (pUIFactionWarWnd)
        delete_data(pUIFactionWarWnd);
    if (pUIActorInfo)
        delete_data(pUIActorInfo);
    if (pUIRankingWnd)
        delete_data(pUIRankingWnd);
    if (pUILogsWnd)
        delete_data(pUILogsWnd);
    delete_data(m_hint_wnd);
    if (UINoice)
        delete_data(UINoice);
}

void CUIPdaWnd::Init()
{
    CUIXml uiXml;
    uiXml.Load(CONFIG_PATH, UI_PATH, PDA_XML);

    m_pActiveDialog = nullptr;
    m_sActiveSection = "";

    CUIXmlInit::InitWindow(uiXml, "main", 0, this);

    UIMainPdaFrame = UIHelper::CreateStatic(uiXml, "background_static", this);

    CUIXmlInit::InitAutoStaticGroup(uiXml, "", 0, this);

    m_caption = UIHelper::CreateStatic(uiXml, "caption_static", this, false);
    m_caption_const = m_caption ? m_caption->GetText() : "";

    // Main buttons background
    CUIWindow* buttons_background = this;
    if (UIHelper::CreateFrameLine(uiXml, "mbbackground_frame_line", UIMainPdaFrame, false))
        buttons_background = UIMainPdaFrame; // SOC

    // Timer background
    m_clock = UIHelper::CreateStatic(uiXml, "clock_wnd", this, false);
    if (!m_clock)
    {
        const auto timer = UIHelper::CreateFrameLine(uiXml, "timer_frame_line", UIMainPdaFrame, false);
        if (timer && timer->GetTitleText())
            m_clock = timer->GetTitleText(); // SOC
    }

    if (uiXml.NavigateToNode("anim_static")) // XXX: Replace with UIHelper
    {
        auto* anim_static = xr_new<CUIAnimatedStatic>();
        AttachChild(anim_static);
        anim_static->SetAutoDelete(true);
        CUIXmlInit::InitAnimatedStatic(uiXml, "anim_static", 0, anim_static);
    }

    // RTT PDA
    /*m_btn_close = UIHelper::Create3tButton(uiXml, "close_button", this, false);
    if (m_btn_close)
    {
        m_btn_close->SetAccelerator(kUI_BACK, false, 2);
        UI().Focus().UnregisterFocusable(m_btn_close);
    }*/

    m_hint_wnd = UIHelper::CreateHint(uiXml, "hint_wnd", false);

    m_battery_bar = new CUIProgressBar();
    m_battery_bar->SetAutoDelete(true);
    AttachChild(m_battery_bar);
    CUIXmlInit::InitProgressBar(uiXml, "battery_bar", 0, m_battery_bar);
    m_battery_bar->Show(true);

    if (IsGameTypeSingle())
    {
        pUIMapWnd = xr_new<CUIMapWnd>(m_hint_wnd);
        if (!pUIMapWnd->Init("pda_map.xml", "map_wnd", false))
            xr_delete(pUIMapWnd);

        pUITaskWnd = xr_new<CUITaskWnd>(m_hint_wnd);
        if (!pUITaskWnd->Init())
            xr_delete(pUITaskWnd);

        pUIFactionWarWnd = xr_new<CUIFactionWarWnd>(m_hint_wnd);
        if (!pUIFactionWarWnd->Init())
            xr_delete(pUIFactionWarWnd);

        pUIActorInfo = xr_new<CUIActorInfoWnd>();
        if (!pUIActorInfo->Init())
            xr_delete(pUIActorInfo);

        pUIRankingWnd = xr_new<CUIRankingWnd>();
        if (!pUIRankingWnd->Init())
            xr_delete(pUIRankingWnd);

        pUILogsWnd = xr_new<CUILogsWnd>();
        if (!pUILogsWnd->Init())
            xr_delete(pUILogsWnd);
    }

    UITabControl = xr_new<CUITabControl>();
    UITabControl->SetAutoDelete(true);
    buttons_background->AttachChild(UITabControl);
    CUIXmlInit::InitTabControl(uiXml, "tab", 0, UITabControl, true, ShadowOfChernobylMode);
    UITabControl->SetMessageTarget(this);
    UITabControl->SetAcceleratorsMode(true);

    constexpr std::tuple<pcstr, pcstr> known_soc_tab_ids[] =
    {
        {"0", "eptTasks"},
        {"1", "eptMap"},
        {"2", "eptDiary"},
        {"3", "eptContacts"},
        {"4", "eptStalkersRanking"},
        {"5", "eptStatistics"},
        {"6", "eptEncyclopedia"},
    };

    for (u32 i = 0; i < UITabControl->GetTabsCount(); i++)
    {
        CUITabButton* btn = UITabControl->GetButtonByIndex(i);
        if (!btn || !btn->IsIdDefaultAssigned())
            continue;

        for (const auto& [id, replace] : known_soc_tab_ids)
        {
            if (btn->m_btn_id == id)
            {
                btn->m_btn_id = replace;
                break;
            }
        }
    }

    UINoice = xr_new<CUIStatic>("Noise");
    UINoice->SetAutoDelete(true);
    if (!CUIXmlInit::InitStatic(uiXml, "noice_static", 0, UINoice, false))
        xr_delete(UINoice);

    // XXX: dynamically determine if we need to rearrange the tabs
    if (ClearSkyMode)
        RearrangeTabButtons(UITabControl);
}

void CUIPdaWnd::SendMessage(CUIWindow* pWnd, s16 msg, void* pData)
{
    switch (msg)
    {
    case TAB_CHANGED:
    {
        if (pWnd == UITabControl)
        {
            const auto& id = UITabControl->GetActiveId();
            SetActiveSubdialog(id);
        }
        break;
    }
    case BUTTON_CLICKED:
    {
        if (m_btn_close && pWnd == m_btn_close)
        {
            if (Actor() && Actor()->inventory().GetActiveSlot() == PDA_SLOT)
                Actor()->inventory().Activate(NO_ACTIVE_SLOT);
        }
        break;
    }
    default:
    {
        if (m_pActiveDialog)
            m_pActiveDialog->SendMessage(pWnd, msg, pData);
    }
    };
}

bool CUIPdaWnd::OnMouseAction(float x, float y, EUIMessages mouse_action)
{
	switch (mouse_action)
	{
	case WINDOW_LBUTTON_DOWN:
	case WINDOW_RBUTTON_DOWN:
	case WINDOW_LBUTTON_UP:
	case WINDOW_RBUTTON_UP:
	{
		CPda* pda = Actor()->GetPDA();
		if (pda)
		{
			if (pda->IsPending())
				return true;

			if (mouse_action == WINDOW_LBUTTON_DOWN)
				bButtonL = true;
			else if (mouse_action == WINDOW_RBUTTON_DOWN)
				bButtonR = true;
			else if (mouse_action == WINDOW_LBUTTON_UP)
				bButtonL = false;
			else if (mouse_action == WINDOW_RBUTTON_UP)
				bButtonR = false;
		}
		break;
	}
	}
	CUIDialogWnd::OnMouseAction(x, y, mouse_action);
	return true; //always true because StopAnyMove() == false
}

void CUIPdaWnd::MouseMovement(float x, float y)
{
    CPda* pda = Actor() ? Actor()->GetPDA() : nullptr;
    if (!pda || !g_player_hud)
        return;

    x *= .1f;
    y *= .1f;
    clamp(x, -.15f, .15f);
    clamp(y, -.15f, .15f);

    if (_abs(x) < .05f)
        x = 0.f;

    if (_abs(y) < .05f)
        y = 0.f;

    bool buttonpressed = (bButtonL || bButtonR);

    target_buttonpress = (buttonpressed ? -.0015f : 0.f);
    target_joystickrot.set(x * -.75f, 0.f, y * .75f);

    x += y * pda->m_thumb_rot[0];
    y += x * pda->m_thumb_rot[1];

    g_player_hud->target_thumb0rot.set(y * .15f, y * -.05f, (x * -.15f) + (buttonpressed ? .002f : 0.f));
    g_player_hud->target_thumb01rot.set(0.f, 0.f, (x * -.25f) + (buttonpressed ? .01f : 0.f));
    g_player_hud->target_thumb02rot.set(0.f, 0.f, (x * .75f) + (buttonpressed ? .025f : 0.f));
}

void CUIPdaWnd::Show(bool status)
{
    inherited::Show(status);
    if (status)
    {
        InventoryUtilities::SendInfoToActor("ui_pda");

        if (!m_sActiveSection.empty()) // Пометка на fatal error, мало ли
            SetActiveSubdialog(m_sActiveSection);
        else
        {
            cpcstr subdialog = pUIMapWnd && !pUITaskWnd ? "eptMap" : "eptTasks";
            SetActiveSubdialog(subdialog);
            UITabControl->SetActiveTab(subdialog);
        }
        if (CurrentGameUI())
            CurrentGameUI()->HideActorMenu();
    }
    else
    {
        InventoryUtilities::SendInfoToActor("ui_pda_hide");
        CurrentGameUI()->UIMainIngameWnd->SetFlashIconState_(CUIMainIngameWnd::efiPdaTask, false);
        if (m_pActiveDialog)
        {
            m_pActiveDialog->Show(false);
            m_pActiveDialog = pUITaskWnd; //hack for script window
        }
        g_btnHint->Discard();
        g_statHint->Discard();
    }
}

void CUIPdaWnd::Update()
{
    inherited::Update();

    if (m_pActiveDialog)
        m_pActiveDialog->Update();

    if (m_clock)
    {
        auto time = GetGameTimeAsString(InventoryUtilities::etpTimeToMinutes);
        if (m_clock->GetParent() != this) // SOC
        {
            const auto date = GetGameDateAsString(InventoryUtilities::edpDateToDay, '/', true);
            xr_sprintf(time, "%s %s", time.c_str(), date.c_str());
        }
        m_clock->SetText(time.c_str());
    }

    if (m_battery_bar)
        m_battery_bar->SetProgressPos(m_power);

    if (pUILogsWnd)
        pUILogsWnd->PerformWork(); 
}

void CUIPdaWnd::SetActiveSubdialog(const shared_str& section)
{
    //if (m_sActiveSection == section)
    //    return;

    if (m_pActiveDialog)
    {
        if (UIMainPdaFrame->IsChild(m_pActiveDialog))
            UIMainPdaFrame->DetachChild(m_pActiveDialog);
        UIMainPdaFrame->SetKeyboardCapture(nullptr, true);
        m_pActiveDialog->Show(false);
    }

    const std::tuple<shared_str, pcstr, CUIWindow*> availableWindowsList[] =
    {
        { "eptMap",         nullptr, pUIMapWnd },
        { "eptTasks",       nullptr, pUITaskWnd },
        { "eptFractionWar", nullptr, pUIFactionWarWnd },
        { "eptStatistics",  "ui_pda_actor_info", pUIActorInfo },
        { "eptRanking",     nullptr, pUIRankingWnd },
        { "eptLogs",        nullptr, pUILogsWnd },
    };

    pcstr soc_infoportion{};
    for (const auto& [id, info, wnd] : availableWindowsList)
    {
        if (section == id && wnd)
        {
            m_pActiveDialog = wnd;
            soc_infoportion = info;
            break;
        }
    }

    luabind::functor<CUIDialogWndEx*> functor;
    if (Device.dwPrecacheFrame == 0)
    {
        if (GEnv.ScriptEngine->functor("pda.set_active_subdialog", functor))
        {
            if (CUIDialogWndEx* scriptWnd = functor(section.c_str()))
            {
                scriptWnd->SetHolder(CurrentDialogHolder());
                m_pActiveDialog = scriptWnd;
            }
        }
    }

    if (m_pActiveDialog)
    {
        InventoryUtilities::SendInfoToActor(section.c_str()); // X-Ray extensions
        if (soc_infoportion)
            InventoryUtilities::SendInfoToActor(soc_infoportion); // SOC

        if (!UIMainPdaFrame->IsChild(m_pActiveDialog))
            UIMainPdaFrame->AttachChild(m_pActiveDialog);
        UIMainPdaFrame->SetKeyboardCapture(m_pActiveDialog, true);
        m_pActiveDialog->Show(true);
        m_sActiveSection = section;
        SetActiveCaption();
    }
    else
    {
        m_sActiveSection = "";
    }
}

void CUIPdaWnd::SetActiveCaption()
{
    if (!m_caption)
        return;

    TABS_VECTOR* btn_vec = UITabControl->GetButtonsVector();
    TABS_VECTOR::iterator it_b = btn_vec->begin();
    TABS_VECTOR::iterator it_e = btn_vec->end();
    for (; it_b != it_e; ++it_b)
    {
        if ((*it_b)->m_btn_id == m_sActiveSection)
        {
            LPCSTR cur = (*it_b)->TextItemControl()->GetText();
            string256 buf;
            strconcat(sizeof(buf), buf, m_caption_const.c_str(), cur);
            SetCaption(buf);
            UITabControl->Show(true);
            m_clock->Show(true);
            m_caption->Show(true);
            m_battery_bar->Show(true);
            return;
        }
    }
	UITabControl->Show(false);
    m_clock->Show(false);
    m_caption->Show(false);
    m_battery_bar->Show(false);
}

void CUIPdaWnd::Show_SecondTaskWnd(bool status)
{
    if (pUITaskWnd)
    {
        if (status)
        {
            SetActiveSubdialog("eptTasks");
        }
        pUITaskWnd->Show_TaskListWnd(status);
    }
}

void CUIPdaWnd::Show_MapWnd(bool status)
{
    if (pUIMapWnd)
    {
        if (status)
            SetActiveSubdialog("eptMap");
    }
}

void CUIPdaWnd::Show_ContactsWnd(bool status)
{
    if (true) // XXX: replace with contacts wnd pointer
    {
        if (status)
            SetActiveSubdialog("eptContacts");
    }
}

#include "xrUICore/Cursor/UICursor.h"

void CUIPdaWnd::ResetCursor()
{
    if (!last_cursor_pos.similar({0.f, 0.f}))
        GetUICursor().SetUICursorPosition(last_cursor_pos);
}

void CUIPdaWnd::Draw()
{
    if (Device.dwFrame == dwPDAFrame)
        return;

    dwPDAFrame = Device.dwFrame;

    inherited::Draw();
    //.	DrawUpdatedSections();
    DrawHint();
    if (UINoice)
        UINoice->Draw(); // over all
}

void CUIPdaWnd::DrawHint()
{
    if (m_pActiveDialog == pUITaskWnd && pUITaskWnd)
        pUITaskWnd->DrawHint();
    else if (m_pActiveDialog == pUIMapWnd && pUIMapWnd)
        pUIMapWnd->DrawHint();
    else if (m_pActiveDialog == pUIRankingWnd && pUIRankingWnd)
        pUIRankingWnd->DrawHint();

    if (m_hint_wnd)
        m_hint_wnd->Draw();
}

bool CUIPdaWnd::NeedCursor() const
{
    if (m_pActiveDialog && m_pActiveDialog->IsUsingCursorRightNow())
       return true;

    return CUIDialogWnd::NeedCursor();
}

void CUIPdaWnd::UpdatePda()
{
    if (pUILogsWnd)
        pUILogsWnd->UpdateNews();

    if (m_pActiveDialog == pUITaskWnd && pUITaskWnd)
    {
        pUITaskWnd->ReloadTaskInfo();
    }
}

void CUIPdaWnd::UpdateRankingWnd()
{
    if (pUIRankingWnd)
        pUIRankingWnd->Update();
}

void CUIPdaWnd::Reset()
{
    inherited::ResetAll();

    if (pUIMapWnd)
        pUIMapWnd->Reset();
    if (pUITaskWnd)
        pUITaskWnd->ResetAll();
    if (pUIFactionWarWnd)
        pUIFactionWarWnd->ResetAll();
    if (pUIActorInfo)
        pUIActorInfo->ResetAll();
    if (pUIRankingWnd)
        pUIRankingWnd->ResetAll();
    if (pUILogsWnd)
        pUILogsWnd->ResetAll();
}

void CUIPdaWnd::SetCaption(pcstr text)
{
    if (m_caption)
        m_caption->SetText(text);
}

void RearrangeTabButtons(CUITabControl* pTab)
{
    const auto& buttons = *pTab->GetButtonsVector();

    Fvector2 pos;
    pos.set(buttons.front()->GetWndPos());

    for (const auto& btn : buttons)
    {
        btn->SetWndPos(pos);
        btn->AdjustWidthToText();
        const float size_x = btn->GetWidth() + 30.0f;
        btn->SetWidth(size_x);
        pos.x += size_x - 6.0f;
    }

    pTab->SetWidth(pos.x + 5.0f);
    pos.x = pTab->GetWndPos().x - pos.x;
    pos.y = pTab->GetWndPos().y;
    pTab->SetWndPos(pos);
}

void CUIPdaWnd::Enable(bool status)
{
        if (status)
            ResetCursor();
        else
        {
            g_player_hud->reset_thumb(false);
            ResetJoystick(false);
            bButtonL = false;
            bButtonR = false;
        }

    inherited::Enable(status);
}

bool CUIPdaWnd::OnKeyboardAction(int dik, EUIMessages keyboard_action)
{
    if (WINDOW_KEY_PRESSED == keyboard_action && IsShown())
    {
        if (!psActorFlags.test(AF_3D_PDA))
        {
            EGameActions action = GetBindedAction(dik);

            if (action == kQUIT || action == kINVENTORY || action == kACTIVE_JOBS)
            {
                HideDialog();
                return true;
            }
        }
        else
        {
            CPda* pda = Actor()->GetPDA();
            if (pda)
            {
                EGameActions action = GetBindedAction(dik);

                if (action == kQUIT) // "Hack" to make Esc key open main menu instead of simply hiding the PDA UI
                {
                    if (pda->GetState() == CPda::eHiding || pda->GetState() == CPda::eHidden)
                    {
                        HideDialog();
                        Console->Execute("main_menu");
                    }
                    else
                        Actor()->inventory().Activate(NO_ACTIVE_SLOT);

                    return true;
                }

                if (action == kUSE || action == kACTIVE_JOBS || action == kINVENTORY || (action > kCAM_ZOOM_OUT && action < kWPN_NEXT)) // Since UI no longer passes non-movement inputs to // the actor input receiver this is needed now.
                {
                    IGameObject* obj = (GameID() == eGameIDSingle) ? Level().CurrentEntity() : Level().CurrentControlEntity();
                    {
                        IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(obj));
                        if (IR)
                            IR->IR_OnKeyboardPress(action);
                    }
                    return true;
                }

                // Don't allow zoom in while draw/holster animation plays, freelook is enabled or a hand animation plays
                if (pda->IsPending())
                    return false;

                // Simple PDA input mode - only allow input if PDA is zoomed in. Both left and right mouse button will
                // zoom in instead of only right mouse button
                if (psActorFlags.test(AF_SIMPLE_PDA))
                {
                    if (action == kWPN_RELOAD || (!IsEnabled() && action == kWPN_ZOOM))
                    {
                        if (!pda->m_bZoomed)
                        {
                            Actor()->StopSprint();

							// Input state change must be deferred because actor state can still be sprinting when activating which would instantly deactivate input again
							pda->m_eDeferredEnable = CPda::eDeferredEnableState::eEnableZoomed;
                        }
                        else
                            Enable(false);

                        pda->m_bZoomed = !pda->m_bZoomed;
                        return true;
                    }
                }
                // "Normal" input mode, PDA input can be toggled without having to be zoomed in
                else
                {
                    if (action == kWPN_RELOAD || (!IsEnabled() && action == kWPN_ZOOM))
                    {
                        if (!pda->m_bZoomed && !IsEnabled())
                        {
                            Actor()->StopSprint();

							// Input state change must be deferred because actor state can still be sprinting when activating which would instantly deactivate input again
							pda->m_eDeferredEnable = CPda::eDeferredEnableState::eEnableZoomed;
                        }

                        pda->m_bZoomed = !pda->m_bZoomed;
                        return true;
                    }
                    
                    if (action == kWPN_FUNC || (!IsEnabled() && action == kWPN_FIRE))
                    {
                        if (IsEnabled())
                        {
                            pda->m_bZoomed = false;
                            Enable(false);
                        }
                        else
                        {
                            Actor()->StopSprint();

							// Input state change must be deferred because actor state can still be sprinting when activating which would instantly deactivate input again
							pda->m_eDeferredEnable = CPda::eDeferredEnableState::eEnable;
                        }
                        return true;
                    }
                }
            }
        }
    }

    return inherited::OnKeyboardAction(dik, keyboard_action);
}

bool CUIPdaWnd::OnControllerAction(int axis, const ControllerAxisState& state, EUIMessages controller_action)
{
    if (inherited::OnControllerAction(axis, state, controller_action))
        return true;

    return false;
}

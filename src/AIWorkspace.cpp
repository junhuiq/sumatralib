/* Copyright 2025 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Win.h"
#include "base/Http.h"
#include "base/File.h"
#include "base/Dpi.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/WinGui.h"
#include "wingui/LabelWithCloseWnd.h"

#include "Settings.h"
#include "GlobalPrefs.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "Theme.h"

#include "MainWindow.h"
#include "SumatraPDF.h"
#include "Translations.h"
#include "resource.h"
#include "EngineBase.h"
#include "DocController.h"
#include "WindowTab.h"
#include "AIWorkspace.h"
#include "base/Log.h"

static HWND AddButtonTooltip(HWND hwndParent, HWND hwndButton, Str tipText) {
    HWND hwndTip =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndButton, nullptr, GetModuleHandle(nullptr), nullptr);
    if (hwndTip) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = hwndParent;
        ti.uId = (UINT_PTR)hwndButton;
        ti.lpszText = CWStrTemp(tipText);
        SendMessageW(hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(hwndTip, TTM_SETMAXTIPWIDTH, 0, 300);
    }
    return hwndTip;
}

static void UpdateAIWorkspaceOkBtn(MainWindow* win) {
    if (!win->hwndAIWorkspaceOkBtn) {
        return;
    }

    bool hasModel = gGlobalPrefs && gGlobalPrefs->aiModel.apiUrl.s && gGlobalPrefs->aiModel.apiKey.s &&
                    gGlobalPrefs->aiModel.modelName.s && gGlobalPrefs->aiModel.apiUrl.len > 0 &&
                    gGlobalPrefs->aiModel.apiKey.len > 0 && gGlobalPrefs->aiModel.modelName.len > 0;
    // button is always enabled when model is configured; empty input is handled in AICallSendPrompt
    EnableWindow(win->hwndAIWorkspaceOkBtn, hasModel);
    InvalidateRect(win->hwndAIWorkspaceOkBtn, nullptr, TRUE);
}

static void UpdateAIWorkspaceNoteBtn(MainWindow* win) {
    if (!win->hwndAIWorkspaceNoteBtn) {
        return;
    }
    // always enable so tooltip shows on hover; SaveNotesToFile checks for preconditions
    EnableWindow(win->hwndAIWorkspaceNoteBtn, TRUE);
    InvalidateRect(win->hwndAIWorkspaceNoteBtn, nullptr, TRUE);
}

void AICallSendPrompt(MainWindow* win) {
    if (!win->aiWorkspaceInput || !win->aiWorkspaceInput->hwnd) {
        return;
    }
    TempStr prompt = HwndGetTextTemp(win->aiWorkspaceInput->hwnd);
    if (prompt.len == 0) {
        return;
    }

    // show "thinking..." in reply area
    HwndSetText(win->hwndAIWorkspaceReply, StrL("思考中..."));

    // parse URL
    Str url = gGlobalPrefs->aiModel.apiUrl;
    Str server = url;
    int port = 443;
    Str path = StrL("/");

    if (str::StartsWith(url, StrL("https://"))) {
        server = Str(url.s + 8, url.len - 8);
        port = 443;
    } else if (str::StartsWith(url, StrL("http://"))) {
        server = Str(url.s + 7, url.len - 7);
        port = 80;
    }

    Str slashStr = str::FindChar(server, '/');
    if (slashStr.s) {
        const char* slash = slashStr.s;
        path = Str(slash, (int)(server.s + server.len - slash));
        server = Str(server.s, (int)(slash - server.s));
    } else {
        // user only gave a base URL — auto-append standard chat completions path
        path = StrL("/v1/chat/completions");
    }

    // handle custom port (e.g. localhost:11434)
    Str colonStr = str::FindChar(server, ':');
    if (colonStr.s) {
        const char* colon = colonStr.s;
        int customPort = 0;
        for (const char* p = colon + 1; p < server.s + server.len && *p >= '0' && *p <= '9'; p++) {
            customPort = customPort * 10 + (*p - '0');
        }
        if (customPort > 0 && customPort < 65536) {
            port = customPort;
        }
        server = Str(server.s, (int)(colon - server.s));
    }

    // build request body
    StrBuilder body;
    body.Append("{\"model\":\"");
    body.Append(gGlobalPrefs->aiModel.modelName);
    body.Append("\",\"messages\":[{\"role\":\"user\",\"content\":\"");
    // simple JSON escaping for prompt
    for (int i = 0; i < prompt.len; i++) {
        char c = prompt.s[i];
        if (c == '"') {
            body.Append("\\\"");
        } else if (c == '\\') {
            body.Append("\\\\");
        } else if (c == '\n') {
            body.Append("\\n");
        } else if (c == '\r') {
            body.Append("\\r");
        } else if (c == '\t') {
            body.Append("\\t");
        } else {
            body.Append(&c, 1);
        }
    }
    body.Append("\"}]}");

    // build headers
    StrBuilder headers;
    headers.Append("Content-Type: application/json\r\n");
    headers.Append(fmt("Content-Length: %d\r\n", (int)body.size()).s);
    headers.Append("Authorization: Bearer ");
    headers.Append(gGlobalPrefs->aiModel.apiKey);
    headers.Append("\r\n");

    // make the API call
    StrBuilder resp(4096);
    bool ok = HttpPostWithResp(server, port, path, &headers, &body, &resp, 120 * 1000);

    if (ok) {
        // parse JSON response: extract content between "content":" and the next "
        Str respStr = resp.Get();
        Str found = str::FindFrom(respStr, StrL("\"content\":\""));
        if (found.s) {
            const char* p = found.s + LenL("\"content\":\"");
            StrBuilder content;
            while (*p) {
                if (*p == '"' && (p == respStr.s || *(p - 1) != '\\')) {
                    break;
                }
                if (*p == '\\' && *(p + 1) == 'n') {
                    content.Append("\r\n");
                    p += 2;
                    continue;
                }
                if (*p == '\\' && *(p + 1) == '"') {
                    content.Append("\"");
                    p += 2;
                    continue;
                }
                if (*p == '\\' && *(p + 1) == '\\') {
                    content.Append("\\");
                    p += 2;
                    continue;
                }
                content.Append(p, 1);
                p++;
            }
            HwndSetText(win->hwndAIWorkspaceReply, content.Get());
        } else {
            HwndSetText(win->hwndAIWorkspaceReply, respStr);
        }
    } else {
        TempStr errMsg = fmt("请求失败: %s", resp.Get());
        HwndSetText(win->hwndAIWorkspaceReply, errMsg);
    }
}

void ExplainSelectedText(MainWindow* win, Str selectedText, bool inDepth) {
    if (!selectedText || selectedText.len == 0) {
        return;
    }

    TempStr prefix = inDepth ? str::DupTemp(_TRA("Please explain in depth:")) : str::DupTemp(_TRA("Please briefly explain:"));
    TempStr prompt = fmt("%s %s", prefix, selectedText);

    // ensure AI workspace is visible
    if (!win->hwndAIWorkspaceBox) {
        CreateAIWorkspacePanel(win);
    }
    if (!win->aiWorkspaceVisible) {
        ToggleAIWorkspace(win);
    }

    HwndSetText(win->aiWorkspaceInput->hwnd, prompt);

    // only auto-send if AI model is configured
    bool hasModel = gGlobalPrefs && gGlobalPrefs->aiModel.apiUrl.s && gGlobalPrefs->aiModel.apiKey.s &&
                    gGlobalPrefs->aiModel.modelName.s && gGlobalPrefs->aiModel.apiUrl.len > 0 &&
                    gGlobalPrefs->aiModel.apiKey.len > 0 && gGlobalPrefs->aiModel.modelName.len > 0;
    if (hasModel) {
        AICallSendPrompt(win);
    }
}

static bool SaveNotesToFile(MainWindow* win) {
    WindowTab* tab = win->CurrentTab();
    if (!tab || !tab->filePath || !tab->ctrl) {
        return false;
    }

    // derive .md path from book path
    TempStr mdPath = fmt("%s.md", path::GetPathNoExtTemp(tab->filePath));

    // get text from input
    TempStr text = HwndGetTextTemp(win->aiWorkspaceInput->hwnd);
    if (text.len == 0) {
        return false;
    }

    int pageNo = tab->ctrl->CurrentPageNo();

    SYSTEMTIME st;
    GetLocalTime(&st);
    TempStr dateStr =
        fmt("%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    // build note entry: first line is bold title
    // **First line**
    // Rest of text
    //
    // Page: N | Date: YYYY-MM-DD HH:MM:SS
    // ---
    const char* firstLineEnd = text.s;
    while (firstLineEnd < text.s + text.len && *firstLineEnd != '\n' && *firstLineEnd != '\r') {
        firstLineEnd++;
    }
    Str firstLine(text.s, (int)(firstLineEnd - text.s));

    StrBuilder entry;
    entry.Append("**");
    entry.Append(firstLine);
    entry.Append("**\n\n");

    // append remaining lines (skip the first line's newline)
    const char* rest = firstLineEnd;
    while (rest < text.s + text.len && (*rest == '\n' || *rest == '\r')) {
        rest++;
    }
    if (rest < text.s + text.len) {
        entry.Append(rest, (int)(text.s + text.len - rest));
        entry.Append("\n\n");
    }

    entry.Append(fmt("Page: %d | %s\n\n---\n\n", pageNo, dateStr));

    // read existing content if file exists, then append
    StrBuilder fileContent;
    if (file::Exists(mdPath)) {
        ByteSlice existing = file::ReadFile(mdPath);
        fileContent.Append(Str((const char*)existing.data(), (int)existing.size()));
        free(existing.data());
    }
    fileContent.Append(entry.Get());

    bool ok = file::WriteFile(mdPath, fileContent.AsByteSlice());

    // clear input on success
    if (ok) {
        HwndSetText(win->aiWorkspaceInput->hwnd, StrL(""));
    }
    return ok;
}

void TakeNotesFromSelection(MainWindow* win, Str selectedText) {
    if (!selectedText || selectedText.len == 0) {
        return;
    }

    // ensure AI workspace is visible
    if (!win->hwndAIWorkspaceBox) {
        CreateAIWorkspacePanel(win);
    }
    if (!win->aiWorkspaceVisible) {
        ToggleAIWorkspace(win);
    }

    // fill text in input but don't auto-send
    HwndSetText(win->aiWorkspaceInput->hwnd, selectedText);
    UpdateAIWorkspaceNoteBtn(win);
}

static LRESULT CALLBACK WndProcAIWorkspaceInput(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uIdSubclass,
                                                DWORD_PTR dwRefData) {
    MainWindow* win = (MainWindow*)dwRefData;
    if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE) {
        UpdateAIWorkspaceNoteBtn(win);
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN && (GetKeyState(VK_SHIFT) & 0x8000) &&
        !(GetKeyState(VK_CONTROL) & 0x8000)) {
        if (IsWindowEnabled(win->hwndAIWorkspaceOkBtn)) {
            AICallSendPrompt(win);
        }
        return 0;
    }
    if (msg == WM_CHAR && wp == VK_RETURN && (GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000)) {
        return 0; // eat the Enter, handled in WM_KEYDOWN
    }
    LRESULT res = DefSubclassProc(hwnd, msg, wp, lp);
    return res;
}

static LRESULT CALLBACK WndProcAIWorkspaceBox(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR uIdSubclass,
                                              DWORD_PTR dwRefData) {
    MainWindow* win = (MainWindow*)dwRefData;
    if (msg == WM_ERASEBKGND) {
        if (win->brControlBgColor) {
            RECT r;
            GetClientRect(hwnd, &r);
            FillRect((HDC)wp, &r, win->brControlBgColor);
        }
        return TRUE;
    }
    if (msg == WM_DRAWITEM) {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        bool isNote = (dis->hwndItem == win->hwndAIWorkspaceNoteBtn);
        bool isAsk = (dis->hwndItem == win->hwndAIWorkspaceOkBtn);
        if (!isNote && !isAsk) {
            return FALSE;
        }
        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;

        // background
        COLORREF bgCol = ThemeControlBackgroundColor();
        HBRUSH hbr = CreateSolidBrush(bgCol);
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);

        // hover/pressed effect
        if (dis->itemState & ODS_SELECTED) {
            COLORREF c = RGB((GetRValue(bgCol) * 3) / 4, (GetGValue(bgCol) * 3) / 4, (GetBValue(bgCol) * 3) / 4);
            HBRUSH hp = CreateSolidBrush(c);
            RECT rp = rc;
            InflateRect(&rp, -1, -1);
            FillRect(hdc, &rp, hp);
            DeleteObject(hp);
        } else if (dis->itemState & ODS_HOTLIGHT) {
            int r2 = GetRValue(bgCol) + 40;
            int g2 = GetGValue(bgCol) + 40;
            int b2 = GetBValue(bgCol) + 40;
            COLORREF c = RGB(r2 > 255 ? 255 : r2, g2 > 255 ? 255 : g2, b2 > 255 ? 255 : b2);
            HBRUSH hh = CreateSolidBrush(c);
            RECT rh = rc;
            InflateRect(&rh, -1, -1);
            FillRect(hdc, &rh, hh);
            DeleteObject(hh);
        }

        // focus rect
        if (dis->itemState & ODS_FOCUS) {
            RECT rf = rc;
            InflateRect(&rf, -2, -2);
            DrawFocusRect(hdc, &rf);
        }

        // draw the JPEG icon centered, scaled to fit with 3px padding
        auto* bmp = (Gdiplus::Bitmap*)(isNote ? win->aiNoteIcon : win->aiAskIcon);
        if (bmp) {
            int pad = DpiScale(win->hwndFrame, 3);
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            Gdiplus::Rect destRect(rc.left + pad, rc.top + pad, rc.right - rc.left - pad * 2,
                                   rc.bottom - rc.top - pad * 2);
            g.DrawImage(bmp, destRect);
        }
        return TRUE;
    }
    if (msg == WM_SIZE) {
        RelayoutAIWorkspace(win);
        return 0;
    }
    // relay mouse messages to tooltip controls so tooltips show on hover
    if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN ||
        msg == WM_RBUTTONUP || msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP) {
        MSG m = {};
        m.hwnd = hwnd;
        m.message = msg;
        m.wParam = wp;
        m.lParam = lp;
        m.time = GetMessageTime();
        DWORD pos = GetMessagePos();
        m.pt.x = GET_X_LPARAM(pos);
        m.pt.y = GET_Y_LPARAM(pos);
        if (win->hwndAINoteTip) {
            SendMessageW(win->hwndAINoteTip, TTM_RELAYEVENT, 0, (LPARAM)&m);
        }
        if (win->hwndAIAskTip) {
            SendMessageW(win->hwndAIAskTip, TTM_RELAYEVENT, 0, (LPARAM)&m);
        }
    }
    if (msg == WM_COMMAND) {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);
        if (id == IDC_AI_WORKSPACE_LABEL_CLOSE && code == 0) {
            ToggleAIWorkspace(win);
            return 0;
        }
        if (id == IDC_AI_WORKSPACE_OK_BTN && code == BN_CLICKED) {
            AICallSendPrompt(win);
            return 0;
        }
        if (id == IDC_AI_WORKSPACE_NOTE_BTN && code == BN_CLICKED) {
            SaveNotesToFile(win);
            UpdateAIWorkspaceNoteBtn(win);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void RelayoutAIWorkspace(MainWindow* win) {
    if (!win->hwndAIWorkspaceBox) {
        return;
    }
    Rect rc = ClientRect(win->hwndAIWorkspaceBox);
    if (rc.IsEmpty()) {
        return;
    }

    int labelDy = 60;
    int padding = 4;
    int inputSidePad = 8;
    int btnReplyGap = 60;
    int inputDy = 400;

    // label at top (height matches My Library sidebar)
    if (win->aiWorkspaceLabelWithClose) {
        HWND hwndLabel = win->aiWorkspaceLabelWithClose->hwnd;
        MoveWindow(hwndLabel, rc.x + padding, rc.y + padding, rc.dx - padding * 2, labelDy, TRUE);
    }

    // input area below label
    int inputTop = rc.y + labelDy + padding * 2;
    if (win->aiWorkspaceInput && win->aiWorkspaceInput->hwnd) {
        MoveWindow(win->aiWorkspaceInput->hwnd, rc.x + inputSidePad, inputTop, rc.dx - inputSidePad * 2, inputDy, TRUE);
    }

    // Two icon buttons below input, centered
    int btnTop = inputTop + inputDy + padding;
    int iconBtnSize = DpiScale(win->hwndFrame, 24);
    int btnGap = DpiScale(win->hwndFrame, 6);
    int totalBtnW = iconBtnSize * 2 + btnGap;
    int btnLeft = rc.x + (rc.dx - totalBtnW) / 2;

    if (win->hwndAIWorkspaceNoteBtn) {
        MoveWindow(win->hwndAIWorkspaceNoteBtn, btnLeft, btnTop, iconBtnSize, iconBtnSize, TRUE);
    }
    if (win->hwndAIWorkspaceOkBtn) {
        MoveWindow(win->hwndAIWorkspaceOkBtn, btnLeft + iconBtnSize + btnGap, btnTop, iconBtnSize, iconBtnSize, TRUE);
    }

    // reply area with gap from button
    int replyTop = btnTop + iconBtnSize + btnReplyGap;
    if (win->hwndAIWorkspaceReply) {
        MoveWindow(win->hwndAIWorkspaceReply, rc.x + inputSidePad, replyTop, rc.dx - inputSidePad * 2,
                   rc.dy - replyTop - padding, TRUE);
    }
}

static constexpr int kAIWorkspaceMinDx = 150;

static void OnAIWorkspaceSplitterMove(Splitter::MoveEvent* ev) {
    Splitter* splitter = ev->w;
    MainWindow* win = FindMainWindowByHwnd(splitter->hwnd);
    if (!win) {
        return;
    }
    Point pcur = HwndGetCursorPos(win->hwndFrame);
    Rect rFrame = ClientRect(win->hwndFrame);
    int dx = rFrame.dx - pcur.x;
    if (dx < kAIWorkspaceMinDx || dx > rFrame.dx / 2) {
        ev->resizeAllowed = false;
        return;
    }
    win->aiWorkspaceDx = dx;
    gGlobalPrefs->aiWorkspaceDx = dx;
    RelayoutWindow(win);
    // paint synchronously during live drag so child controls (input, button,
    // reply) don't appear stale while WM_MOUSEMOVE starves WM_PAINT
    RedrawWindow(win->hwndFrame, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void CreateAIWorkspacePanel(MainWindow* win) {
    if (win->hwndAIWorkspaceBox) {
        return; // already created
    }
    HMODULE hmod = GetModuleHandle(nullptr);
    int dx = 200;
    DWORD style = WS_CHILD | WS_CLIPCHILDREN;
    HWND parent = win->hwndFrame;
    win->hwndAIWorkspaceBox = CreateWindowExW(0, WC_STATIC, L"", style, 0, 0, dx, 0, parent, nullptr, hmod, nullptr);

    // splitter
    {
        Splitter::CreateArgs args;
        args.parent = win->hwndFrame;
        args.type = SplitterType::Vert;
        args.isLive = true;
        win->aiWorkspaceSplitter = new Splitter();
        win->aiWorkspaceSplitter->onMove = MkFunc1Void(OnAIWorkspaceSplitterMove);
        win->aiWorkspaceSplitter->Create(args);
    }

    // label with close button
    auto label = new LabelWithCloseWnd();
    {
        LabelWithCloseWnd::CreateArgs args;
        args.parent = win->hwndAIWorkspaceBox;
        args.cmdId = IDC_AI_WORKSPACE_LABEL_CLOSE;
        args.isRtl = IsUIRtl();
        args.font = GetAppSidebarLabelFont();
        label->Create(args);
    }
    win->aiWorkspaceLabelWithClose = label;
    label->SetPaddingXY(2, 2);
    HwndSetText(label->hwnd, _TRA("AI Assistant"));

    // input box (multi-line), slightly narrower than the workspace
    auto input = new Edit();
    {
        Edit::CreateArgs args;
        args.parent = win->hwndAIWorkspaceBox;
        args.isMultiLine = true;
        args.idealSizeLines = 3;
        args.withBorder = true;
        args.cueText = _TRA("Enter prompt...");
        input->Create(args);
    }
    win->aiWorkspaceInput = input;

    // reply area (read-only multi-line edit) — created before button so button is on top
    win->hwndAIWorkspaceReply =
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, dx, 200,
                        win->hwndAIWorkspaceBox, (HMENU)(UINT_PTR)IDC_AI_WORKSPACE_REPLY, hmod, nullptr);
    SendMessageW(win->hwndAIWorkspaceReply, WM_SETFONT, (WPARAM)GetDefaultGuiFont(), TRUE);

    int iconBtnSize = DpiScale(win->hwndFrame, 24);

    // Load JPEG icons via GDI+
    TempStr exeDir = GetAppDataDirTemp();
    {
        TempStr path = path::JoinTemp(exeDir, StrL("takenotes.jpg"));
        TempWStr pathW = ToWStrTemp(path);
        auto* bmp = new Gdiplus::Bitmap(pathW.s);
        if (bmp->GetLastStatus() == Gdiplus::Ok) {
            win->aiNoteIcon = bmp;
        } else {
            delete bmp;
        }
    }
    {
        TempStr path = path::JoinTemp(exeDir, StrL("askAI.jpg"));
        TempWStr pathW = ToWStrTemp(path);
        auto* bmp = new Gdiplus::Bitmap(pathW.s);
        if (bmp->GetLastStatus() == Gdiplus::Ok) {
            win->aiAskIcon = bmp;
        } else {
            delete bmp;
        }
    }

    // Take Notes button — owner-draw icon button
    win->hwndAIWorkspaceNoteBtn =
        CreateWindowExW(0, L"BUTTON", L"",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 0, 0, iconBtnSize, iconBtnSize,
                        win->hwndAIWorkspaceBox, (HMENU)(UINT_PTR)IDC_AI_WORKSPACE_NOTE_BTN, hmod, nullptr);
    win->hwndAINoteTip = AddButtonTooltip(win->hwndAIWorkspaceBox, win->hwndAIWorkspaceNoteBtn, _TRA("Save notes"));

    // Ask AI button — owner-draw icon button
    win->hwndAIWorkspaceOkBtn =
        CreateWindowExW(0, L"BUTTON", L"",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 0, 0, iconBtnSize, iconBtnSize,
                        win->hwndAIWorkspaceBox, (HMENU)(UINT_PTR)IDC_AI_WORKSPACE_OK_BTN, hmod, nullptr);
    win->hwndAIAskTip = AddButtonTooltip(win->hwndAIWorkspaceBox, win->hwndAIWorkspaceOkBtn, _TRA("Ask AI"));

    // subclass input for Enter key and text change detection
    UINT_PTR inputSubclassId = NextSubclassId();
    SetWindowSubclass(input->hwnd, WndProcAIWorkspaceInput, inputSubclassId, (DWORD_PTR)win);

    // subclass box for WM_SIZE and WM_COMMAND
    win->aiWorkspaceBoxSubclassId = NextSubclassId();
    SetWindowSubclass(win->hwndAIWorkspaceBox, WndProcAIWorkspaceBox, win->aiWorkspaceBoxSubclassId, (DWORD_PTR)win);

    // show AI workspace based on saved preference (default true)
    win->aiWorkspaceVisible = gGlobalPrefs->showAiWorkspace;
    HwndSetVisibility(win->hwndAIWorkspaceBox, gGlobalPrefs->showAiWorkspace);
    if (win->aiWorkspaceSplitter) {
        HwndSetVisibility(win->aiWorkspaceSplitter->hwnd, gGlobalPrefs->showAiWorkspace);
    }

    UpdateAIWorkspaceOkBtn(win);
    UpdateAIWorkspaceNoteBtn(win);
}

void ToggleAIWorkspace(MainWindow* win) {
    if (!win->hwndAIWorkspaceBox) {
        CreateAIWorkspacePanel(win);
    }
    win->aiWorkspaceVisible = !win->aiWorkspaceVisible;
    gGlobalPrefs->showAiWorkspace = win->aiWorkspaceVisible;
    HwndSetVisibility(win->hwndAIWorkspaceBox, win->aiWorkspaceVisible);
    if (win->aiWorkspaceSplitter) {
        HwndSetVisibility(win->aiWorkspaceSplitter->hwnd, win->aiWorkspaceVisible);
    }
    if (win->aiWorkspaceVisible) {
        UpdateAIWorkspaceOkBtn(win);
        UpdateAIWorkspaceNoteBtn(win);
        if (win->aiWorkspaceInput && win->aiWorkspaceInput->hwnd) {
            HwndSetFocus(win->aiWorkspaceInput->hwnd);
        }
    }
    RelayoutWindow(win);
}

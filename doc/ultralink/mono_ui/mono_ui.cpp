// mono_ui.cpp —— MonoUI 核心实现
//
// 对应 3rdParty/mono_ui/mono_ui.cpp。
//
// 帧循环照固件 0x08015BC8：
//   清屏 → 分派渲染（LIST / ICON / CUSTOM）→ 覆盖层 → 提交整帧 → 统计 fps
//
// 列表行坐标照固件 0x080145A8 的原式：
//   y = (row * 16.0 + 1.0) * slide + 1.0
// 注意这里**没有标题栏** —— 128 / 16 = 8 行正好铺满整屏。
#include "mono_ui.h"
#include "mono_widget.h"
#include "resources.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// 静态成员
// ---------------------------------------------------------------------------
MonoUI*          MonoUI::s_inst_   = nullptr;
MonoUI::MillisFn MonoUI::s_millis_ = nullptr;
constexpr int    MonoUI::kMaxOverlays;

MonoUI::MonoUI(u8g2_t* g2, MonoInput* input) : g2_(g2), in_(input) {}

MonoUI& MonoUI::Instance() { return *s_inst_; }
void    MonoUI::SetInstance(MonoUI* ui)    { s_inst_ = ui; }
void    MonoUI::SetMillisSource(MillisFn fn) { s_millis_ = fn; }

void MonoUI::SetFonts(const uint8_t* listFont, const uint8_t* bigFont) {
    if (listFont) fontList_ = listFont;
    if (bigFont)  fontBig_  = bigFont;
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------
void MonoUI::Begin() {
    if (!fontList_) fontList_ = MonoRes::kFontSmall;
    if (!fontBig_)  fontBig_  = MonoRes::kFontBig;

    if (g2_) {
        u8g2_InitDisplay(g2_);
        u8g2_SetPowerSave(g2_, 0);
        u8g2_ClearBuffer(g2_);
    }

    MonoPage::ResetPages();
    InitLayout();                        // 应用层提供

    if (MonoPage::PageCount() > 0)
        applyPage(MonoPage::GetPage(0), 0);

    if (in_) in_->Begin();
}

void MonoUI::Frame() {
    if (!g2_) return;

    uptime_ = now_;

    u8g2_ClearBuffer(g2_);
    u8g2_SetDrawColor(g2_, theme_.fg);

    renderPage();

    if (overlayDepth_ > 0) drawOverlays();

    u8g2_SendBuffer(g2_);

    if (lastFrame_ != 0 && now_ > lastFrame_) {
        const uint32_t dt = now_ - lastFrame_;
        if (dt > 0) fps_ = (uint8_t)(1000u / dt);
    }
    lastFrame_ = now_;
    ++frameCount_;
}

void MonoUI::Tick(uint32_t now) {
    now_ = now;
    if (!in_) return;

    in_->Update(now);

    MonoInput::Event ev;
    MonoInput::Key   k;
    while (in_->Pop(ev, k)) {
        bool consumed = false;

        if (overlayDepth_ > 0) {
            MonoWidget* w = TopOverlay();
            if (w) consumed = w->OnKey(*this, k);
        }
        if (!consumed) handleEvent(ev, k);
    }

    // 覆盖层自身的时间逻辑（Toast 到期、进度条刷新）
    if (overlayDepth_ > 0) {
        MonoWidget* w = TopOverlay();
        if (w) w->Update(*this);
    }
}

void MonoUI::Run() {
    for (;;) {
        const uint32_t t = s_millis_ ? s_millis_() : (now_ + 1);
        Tick(t);
        Frame();
    }
}

// ---------------------------------------------------------------------------
// 导航
// ---------------------------------------------------------------------------
MonoPage* MonoUI::CurrentPage() { return MonoPage::GetPage(curPage_); }

MonoItem* MonoUI::CurrentItem() {
    MonoPage* p = CurrentPage();
    if (!p) return nullptr;
    return p->Item(TargetIndex());
}

int MonoUI::FocusIndex() const {
    // 截断而非四舍五入：与固件的 (int)(float)焦点 一致，
    // 色块在过渡的后半段才切到新行。
    const int i = (int)focusS_.pos;
    return i < 0 ? 0 : i;
}

int MonoUI::TargetIndex() const {
    const int i = (int)(focusS_.target + 0.5f);
    return i < 0 ? 0 : i;
}

void MonoUI::SetFocus(int index) {
    MonoPage* p = CurrentPage();
    if (!p) return;
    const int last = p->ItemCount() > 0 ? p->ItemCount() - 1 : 0;
    index = monoClampi(index, 0, last);
    focusS_.target = (float)index;
    p->savedFocus  = (float)index;
}

void MonoUI::MoveFocus(int delta) {
    MonoPage* p = CurrentPage();
    if (!p || p->ItemCount() == 0) return;
    SetFocus(TargetIndex() + delta);   // 以目标为基准，连按不丢步
}

void MonoUI::GotoPage(MonoPage* page, int item) {
    if (!page) return;
    MonoPage* cur = CurrentPage();
    if (cur && item < 0) cur->savedFocus = focusS_.target;
    applyPage(page, item);
}

void MonoUI::GotoPage(int index, int item) {
    GotoPage(MonoPage::GetPage(index), item);
}

void MonoUI::applyPage(MonoPage* p, int item) {
    if (!p) return;
    curPage_ = p->Index();

    const float f = (item >= 0) ? (float)item : p->savedFocus;
    focusS_.reset(f);
    scroll_.reset(0.0f);

    slide_     = 0.0f;      // 整页从顶部展开
    slidePage_ = curPage_;
}

void MonoUI::Back() {
    MonoPage* p = CurrentPage();
    if (!p) return;

    if (p->backPage >= 0) GotoPage(p->backPage, -1);
    else                  HomeScreen(nullptr);
}

void MonoUI::Press() {
    MonoItem* it = CurrentItem();
    if (it) it->Activate(this);
}

void MonoUI::AdjustCurrent(int dir) {
    MonoItem* it = CurrentItem();
    if (it) adjustItem(*it, dir);
}

void MonoUI::activateItem(MonoItem& it) { it.Activate(this); }

void MonoUI::switchItem(MonoItem& it) {
    if (it.type != ItemType::ITEM_SWITCH && it.type != ItemType::ITEM_CHECKBOX)
        return;
    it.Toggle();
    if (it.callback) it.callback(this, it.Context());
}

void MonoUI::adjustItem(MonoItem& it, int dir) {
    switch (it.type) {
    case ItemType::ITEM_VALUE:
        if (it.IsEditing()) it.Adjust(dir);
        break;

    case ItemType::ITEM_PROGRESS_BAR:
        it.SetValue(it.value + (float)(dir * 5));
        if (it.callback) it.callback(this, it.Context());
        break;

    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        it.SetOn(dir > 0);
        if (it.callback) it.callback(this, it.Context());
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 输入
// ---------------------------------------------------------------------------
void MonoUI::handleEvent(MonoInput::Event ev, MonoInput::Key k) {
    switch (ev) {
    case MonoInput::EV_CLICK:
        switch (k) {
        case MonoInput::KEY_UP:    MoveFocus(-1);     break;
        case MonoInput::KEY_DOWN:  MoveFocus(+1);     break;
        case MonoInput::KEY_LEFT:  AdjustCurrent(-1); break;
        case MonoInput::KEY_RIGHT: AdjustCurrent(+1); break;
        case MonoInput::KEY_OK:    Press();           break;
        case MonoInput::KEY_BACK:  Back();            break;
        default: break;
        }
        break;

    case MonoInput::EV_LONG:
        // 长按 OK = 返回（固件的 500ms 判定）
        if (k == MonoInput::KEY_OK) Back();
        break;

    case MonoInput::EV_REPEAT:
        switch (k) {
        case MonoInput::KEY_UP:    MoveFocus(-1);     break;
        case MonoInput::KEY_DOWN:  MoveFocus(+1);     break;
        case MonoInput::KEY_LEFT:  AdjustCurrent(-1); break;
        case MonoInput::KEY_RIGHT: AdjustCurrent(+1); break;
        default: break;
        }
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 覆盖层
// ---------------------------------------------------------------------------
void MonoUI::PushOverlay(MonoWidget* w) {
    if (!w || overlayDepth_ >= kMaxOverlays) return;
    overlay_[overlayDepth_++] = w;
}

void MonoUI::PopOverlay() {
    if (overlayDepth_ <= 0) return;
    MonoWidget* w = overlay_[--overlayDepth_];
    overlay_[overlayDepth_] = nullptr;
    MonoWidget::Free(w);
}

MonoWidget* MonoUI::TopOverlay() {
    if (overlayDepth_ <= 0) return nullptr;
    return overlay_[overlayDepth_ - 1];
}

// ---------------------------------------------------------------------------
// 渲染分派
// ---------------------------------------------------------------------------
void MonoUI::renderPage() {
    MonoPage* p = CurrentPage();
    if (!p) return;

    switch (p->GetMode()) {
    case MonoPage::ICON:   renderIconPage(*p); break;
    case MonoPage::CUSTOM: renderCustom(*p);   break;
    default:               renderList(*p);     break;
    }
}

void MonoUI::renderCustom(MonoPage& p) {
    if (p.drawCustom) p.drawCustom(*this, p);
}

// ---------------------------------------------------------------------------
// 列表页
// ---------------------------------------------------------------------------
void MonoUI::renderList(MonoPage& p) {
    const int rows  = MonoLayout::kScreenH / MonoLayout::kRowH;   // 8
    const int count = p.ItemCount();

    // ---- 滚动跟随：让焦点尽量停在中间 ----
    float st = focusS_.pos - (float)(rows - 1) * 0.5f;
    if (st < 0.0f) st = 0.0f;
    if (count > rows) {
        const float mx = (float)(count - rows);
        if (st > mx) st = mx;
    } else {
        st = 0.0f;
    }
    scroll_.target = st;

    focusS_.step();
    scroll_.step();

    // ---- 进页展开 ----
    if (slide_ < 1.0f) {
        slide_ += 0.15f;
        if (slide_ > 1.0f) slide_ = 1.0f;
    }

    const int first = (int)scroll_.pos;
    const int last  = first + rows;

    for (int i = first; i <= last && i < count; ++i) {
        MonoItem* it = p.Item(i);
        if (!it) continue;

        const float rowF = (float)i - scroll_.pos;
        if (rowF < -1.0f || rowF > (float)rows) continue;

        drawRow(*it, rowF, (i == FocusIndex()), slide_);
    }
}

// 单行绘制。slide 是整页展开进度。
void MonoUI::drawRow(MonoItem& it, float rowF, bool selected, float slide) {
    // 固件原式：(row * 16 + 1) * slide + 1
    const int y = (int)((rowF * (float)MonoLayout::kRowH + 1.0f) * slide + 1.0f);

    // ITEM_TEXT 缩进 3，其余 10（给行首图标列留位）
    const int x    = (it.type == ItemType::ITEM_TEXT)
                   ? MonoLayout::kPadText : MonoLayout::kPadItem;
    const int base = y + 11;   // 基线：6x12 字体 ascent 7 / descent -2

    u8g2_SetDrawColor(g2_, theme_.fg);

    switch (it.type) {
    case ItemType::ITEM_TEXT:
        DrawText(x, base, it.GetText());
        break;

    case ItemType::ITEM_SWITCH: {
        const char* state = it.IsOn() ? MonoRes::kTextOn : MonoRes::kTextOff;
        DrawTextClipped(x, base, it.GetText(),
                        MonoLayout::kWidgetRight - TextWidth(state) - 4);
        // ON/OFF 右对齐到 kWidgetRight，不能越过右侧选中色块
        DrawTextRight(MonoLayout::kWidgetRight, base, state);
        break;
    }

    case ItemType::ITEM_CHECKBOX: {
        // 勾选框放在 kPadItem 处，不再向左探 —— 左边 0..9 是选中色块的
        // 地盘，探出去会被色块盖掉。
        const int bs = 9;
        FrameBox(x, y + 4, bs, bs);
        if (it.IsOn()) FillBox(x + 2, y + 6, bs - 4, bs - 4);
        DrawText(x + bs + 3, base, it.GetText());
        break;
    }

    case ItemType::ITEM_VALUE: {
        snprintf(it.valueBuf, sizeof(it.valueBuf), MonoRes::kFormatValue, it.value);
        DrawTextClipped(x, base, it.GetText(),
                        MonoLayout::kWidgetRight - TextWidth(it.valueBuf) - 4);
        DrawTextRight(MonoLayout::kWidgetRight, base, it.valueBuf);
        if (it.IsEditing())
            FrameRound(x - 3, y, MonoLayout::kScreenW - 6, MonoLayout::kRowH, 3);
        break;
    }

    case ItemType::ITEM_PROGRESS_BAR: {
        const int bw = MonoLayout::kBarW;
        const int bh = MonoLayout::kBarH;
        const int bx = MonoLayout::kWidgetRight - bw;
        const int by = y + (MonoLayout::kRowH - bh) / 2;

        DrawTextClipped(x, base, it.GetText(), bx - 4);

        FrameRound(bx, by, bw, bh, 2);
        const int fw = (bw - 4) * (int)it.value / 100;
        if (fw > 0) FillRound(bx + 2, by + 2, fw, bh - 4, 1);
        break;
    }

    case ItemType::ITEM_TEXT_MARQUEE: {
        const int tw = TextWidth(it.GetText());
        int mx = 0;
        if (tw > MonoLayout::kMarqueeGate && selected)
            mx = it.marquee.step(tw, now_);
        else
            it.marquee.reset();

        DrawText(x + mx, base, it.GetText());

        if (it.statusIcon) {
            if (selected) {
                u8g2_SetDrawColor(g2_, theme_.bg);
                FillBox(MonoLayout::kStatusIconX - 1, y + 2,
                        MonoLayout::kStatusHiS, MonoLayout::kStatusHiS);
                u8g2_SetDrawColor(g2_, theme_.fg);
            }
            DrawRawBmp(it.statusIcon, MonoLayout::kStatusIconX, y + 3,
                       MonoLayout::kStatusIconS, MonoLayout::kStatusIconS);
        }
        break;
    }

    case ItemType::ITEM_CALL_PAGE:
    case ItemType::ITEM_CALL_FUNCTION:
        // 行首前缀："+" 可进入子页，"-" 纯动作
        DrawText(MonoLayout::kPrefixX, base,
                 (it.type == ItemType::ITEM_CALL_PAGE)
                     ? MonoRes::kPrefixPage
                     : MonoRes::kPrefixFunction);
        DrawText(x, base, it.GetText());
        break;
    }

    // 选中行：**先画文字，再盖左右两块实心色块**。
    // 顺序不能反 —— 反了会把文字一起盖掉，选中行就成了两个黑块夹一片空白。
    if (selected) {
        u8g2_SetDrawColor(g2_, theme_.accent);
        FillBox(0, y, MonoLayout::kMarkW, MonoLayout::kRowH);
        FillBox(MonoLayout::kScrollBarX, y,
                MonoLayout::kScrollBarW, MonoLayout::kRowH);
        u8g2_SetDrawColor(g2_, theme_.fg);
    }
}

// ---------------------------------------------------------------------------
// 图标页
// ---------------------------------------------------------------------------
void MonoUI::renderIconPage(MonoPage& p) {
    const int count   = p.ItemCount();
    const int centerX = MonoLayout::kScreenW / 2;

    scroll_.target = focusS_.pos;
    focusS_.step();
    scroll_.step();

    // 固件原式：格顶 y = kIconOriginY + i * kIconCell，但屏幕一次只显示
    // 两三格，所以要随焦点滚动。展开后等价于"焦点那格的顶边固定在
    // kIconOriginY + 2 格"= 43px，间距 56px —— 焦点略偏上，下方留白。
    const int focusTop = MonoLayout::kIconOriginY + 2 * MonoLayout::kIconCell;

    for (int i = 0; i < count; ++i) {
        const float rel = (float)i - scroll_.pos;
        const int   by  = focusTop + (int)(rel * (float)MonoLayout::kIconCell);
        if (by < -MonoLayout::kIconBox || by > MonoLayout::kScreenH) continue;

        const bool sel = (i == FocusIndex());
        const int  bx  = centerX - MonoLayout::kIconBox / 2;

        MonoItem* it = p.Item(i);
        const uint8_t* icon = it ? it->iconSrc : nullptr;

        if (sel) {
            // 选中：实心黑底 + 图标用背景色挖空（反白选中条）
            u8g2_SetDrawColor(g2_, theme_.fg);
            FillRound(bx, by, MonoLayout::kIconBox, MonoLayout::kIconBox, 8);

            if (icon) {
                u8g2_SetDrawColor(g2_, theme_.bg);
                DrawIcon(icon, bx + 3, by + 3, 3);
            }
        } else if (icon) {
            // 未选中：直接在白底上画线条图标。
            // 48px 底板 + 56px 间距只剩 8px 缝隙，放不下 12px 的字，
            // 所以不画文字标签（固件主界面同样是纯图标）。
            u8g2_SetDrawColor(g2_, theme_.fg);
            DrawIcon(icon, bx + 3, by + 3, 3);
        }

        u8g2_SetDrawColor(g2_, theme_.fg);
    }
}

void MonoUI::drawOverlays() {
    for (int i = 0; i < overlayDepth_; ++i)
        if (overlay_[i]) overlay_[i]->Draw(*this);
    u8g2_SetDrawColor(g2_, theme_.fg);
}

// ---------------------------------------------------------------------------
// 绘制工具
// ---------------------------------------------------------------------------
int MonoUI::TextWidth(const char* s) {
    if (!s || !g2_) return 0;
    if (fontList_) u8g2_SetFont(g2_, fontList_);
    return (int)u8g2_GetUTF8Width(g2_, s);
}

int MonoUI::FontAscent() {
    if (!fontList_) return 8;
    return (int)(int8_t)fontList_[13];   // u8g2_font_info_t.ascent_A
}

void MonoUI::DrawText(int x, int y, const char* s) {
    if (!s || !g2_) return;
    if (fontList_) u8g2_SetFont(g2_, fontList_);
    u8g2_DrawUTF8(g2_, (int16_t)x, (int16_t)y, s);
}

void MonoUI::DrawTextCenter(int cx, int y, const char* s) {
    DrawText(cx - TextWidth(s) / 2, y, s);
}

// 右对齐：字号是等宽 6px，但这里按实测宽度算，换字体也不会错位
void MonoUI::DrawTextRight(int rightX, int y, const char* s) {
    DrawText(rightX - TextWidth(s), y, s);
}

// 逐字画，超出行内右侧控件的可用宽度就停下。
// 128px 屏上标签和右侧控件经常抢位置，硬画必然叠字，截断比叠字好看。
void MonoUI::DrawTextClipped(int x, int y, const char* s, int maxX) {
    if (!s || !g2_) return;
    if (fontList_) u8g2_SetFont(g2_, fontList_);

    char one[2] = {0, 0};
    int  cx = x;

    for (const char* p = s; *p; ++p) {
        one[0] = *p;
        const int w = (int)u8g2_GetUTF8Width(g2_, one);
        if (cx + w > maxX) break;
        u8g2_DrawUTF8(g2_, (int16_t)cx, (int16_t)y, one);
        cx += w;
    }
}

void MonoUI::FillBox(int x, int y, int w, int h)  { u8g2_DrawBox(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h); }
void MonoUI::FillRound(int x, int y, int w, int h, int r) { u8g2_DrawRBox(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r); }
void MonoUI::FrameBox(int x, int y, int w, int h) { u8g2_DrawFrame(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h); }
void MonoUI::FrameRound(int x, int y, int w, int h, int r) { u8g2_DrawRFrame(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r); }

// 画 14x14 图标。
//
// 注意极性：固件这批图标是**负片** —— 位为 1 的地方是背景，位为 0 的地方
// 才是笔画（拿 kIcon0 印出来就能看到：外圈全是 1，中间那个"显示屏"的
// 轮廓是 0）。所以这里对 0 位落墨。
//
// 于是在白底上用前景色画 = 黑色线条图标；在黑底板上用背景色画 = 反白挖空。
void MonoUI::DrawIcon(const uint8_t* icon, int x, int y, int scale) {
    if (!icon) return;
    const int n = icon[0];
    if (n <= 0 || n > 32) return;

    const int stride = (n + 7) >> 3;

    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            const uint8_t b = icon[1 + r * stride + (c >> 3)];
            if ((b >> (7 - (c & 7))) & 1) continue;      // 1 = 背景，跳过

            if (scale <= 1)
                u8g2_DrawPixel(g2_, (int16_t)(x + c), (int16_t)(y + r));
            else
                u8g2_DrawBox(g2_, (int16_t)(x + c * scale), (int16_t)(y + r * scale),
                             (int16_t)scale, (int16_t)scale);
        }
    }
}

void MonoUI::DrawRawBmp(const uint8_t* bmp, int x, int y, int w, int h) {
    if (!bmp) return;
    const int stride = (w + 7) >> 3;

    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            const uint8_t b = bmp[r * stride + (c >> 3)];
            if ((b >> (7 - (c & 7))) & 1)
                u8g2_DrawPixel(g2_, (int16_t)(x + c), (int16_t)(y + r));
        }
}

// ---------------------------------------------------------------------------
// MonoItem 的实现（原项目没有 mono_item.cpp，实现寄生在这里）
// ---------------------------------------------------------------------------
MonoPage* MonoItem::TargetPage() const {
    return monoFromUptr<MonoPage*>(param1);
}

bool MonoItem::Activate(MonoUI* ui) {
    if (!ui) return false;

    switch (type) {
    case ItemType::ITEM_CALL_PAGE: {
        MonoPage* tp = TargetPage();
        if (tp) {
            const int t = (int)(intptr_t)param2;
            ui->GotoPage(tp, t > 0 ? t : -1);
        }
        return true;
    }

    case ItemType::ITEM_CALL_FUNCTION:
    case ItemType::ITEM_TEXT_MARQUEE:
        if (callback) callback(ui, Context());
        return true;

    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        Toggle();
        if (callback) callback(ui, Context());
        return true;

    case ItemType::ITEM_VALUE:
        SetEditing(!IsEditing());
        return true;

    case ItemType::ITEM_PROGRESS_BAR:
        return true;

    default:
        return false;
    }
}

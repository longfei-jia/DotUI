// mono_ui.cpp —— MonoUI 核心实现
//
// 帧循环：清屏 → 分派渲染（LIST / ICON / CUSTOM）→ 覆盖层 → 提交整帧。
// 本文件按节组织：
//   1. 生命周期        2. 导航与焦点     3. 输入分派
//   4. 覆盖层栈        5. 列表页渲染     6. 图标页渲染
//   7. 绘制工具
#include "mono_ui.h"
#include "mono_widget.h"
#include "resources.h"

#include <stdio.h>
#include <string.h>

// 运行期弹簧调试日志：1 = 开启，0 = 关闭。
// 开启时每帧向 stderr 打印 [spring] 行，用于排查选中框/滚动问题。
// 判据：moving=0 之后 dy 必须为 0（选中框与文本行严格对齐）。
#ifndef MONO_UI_DEBUG_SPRING
#define MONO_UI_DEBUG_SPRING 1
#endif

MonoUI*          MonoUI::s_inst_   = nullptr;
MonoUI::MillisFn MonoUI::s_millis_ = nullptr;
constexpr int    MonoUI::kMaxOverlays;

MonoUI::MonoUI(u8g2_t* g2, MonoInput* input) : g2_(g2), in_(input) {}

MonoUI& MonoUI::Instance() { return *s_inst_; }
void    MonoUI::SetInstance(MonoUI* ui) { s_inst_ = ui; }
void    MonoUI::SetMillisSource(MillisFn fn) { s_millis_ = fn; }

void MonoUI::SetFonts(const uint8_t* listFont, const uint8_t* bigFont) {
    if (listFont) fontList_ = listFont;
    if (bigFont)  fontBig_  = bigFont;
}

// ---------------------------------------------------------------------------
// 1. 生命周期
// ---------------------------------------------------------------------------
void MonoUI::Begin() {
    if (!fontList_) fontList_ = MonoRes::kFontSmall;
    if (!fontBig_)  fontBig_  = MonoRes::kFontBig;

    if (g2_) {
        u8g2_InitDisplay(g2_);
        u8g2_SetPowerSave(g2_, 0);
        u8g2_ClearBuffer(g2_);

        // 用实际显示尺寸重算布局（128x128 / 144x168 共用一套逻辑）
        MonoLayout::init((int)u8g2_GetDisplayWidth(g2_),
                         (int)u8g2_GetDisplayHeight(g2_));
    }

    MonoPage::ResetPages();
    InitLayout();

    if (MonoPage::PageCount() > 0)
        applyPage(MonoPage::GetPage(0), 0);

    if (in_) in_->Begin();
}

// 渲染一帧：清屏 → 页面 → 覆盖层 → 提交
void MonoUI::Frame() {
    if (!g2_) return;

    // 帧间隔 dt（秒）：限幅 1~50ms。
    // 下限防止除零/零步长，上限防止窗口挂起后的大步长破坏积分稳定性。
    if (lastFrame_ != 0 && now_ > lastFrame_) {
        float dtMs = (float)(now_ - lastFrame_);
        if (dtMs < 1.0f)  dtMs = 1.0f;
        if (dtMs > 50.0f) dtMs = 50.0f;
        dt_ = dtMs * 0.001f;
    }

    uptime_ = now_;

    u8g2_ClearBuffer(g2_);
    u8g2_SetDrawColor(g2_, theme_.fg);

    renderPage();

    if (overlayDepth_ > 0) drawOverlays();

    u8g2_SendBuffer(g2_);

    // 粗略 FPS：取相邻两帧间隔的倒数
    if (lastFrame_ != 0 && now_ > lastFrame_) {
        const uint32_t dt = now_ - lastFrame_;
        if (dt > 0) fps_ = (uint8_t)(1000u / dt);
    }
    lastFrame_ = now_;
    ++frameCount_;
}

// 消费输入：覆盖层有按键优先吃掉，其余交给页面导航
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

    // 顶层覆盖层的超时处理（Toast 自动消失）
    if (overlayDepth_ > 0) {
        MonoWidget* w = TopOverlay();
        if (w) w->Update(*this);
    }
}

// MCU 一体式主循环：PC 模拟器用自己的循环替代
void MonoUI::Run() {
    for (;;) {
        const uint32_t t = s_millis_ ? s_millis_() : (now_ + 1);
        Tick(t);
        Frame();
    }
}

// ---------------------------------------------------------------------------
// 2. 导航与焦点
// ---------------------------------------------------------------------------
MonoPage* MonoUI::CurrentPage() { return MonoPage::GetPage(curPage_); }

MonoItem* MonoUI::CurrentItem() {
    MonoPage* p = CurrentPage();
    if (!p) return nullptr;
    return p->Item(TargetIndex());
}

// 焦点弹簧当前位置（向下取整，渲染用）
int MonoUI::FocusIndex() const {
    const int i = (int)focusS_.pos;
    return i < 0 ? 0 : i;
}

// 焦点目标索引（四舍五入，逻辑判定/滚动目标用）
int MonoUI::TargetIndex() const {
    const int i = (int)(focusS_.target + 0.5f);
    return i < 0 ? 0 : i;
}

// 设置目标焦点：更新弹簧目标、保存页面焦点，并同步选中框目标
void MonoUI::SetFocus(int index) {
    MonoPage* p = CurrentPage();
    if (!p) return;
    const int last = p->ItemCount() > 0 ? p->ItemCount() - 1 : 0;
    index = monoClampi(index, 0, last);
    focusS_.target = (float)index;
    p->savedFocus  = (float)index;
    syncSelectionTarget();
}

void MonoUI::MoveFocus(int delta) {
    MonoPage* p = CurrentPage();
    if (!p || p->ItemCount() == 0) return;
    SetFocus(TargetIndex() + delta);
}

void MonoUI::GotoPage(MonoPage* page, int item) {
    if (!page) return;
    MonoPage* cur = CurrentPage();
    if (cur && item < 0) cur->savedFocus = focusS_.target;   // 记住离开位置
    applyPage(page, item);
}

void MonoUI::GotoPage(int index, int item) {
    GotoPage(MonoPage::GetPage(index), item);
}

// 切页：焦点/滚动/选中框全部就位，焦点在底部时滚动直接定位到包含它的页
void MonoUI::applyPage(MonoPage* p, int item) {
    if (!p) return;
    curPage_ = p->Index();

    const float f = (item >= 0) ? (float)item : p->savedFocus;
    focusS_.reset(f);

    // 滚动定位到能包含焦点的那一页，避免返回页面时从顶部重滚
    const int   rows      = MonoLayout::kScreenH / MonoLayout::kRowH;
    const int   count     = p->ItemCount();
    const float maxScroll = (count > rows) ? (float)(count - rows) : 0.0f;
    float st = 0.0f;
    if (f >= (float)rows) st = f - (float)(rows - 1);
    if (st < 0.0f) st = 0.0f;
    if (st > maxScroll) st = maxScroll;
    scroll_.reset(st);

    resetSelectionAnim();

    slide_ = 0.0f;   // 触发页面进入动画
}

// 把"目标项"的选中框 x/宽度写入 selX_/selW_ 的 target。
// 每帧在渲染前调用一次，语言切换、开关状态变化引起的宽度变化也能跟上。
void MonoUI::syncSelectionTarget() {
    MonoPage* p = CurrentPage();
    if (!p || p->ItemCount() == 0) {
        selX_.target = 0.0f;
        selW_.target = 0.0f;
        return;
    }

    int idx = TargetIndex();
    if (idx < 0) idx = 0;
    if (idx >= p->ItemCount()) idx = p->ItemCount() - 1;

    int x = 0, w = 0;
    selectionMetrics(*p->Item(idx), x, w);

    w += MonoLayout::kSelectionOverhang;   // 右侧外扩，形成包围感

    // 选中框不允许进入右侧导航栏
    const int rightLimit = MonoLayout::kNavX;
    if (x < 0) x = 0;
    if (x > rightLimit) x = rightLimit;
    if (x + w > rightLimit) w = rightLimit - x;
    if (w < 0) w = 0;

    selX_.target = (float)x;
    selW_.target = (float)w;
}

// 页面切换时选中框瞬移到目标位置（不做动画）
void MonoUI::resetSelectionAnim() {
    syncSelectionTarget();
    selX_.reset(selX_.target);
    selW_.reset(selW_.target);
}

// 返回父页；没有父页则回主页
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

// 开关/勾选：翻转并回调
void MonoUI::switchItem(MonoItem& it) {
    if (it.type != ItemType::ITEM_SWITCH && it.type != ItemType::ITEM_CHECKBOX)
        return;
    it.Toggle();
    if (it.callback) it.callback(this, it.Context());
}

// 左右键调整：数值项需处于编辑态；进度条步进 5；开关按方向置位
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
// 3. 输入分派
// ---------------------------------------------------------------------------
// 导航键映射：CLICK 与 REPEAT 共用；LEFT/RIGHT 在图标页切图标，其余页调值
void MonoUI::handleEvent(MonoInput::Event ev, MonoInput::Key k) {
    MonoPage* page     = CurrentPage();
    const bool iconPage = page && page->GetMode() == MonoPage::ICON;

    if (ev == MonoInput::EV_CLICK || ev == MonoInput::EV_REPEAT) {
        const bool click = (ev == MonoInput::EV_CLICK);

        switch (k) {
        case MonoInput::KEY_UP:    MoveFocus(-1); break;
        case MonoInput::KEY_DOWN:  MoveFocus(+1); break;
        case MonoInput::KEY_LEFT:
            if (iconPage) MoveFocus(-1); else AdjustCurrent(-1);
            break;
        case MonoInput::KEY_RIGHT:
            if (iconPage) MoveFocus(+1); else AdjustCurrent(+1);
            break;
        case MonoInput::KEY_OK:    if (click) Press(); break;
        case MonoInput::KEY_BACK:  if (click) Back();  break;
        default: break;
        }
        return;
    }

    // 长按 OK 等效返回
    if (ev == MonoInput::EV_LONG && k == MonoInput::KEY_OK) Back();
}

// ---------------------------------------------------------------------------
// 4. 覆盖层栈
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
// 5. 列表页渲染
// ---------------------------------------------------------------------------
// 渲染分派入口
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

void MonoUI::renderList(MonoPage& p) {
    const int rows  = MonoLayout::kScreenH / MonoLayout::kRowH;
    const int count = p.ItemCount();

    // 分页式滚动：按"目标索引"判断是否翻页。
    // 用目标而非当前值，保证底部继续下移时页面与选中框同帧起跳。
    const float maxScroll = (count > rows) ? (float)(count - rows) : 0.0f;
    float target = scroll_.target;
    const float f = (float)TargetIndex();
    if (f < target) {
        target = f;
    } else if (f >= target + (float)rows) {
        target = f - (float)(rows - 1);   // 焦点贴底：滚到正好显示它
    }
    if (target < 0.0f) target = 0.0f;
    if (target > maxScroll) target = maxScroll;
    scroll_.target = target;

    focusS_.step(dt_);
    scroll_.step(dt_);

    // 页面进入动画：slide_ 0→1（时间驱动，约 1/6 秒完成）
    if (slide_ < 1.0f) {
        slide_ += dt_ * 6.0f;
        if (slide_ > 1.0f) slide_ = 1.0f;
    }

    // 画可见行（多画一行作为滚动过渡）
    const int first = (int)scroll_.pos;
    const int last  = first + rows;
    for (int i = first; i <= last && i < count; ++i) {
        MonoItem* it = p.Item(i);
        if (!it) continue;

        const float rowF = (float)i - scroll_.pos;
        if (rowF < -1.0f || rowF > (float)rows) continue;

        drawRow(*it, rowF, (i == FocusIndex()), slide_);
    }

    // 选中框：x/y/宽度三个弹簧同步运动；用 XOR 反色覆盖整行文本
    const float focusRow = focusS_.pos - scroll_.pos;
    int barY = MonoLayout::kListTop +
               (int)((focusRow * (float)MonoLayout::kRowH) * slide_);

    if (count > 0) {
        syncSelectionTarget();
        selX_.step(dt_);
        selW_.step(dt_);

        // 到位兜底：所有弹簧收敛后，选中框强制对齐整数行。
        // 浮点积分收敛时仍可能残留亚像素误差（如 8.0001），
        // 而文本行 y 用整数索引计算，二者相减会造成 1px 的视觉偏移。
        if (slide_ >= 1.0f && focusS_.settled() && scroll_.settled()) {
            barY = MonoLayout::kListTop +
                   (FocusIndex() - (int)(scroll_.pos + 0.5f)) * MonoLayout::kRowH;
        }

        int sx = (int)(selX_.pos + 0.5f);
        int sw = (int)(selW_.pos + 0.5f);
        const int rightLimit = MonoLayout::kNavX;

        if (sx < 0) sx = 0;
        if (sx > rightLimit) sx = rightLimit;
        if (sw < 1) sw = 1;
        if (sx + sw > rightLimit) sw = rightLimit - sx;

        u8g2_SetDrawColor(g2_, 2);   // XOR：底、字一起反色
        // u8g2_DrawRBox(g2_, (int16_t)sx, (int16_t)barY,
        //               (int16_t)sw, (int16_t)MonoLayout::kRowH,
        //               (u8g2_uint_t)MonoLayout::kSelectionRadius);
        FillRoundOnce(sx, barY, sw, MonoLayout::kRowH, MonoLayout::kSelectionRadius);
        u8g2_SetDrawColor(g2_, theme_.fg);
    }

    // 右侧导航栏：铺底色盖住列表溢出，再画滚动条
    u8g2_SetDrawColor(g2_, theme_.bg);
    FillBox(MonoLayout::kNavX, 0, MonoLayout::kNavW, MonoLayout::kScreenH);

    if (count > rows) drawScrollbar(count, rows, maxScroll);

#if MONO_UI_DEBUG_SPRING
    {
        const bool moving = !focusS_.settled() || !scroll_.settled() ||
                            !selX_.settled() || !selW_.settled();
        // 运动中每帧打印；静止时每 30 帧打印一次心跳
        if (moving || (frameCount_ % 30u) == 0u) {
            int ci = FocusIndex();
            int ti = TargetIndex();
            if (ci >= count) ci = count > 0 ? count - 1 : 0;
            if (ti >= count) ti = count > 0 ? count - 1 : 0;

            const char* ct = (count > 0 && p.Item(ci)) ? p.Item(ci)->GetText() : "";
            const char* tt = (count > 0 && p.Item(ti)) ? p.Item(ti)->GetText() : "";

            // 焦点行文本 y：与 drawRow 完全相同的算法。
            // dy = barY - rowY：选中框相对文本行的垂直偏差。
            // 判据：moving=0 之后 dy 必须回 0，否则说明选中框没对齐文本行。
            const float rowF  = (float)ci - scroll_.pos;
            const int   rowY  = MonoLayout::kListTop +
                (int)((rowF * (float)MonoLayout::kRowH) * slide_);

            fprintf(stderr,
                    "[spring] page=%d cur=%d/%s tgt=%d/%s "
                    "fT=%.3f fP=%.3f fV=%.2f sT=%.3f sP=%.3f rel=%.3f "
                    "barY=%d rowY=%d dy=%d "
                    "selX=%.2f/%.2f selW=%.2f/%.2f dt=%.1fms moving=%d\n",
                    curPage_, ci, ct ? ct : "", ti, tt ? tt : "",
                    focusS_.target, focusS_.pos, focusS_.vel,
                    scroll_.target, scroll_.pos, focusS_.pos - scroll_.pos,
                    barY, rowY, barY - rowY,
                    selX_.pos, selX_.target, selW_.pos, selW_.target,
                    dt_ * 1000.0f, moving ? 1 : 0);
        }
    }
#endif
}

// 滚动条：轨道 + 按比例缩放的滑块，位置跟随滚动弹簧
void MonoUI::drawScrollbar(int count, int rows, float maxScroll) {
    const int trackX = MonoLayout::kScrollBarX;
    const int trackW = MonoLayout::kScrollBarW;
    const int trackY = MonoLayout::kListTop + 1;
    const int trackH = rows * MonoLayout::kRowH - 2;

    // 滑块高度与"可视行数 / 总行数"成正比，并限制最小可点面积
    int thumbH = (trackH * rows) / count;
    if (thumbH < 8) thumbH = 8;
    if (thumbH > trackH) thumbH = trackH;

    float pos = scroll_.pos;
    if (pos < 0.0f) pos = 0.0f;
    if (pos > maxScroll) pos = maxScroll;

    const int thumbY = trackY +
        (int)((float)(trackH - thumbH) * (pos / maxScroll));

    u8g2_SetDrawColor(g2_, theme_.fg);
    u8g2_DrawFrame(g2_, (int16_t)trackX, (int16_t)trackY,
                   (int16_t)trackW, (int16_t)trackH);
    u8g2_DrawBox(g2_, (int16_t)(trackX + 1), (int16_t)thumbY,
                 (int16_t)(trackW - 2), (int16_t)thumbH);
}

// 画一行：统一前缀 + 按类型绘制内容。
// rowF 为浮点行号（相对可视区首行），slide 为页面进入动画进度。
void MonoUI::drawRow(MonoItem& it, float rowF, bool selected, float slide) {
    const int y    = MonoLayout::kListTop +
                     (int)((rowF * (float)MonoLayout::kRowH) * slide);
    const int x    = MonoLayout::kPadItem;
    const int base = RowBaseline(y);   // 按字体度量垂直居中，不写死偏移

    u8g2_SetDrawColor(g2_, theme_.fg);

    // 所有类型统一左对齐：有子页前缀 "+"，其余 "-"
    const char* prefix = (it.type == ItemType::ITEM_CALL_PAGE)
                             ? MonoRes::kPrefixPage
                             : MonoRes::kPrefixFunction;
    DrawText(MonoLayout::kPrefixX, base, prefix);

    switch (it.type) {
    case ItemType::ITEM_TEXT:
        DrawTextClipped(x, base, it.GetText(), MonoLayout::kNavX);
        break;

    // 开关：左侧标签截断避让，右侧右对齐 开/关
    case ItemType::ITEM_SWITCH: {
        const char* state = it.IsOn() ? MonoRes::kTextOn : MonoRes::kTextOff;
        DrawTextClipped(x, base, it.GetText(),
                        MonoLayout::kWidgetRight - TextWidth(state) - 4);
        DrawTextRight(MonoLayout::kWidgetRight, base, state);
        break;
    }

    // 勾选框：9px 方块 + 3px 间距 + 文本
    case ItemType::ITEM_CHECKBOX: {
        const int bs = 9;
        FrameBox(x, y + 4, bs, bs);
        if (it.IsOn()) FillBox(x + 2, y + 6, bs - 4, bs - 4);
        DrawTextClipped(x + bs + 3, base, it.GetText(), MonoLayout::kNavX);
        break;
    }

    // 数值：右侧右对齐显示，编辑态加圆角框
    case ItemType::ITEM_VALUE: {
        snprintf(it.valueBuf, sizeof(it.valueBuf), MonoRes::kFormatValue, it.value);
        DrawTextClipped(x, base, it.GetText(),
                        MonoLayout::kWidgetRight - TextWidth(it.valueBuf) - 4);
        DrawTextRight(MonoLayout::kWidgetRight, base, it.valueBuf);
        if (it.IsEditing())
            FrameRound(x - 3, y, MonoLayout::kNavX - (x - 3),
                       MonoLayout::kRowH, 3);
        break;
    }

    // 进度条：右侧固定宽度圆角条
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

    // 跑马灯：仅"选中且超宽"时滚动；可选行尾状态图标
    case ItemType::ITEM_TEXT_MARQUEE: {
        const int tw = TextWidth(it.GetText());
        int mx = 0;
        if (tw > MonoLayout::kMarqueeGate && selected)
            mx = it.marquee.step(tw, now_);
        else
            it.marquee.reset();

        DrawTextClipped(x + mx, base, it.GetText(), MonoLayout::kNavX);

        if (it.statusIcon) {
            DrawRawBmp(it.statusIcon, MonoLayout::kStatusIconX, y + 3,
                       MonoLayout::kStatusIconS, MonoLayout::kStatusIconS);
        }
        break;
    }

    // 页面/动作项：纯文本
    case ItemType::ITEM_CALL_PAGE:
    case ItemType::ITEM_CALL_FUNCTION:
        DrawTextClipped(x, base, it.GetText(), MonoLayout::kNavX);
        break;
    }
}

// 选中框几何：x 固定为前缀起点，宽度 = 前缀 + 间距 + 文本内容。
// 开关/数值/进度条只选中标签部分，右侧控件不参与。
void MonoUI::selectionMetrics(MonoItem& it, int& x, int& w) {
    x = MonoLayout::kPrefixX;
    const int lead = MonoLayout::kPadItem - MonoLayout::kPrefixX;   // 前缀+间距

    // 标签宽度，但不得与右侧宽度为 reserve 的控件重叠
    auto labelClamped = [&](int reserve) -> int {
        const int maxX = MonoLayout::kWidgetRight - reserve - 4;
        const int tw  = TextWidth(it.GetText());
        return (MonoLayout::kPadItem + tw > maxX)
                   ? (maxX - MonoLayout::kPadItem)
                   : tw;
    };

    int content = 0;
    switch (it.type) {
    case ItemType::ITEM_TEXT:
    case ItemType::ITEM_CALL_PAGE:
    case ItemType::ITEM_CALL_FUNCTION:
    case ItemType::ITEM_TEXT_MARQUEE:
        content = TextWidth(it.GetText());
        break;

    case ItemType::ITEM_CHECKBOX:
        content = 9 + 3 + TextWidth(it.GetText());
        break;

    case ItemType::ITEM_SWITCH:
        content = labelClamped(TextWidth(it.IsOn() ? MonoRes::kTextOn
                                                   : MonoRes::kTextOff));
        break;

    case ItemType::ITEM_VALUE: {
        char buf[16];
        snprintf(buf, sizeof(buf), MonoRes::kFormatValue, it.value);
        content = labelClamped(TextWidth(buf));
        break;
    }

    case ItemType::ITEM_PROGRESS_BAR:
        content = labelClamped(MonoLayout::kBarW);
        break;

    default:
        break;
    }

    if (content < 0) content = 0;
    w = lead + content;

    const int rightLimit = MonoLayout::kNavX;
    if (w < 0) w = 0;
    if (x + w > rightLimit) w = rightLimit - x;
    if (w < 0) w = 0;
}

// ---------------------------------------------------------------------------
// 6. 图标页渲染
// ---------------------------------------------------------------------------
// 图标副标题：按语言取中/英文
static const char* iconSubtitle(int index) {
    static const char* const kSubsCn[] = {
        "『芯片信息』", "『网络状态』", "『接口检测』", "『返回上一级』",
        "『路径设置』", "『阶梯波形』", "『噪声测试』", "『四向导航』"
    };
    static const char* const kSubsEn[] = {
        "[Chip Info]", "[Network]", "[Connector]", "[Back]",
        "[Path]", "[Waveform]", "[Noise]", "[Navigation]"
    };
    constexpr int n = (int)(sizeof(kSubsCn) / sizeof(kSubsCn[0]));
    if (index < 0 || index >= n) return nullptr;
    return MonoRes::kLanguage ? kSubsCn[index] : kSubsEn[index];
}

// 横向轮播：焦点图标固定在屏幕中心，其余图标按弹簧水平滑动。
// 不画选择框，焦点图标下方显示名称 + 副标题。
void MonoUI::renderIconPage(MonoPage& p) {
    const int count   = p.ItemCount();
    const int centerX = MonoLayout::kScreenW / 2;

    // 图标页的水平"滚动量"直接等于焦点位置
    scroll_.target = focusS_.pos;
    focusS_.step(dt_);
    scroll_.step(dt_);

    const int iconSize = MonoLayout::kIconLogical * 3;   // 放大 3 倍 = 42px
    const int iconY    = MonoLayout::kIconTop;
    const int cell     = MonoLayout::kIconCell;

    // 逐个画图标：相对位置决定 x，超出屏幕的跳过
    for (int i = 0; i < count; ++i) {
        const float rel = (float)i - scroll_.pos;
        const int x = centerX - iconSize / 2 + (int)(rel * (float)cell);
        if (x < -iconSize || x > MonoLayout::kScreenW) continue;

        MonoItem* it = p.Item(i);
        if (it && it->iconSrc) {
            u8g2_SetDrawColor(g2_, theme_.fg);
            DrawIcon(it->iconSrc, x, iconY, 3);
        }
    }

    // 焦点图标的名称（大字）与副标题（中文字体），居中显示
    MonoItem* cur = p.Item(FocusIndex());
    if (cur) {
        u8g2_SetDrawColor(g2_, theme_.fg);

        if (fontBig_) {
            u8g2_SetFont(g2_, fontBig_);
            const int tw = (int)u8g2_GetUTF8Width(g2_, cur->GetText());
            u8g2_DrawUTF8(g2_, (int16_t)(centerX - tw / 2),
                          (int16_t)(MonoLayout::kIconTop + 60), cur->GetText());
        }

        const char* sub = iconSubtitle(FocusIndex());
        if (sub && MonoRes::kFontChinese) {
            u8g2_SetFont(g2_, MonoRes::kFontChinese);
            const int sw = (int)u8g2_GetUTF8Width(g2_, sub);
            u8g2_DrawUTF8(g2_, (int16_t)(centerX - sw / 2),
                          (int16_t)(MonoLayout::kIconTop + 82), sub);
        }

        if (fontList_) u8g2_SetFont(g2_, fontList_);
    }

    u8g2_SetDrawColor(g2_, theme_.fg);
}

void MonoUI::drawOverlays() {
    for (int i = 0; i < overlayDepth_; ++i)
        if (overlay_[i]) overlay_[i]->Draw(*this);
    u8g2_SetDrawColor(g2_, theme_.fg);
}

// ---------------------------------------------------------------------------
// 7. 绘制工具
// ---------------------------------------------------------------------------
// 文本宽度：临时切到列表字体测量（u8g2 的宽度查询依赖当前字体）
int MonoUI::TextWidth(const char* s) {
    if (!s || !g2_) return 0;
    if (fontList_) u8g2_SetFont(g2_, fontList_);
    return (int)u8g2_GetUTF8Width(g2_, s);
}

// 字体上延（从 u8g2 字体头第 13 字节读取）
int MonoUI::FontAscent() {
    if (!fontList_) return 8;
    return (int)(int8_t)fontList_[13];
}

void MonoUI::DrawText(int x, int y, const char* s) {
    if (!s || !g2_) return;
    if (fontList_) u8g2_SetFont(g2_, fontList_);
    u8g2_DrawUTF8(g2_, (int16_t)x, (int16_t)y, s);
}

void MonoUI::DrawTextCenter(int cx, int y, const char* s) {
    DrawText(cx - TextWidth(s) / 2, y, s);
}

void MonoUI::DrawTextRight(int rightX, int y, const char* s) {
    DrawText(rightX - TextWidth(s), y, s);
}

// 逐 UTF-8 码点绘制并在 maxX 处截断。
// 不能按字节截：中文一个码点 3 字节，按字节切会产生乱码。
void MonoUI::DrawTextClipped(int x, int y, const char* s, int maxX) {
    if (!s || !g2_) return;
    if (fontList_) u8g2_SetFont(g2_, fontList_);

    char one[5] = {0, 0, 0, 0, 0};
    int  cx = x;
    const char* p = s;

    while (*p) {
        // 由首字节判断该码点长度（UTF-8）
        const unsigned char c = (unsigned char)*p;
        int len = 1;
        if (c >= 0xF0)      len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;

        for (int i = 0; i < len && p[i]; ++i) one[i] = p[i];
        one[len] = '\0';

        const int w = (int)u8g2_GetUTF8Width(g2_, one);
        if (cx + w > maxX) break;   // 放不下就停，保证不越界

        u8g2_DrawUTF8(g2_, (int16_t)cx, (int16_t)y, one);
        cx += w;
        p += len;
    }
}

void MonoUI::FillBox(int x, int y, int w, int h) {
    u8g2_DrawBox(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h);
}
// u8g2_DrawRBox 的两个坑：
//   1. 内部用无符号 u8g2_uint_t 计算 w-2r / h-2r，宽或高不足 2r+1 时
//      下溢成 65535，矩形画穿屏幕右缘；
//   2. r=1 的 1/4 圆盘是 3 像素 L 形（缺对角像素），拼出的"圆角"在
//      两端各留 1px 毛刺突起（进度条小填充上非常明显）。
// 因此 r<=1 或尺寸退化时一律降级为普通矩形。
void MonoUI::FillRound(int x, int y, int w, int h, int r) {
    if (r <= 1 || w <= 2 * r || h <= 2 * r) {
        FillBox(x, y, w, h);
        return;
    }
    u8g2_DrawRBox(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r);
}
void MonoUI::FrameBox(int x, int y, int w, int h) {
    u8g2_DrawFrame(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h);
}
// u8g2_DrawRFrame 与 DrawRBox 同样存在 w-2r / h-2r 无符号下溢，
// 尺寸退化时降级为普通空心矩形。
void MonoUI::FrameRound(int x, int y, int w, int h, int r) {
    if (r > 0 && (w <= 2 * r || h <= 2 * r)) {
        FrameBox(x, y, w, h);
        return;
    }
    u8g2_DrawRFrame(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r);
}
// 逐行填充圆角矩形：每个像素只绘制一次。
//
// u8g2_DrawRBox 内部是「4 个 1/4 圆盘 + 3 个矩形」拼接，图元彼此重叠；
// 在 XOR（SetDrawColor(2)）反色模式下，重叠像素会被反色两次而恢复原色，
// 表现为四角出现空洞/锯齿凹陷；同时 h-2r-2 的中间条在 r=3、h=16 时会
// 漏掉一行。这里改为按行合并成单段 HLine，两个问题一起解决。
void MonoUI::FillRoundOnce(int x, int y, int w, int h, int r) {
    if (!g2_ || w <= 0 || h <= 0) return;

    // 半径不能超过宽高的一半
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;

    for (int j = 0; j < h; ++j) {
        int inset = 0;
        if (j < r) {
            inset = r - 1 - j;                 // 上部圆角
        } else if (j >= h - r) {
            inset = r - 1 - (h - 1 - j);       // 下部圆角（与上部对称）
        }
        if (inset < 0) inset = 0;

        const int sx = x + inset;
        const int sw = w - inset * 2;
        if (sw <= 0) continue;

        u8g2_DrawHLine(g2_, (u8g2_uint_t)sx,
                       (u8g2_uint_t)(y + j), (u8g2_uint_t)sw);
    }
}

// 固件格式图标：icon[0]=边长 n，位图 MSB-first，0 位着色（1 = 透明）
void MonoUI::DrawIcon(const uint8_t* icon, int x, int y, int scale) {
    if (!icon) return;
    const int n = icon[0];
    if (n <= 0 || n > 32) return;

    const int stride = (n + 7) >> 3;

    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            const uint8_t b = icon[1 + r * stride + (c >> 3)];
            if ((b >> (7 - (c & 7))) & 1) continue;

            if (scale <= 1)
                u8g2_DrawPixel(g2_, (int16_t)(x + c), (int16_t)(y + r));
            else
                u8g2_DrawBox(g2_, (int16_t)(x + c * scale), (int16_t)(y + r * scale),
                             (int16_t)scale, (int16_t)scale);
        }
    }
}

// 裸位图：无长度头，1 位着色（与 DrawIcon 相反）
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

// 行内文本的垂直居中基线。
//
// 不能用固定偏移：字体切换（6x12 英文 / wqy12 中文）时 ascent/descent 会变，
// 固定值必然让其中一种字体偏上或偏下。
//   6x12 : ascent 7, descent -2 → 字高 9  → 基线 y+10
//   wqy12: ascent 8, descent -2 → 字高 10 → 基线 y+11
// 另外汉字墨迹比 ascent_A 更高（约 11~12px），中文再补 1px 才视觉居中。
int MonoUI::RowBaseline(int rowTop) {
    const uint8_t* f = fontList_ ? fontList_ : MonoRes::kFontSmall;

    const int ascent  = (int)(int8_t)f[13];   // 上升高度（正数）
    const int descent = (int)(int8_t)f[14];   // 下降深度（负数）
    const int glyphH  = ascent - descent;     // 参考字高

    int top = (MonoLayout::kRowH - glyphH) / 2;   // 垂直居中的上留白
    if (top < 0) top = 0;

    int base = rowTop + top + ascent;

    // 中文点阵字的墨迹比 ascent_A 高一截，光学补偿 1px
    if (MonoRes::kLanguage) base += 1;

    return base;
}

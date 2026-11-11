// dot_ui.cpp —— DotUI 核心实现
#include "dot_ui.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>
#include "dot_widget.h"

DotUI *DotUI::s_inst_ = nullptr;
DotUI::MillisFn DotUI::s_millis_ = nullptr;
constexpr int DotUI::kMaxOverlays;

DotUI::DotUI(u8g2_t *g2, DotInput *input) : g2_(g2), in_(input) {}

DotUI &DotUI::Instance() { return *s_inst_; }
void DotUI::SetInstance(DotUI *ui) { s_inst_ = ui; }
void DotUI::SetMillisSource(MillisFn fn) { s_millis_ = fn; }

void DotUI::SetFonts(const uint8_t* listFont, const uint8_t* bigFont)
{
    if (listFont)
        fontList_ = listFont;
    if (bigFont)
        fontBig_ = bigFont;
}

// 运行时切换屏幕方向（u8g2 原生能力，旋转裁剪回调即可）。
// 90° 旋转（r1/r3）会交换显示宽高，必须重算布局参数，
// 否则导航栏/滚动条仍按旧宽度贴边、列表行数也错。
void DotUI::RotateTo(const u8g2_cb_t *cb)
{
    if (!g2_ || !cb)
        return;
    u8g2_SetDisplayRotation(g2_, cb);
    DotLayout::init((int)u8g2_GetDisplayWidth(g2_),
                    (int)u8g2_GetDisplayHeight(g2_));
}

// 全局阻尼：设置页切换欠阻尼/临界/过阻尼时调用，
// 四个弹簧（焦点/滚动/选中框x/选中框宽）同步生效
void DotUI::SetSpringZeta(float zeta)
{
    focusS_.zeta = zeta;
    scroll_.zeta = zeta;
    selX_.zeta = zeta;
    selW_.zeta = zeta;
}

// ============================ 1. 生命周期 ============================
void DotUI::Begin()
{
    if (!fontList_)
        fontList_ = DotRes::kFontSmall;
    if (!fontBig_)
        fontBig_ = DotRes::kFontBig;

    if (g2_)
    {
        u8g2_InitDisplay(g2_);
        u8g2_SetPowerSave(g2_, 0);
        u8g2_ClearBuffer(g2_);
        // 用实际显示尺寸重算布局（128x128 / 144x168 共用一套逻辑）
        DotLayout::init((int)u8g2_GetDisplayWidth(g2_),
                        (int)u8g2_GetDisplayHeight(g2_));
    }

    DotPage::ResetPages();
    InitLayout(); // 应用层注册页面

    if (DotPage::PageCount() > 0)
        applyPage(DotPage::GetPage(0), 0);

    if (in_)
        in_->Begin();
}

void DotUI::Frame()
{
    if (!g2_)
        return;

    // 帧间隔 dt（秒）：限幅 1~50ms，防除零 & 防挂起后大步长破坏积分
    if (lastFrame_ != 0 && now_ > lastFrame_)
    {
        float dtMs = (float)(now_ - lastFrame_);
        if (dtMs < 1.0f)
            dtMs = 1.0f;
        if (dtMs > 50.0f)
            dtMs = 50.0f;
        dt_ = dtMs * 0.001f;
    }
    uptime_ = now_;

    u8g2_ClearBuffer(g2_);
    u8g2_SetDrawColor(g2_, theme_.fg);
    renderPage();

    // 覆盖层叠画：renderPage() 之后、SendBuffer 之前（最上层最后画）
    if (overlayDepth_ > 0)
        drawOverlays();

    u8g2_SendBuffer(g2_);

    if (lastFrame_ != 0 && now_ > lastFrame_)
    {
        const uint32_t dt = now_ - lastFrame_;
        if (dt > 0)
            fps_ = (uint8_t)(1000u / dt);
    }
    lastFrame_ = now_;
    ++frameCount_;
}

void DotUI::Tick(uint32_t now)
{
    now_ = now;
    if (!in_)
        return;

    in_->Update(now); // 长按/连发判定

    DotInput::Event ev;
    DotInput::Key k;
    // 事件先给栈顶覆盖层，没被吃掉才交给页面导航
    while (in_->Pop(ev, k)) {
        bool consumed = false;
        if (overlayDepth_ > 0) {
            DotWidget* w = TopOverlay();
            if (w) consumed = w->OnKey(*this, k);
        }
        if (!consumed) handleEvent(ev, k);
    }

    // 循环后：顶层 Toast 超时检查（到期自动弹出）
    if (overlayDepth_ > 0) {
        DotWidget* w = TopOverlay();
        if (w) w->Update(*this);
    }
}

void DotUI::Run()
{ // MCU 一体式主循环
    for (;;)
    {
        const uint32_t t = s_millis_ ? s_millis_() : (now_ + 1);
        Tick(t);
        Frame();
    }
}

// ============================ 2. 导航与焦点 ============================
DotPage *DotUI::CurrentPage() { return DotPage::GetPage(curPage_); }

DotItem *DotUI::CurrentItem()
{
    DotPage *p = CurrentPage();
    return p ? p->Item(TargetIndex()) : nullptr;
}

int DotUI::FocusIndex() const
{ // 渲染用：向下取整
    const int i = (int)focusS_.pos;
    return i < 0 ? 0 : i;
}
int DotUI::TargetIndex() const
{ // 逻辑用：四舍五入
    const int i = (int)(focusS_.target + 0.5f);
    return i < 0 ? 0 : i;
}

// STEP 7 版：瞬移（reset）。STEP 8 会改成 setTarget + 每帧 step
void DotUI::SetFocus(int index)
{
    DotPage *p = CurrentPage();
    if (!p)
        return;
    const int last = p->ItemCount() > 0 ? p->ItemCount() - 1 : 0;
    index = dotClampi(index, 0, last);
    focusS_.setTarget((float)index);
    p->savedFocus = (float)index;
    syncSelectionTarget();
}

void DotUI::MoveFocus(int delta)
{
    DotPage *p = CurrentPage();
    if (!p || p->ItemCount() == 0)
        return;
    SetFocus(TargetIndex() + delta);
}

void DotUI::GotoPage(DotPage *page, int item)
{
    if (!page)
        return;
    DotPage *cur = CurrentPage();
    if (cur && item < 0)
        cur->savedFocus = focusS_.target; // 记住离开位置
    applyPage(page, item);
}

void DotUI::GotoPage(int index, int item)
{
    GotoPage(DotPage::GetPage(index), item);
}

// 切页：焦点/滚动全部就位；焦点在底部时滚动直接定位到包含它的页
void DotUI::applyPage(DotPage *p, int item)
{
    if (!p)
        return;
    if (editing_)
        ExitEdit(false);   // 导航离开时统一取消编辑（跳页/返回/回主页都经此）
    curPage_ = p->Index();

    const float f = (item >= 0) ? (float)item : p->savedFocus;
    focusS_.reset(f);

    const int rows = DotLayout::kScreenH / DotLayout::kRowH;
    const int count = p->ItemCount();
    const float maxScroll = (count > rows) ? (float)(count - rows) : 0.0f;
    float st = 0.0f;
    if (f >= (float)rows)
        st = f - (float)(rows - 1);
    if (st < 0.0f)
        st = 0.0f;
    if (st > maxScroll)
        st = maxScroll;
    scroll_.reset(st);
    resetSelectionAnim();
    slide_ = 0.0f; // 页面进入动画（STEP 8 生效，现在无害）
}

void DotUI::Back()
{
    DotPage *p = CurrentPage();
    if (!p)
        return;
    if (p->backPage >= 0)
        GotoPage(p->backPage, -1);
    else
        HomeScreen(nullptr);
}

void DotUI::Press()
{
    DotItem *it = CurrentItem();
    if (!it)
        return;
    // 可编辑类型：OK 进入编辑态（上下键调值，再按 OK 提交/双击取消）
    if (it->Editable())
        EnterEdit();
    else
        it->Activate(this);
}

// ============================ 编辑态（方案 A：双态分派） ============================
// 进入编辑：快照当前值/开关态（取消时回滚），编辑位同步给渲染层做反白提示
bool DotUI::EnterEdit()
{
    DotItem *it = CurrentItem();
    if (!it || !it->Editable() || editing_)
        return false;
    editing_ = true;
    editSnapValue_ = it->value;
    editSnapOn_ = it->IsOn();
    it->SetEditing(true);
    return true;
}

// 退出编辑：commit=false 回滚快照并触发回调（应用层同步回显示）；
// commit=true 保持调整后的值（调整过程中回调已实时触发过）
void DotUI::ExitEdit(bool commit)
{
    DotItem *it = CurrentItem();
    editing_ = false;
    lastOkClick_ = 0;
    if (!it)
        return;
    it->SetEditing(false);
    if (!commit) {
        it->SetValue(editSnapValue_);
        it->SetOn(editSnapOn_);
        if (it->callback)
            it->callback(this, it->Context());
    }
}

// 右侧控件区文本绘制：编辑态选中行反白（fg 底 + bg 字），提示"正在调节"
void DotUI::DrawValueRight(const char *s, int y, int base, bool inverted)
{
    if (!inverted) {
        DrawTextRight(DotLayout::kWidgetRight, base, s);
        return;
    }
    const int w = TextWidth(s);
    FillRound(DotLayout::kWidgetRight - w - 3, y + 2, w + 6,
              DotLayout::kRowH - 4, 2);
    u8g2_SetDrawColor(g2_, theme_.bg);
    DrawTextRight(DotLayout::kWidgetRight, base, s);
    u8g2_SetDrawColor(g2_, theme_.fg);
}

void DotUI::AdjustCurrent(int dir)
{
    DotItem *it = CurrentItem();
    if (it)
        adjustItem(*it, dir);
}

// 左右键调整：数值项需处于编辑态（调值后触发回调，供应用层吸附/刷新）；
// 进度条步进 5；开关按方向置位
void DotUI::adjustItem(DotItem &it, int dir)
{
    switch (it.type)
    {
    case ItemType::ITEM_VALUE:
        if (it.IsEditing()) {
            it.Adjust(dir);
            if (it.callback)          // 框架不假设值语义，由应用决定如何响应
                it.callback(this, it.Context());
        }
        break;
    case ItemType::ITEM_PROGRESS_BAR:
        it.SetValue(it.value + (float)(dir * 5));
        if (it.callback)
            it.callback(this, it.Context());
        break;
    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        it.SetOn(dir > 0);
        if (it.callback)
            it.callback(this, it.Context());
        break;
    default:
        break;
    }
}

void DotUI::switchItem(DotItem &it)
{ // 目前未被调用，STEP 13 应用层用
    if (it.type != ItemType::ITEM_SWITCH && it.type != ItemType::ITEM_CHECKBOX)
        return;
    it.Toggle();
    if (it.callback)
        it.callback(this, it.Context());
}

// ==================== 3. 输入分派（方案 A：双态分派） ====================
// 导航态：上下移焦点，OK 执行/进入编辑，双击 OK 返回上一级，长按 OK 回主页；
// 编辑态：上下调值（实时回调），OK 提交退出，双击 OK 取消并返回上一级。
// 3 键硬件（上/下/确认）完整可用；4 键时左右键在两态下都直接调值
void DotUI::handleEvent(DotInput::Event ev, DotInput::Key k)
{
    DotPage *page = CurrentPage();
    const bool iconPage = !editing_ && page && page->GetMode() == DotPage::ICON;

    if (ev == DotInput::EV_CLICK || ev == DotInput::EV_REPEAT)
    {
        const bool click = (ev == DotInput::EV_CLICK);

        switch (k)
        {
        case DotInput::KEY_UP:
            editing_ ? AdjustCurrent(+1) : MoveFocus(-1);
            break;
        case DotInput::KEY_DOWN:
            editing_ ? AdjustCurrent(-1) : MoveFocus(+1);
            break;
        case DotInput::KEY_LEFT:
            if (iconPage)
                MoveFocus(-1);
            else
                AdjustCurrent(-1);
            break;
        case DotInput::KEY_RIGHT:
            if (iconPage)
                MoveFocus(+1);
            else
                AdjustCurrent(+1);
            break;
        case DotInput::KEY_OK:
            if (!click)
                break;
            // 双击：事后升级判定（第二次 CLICK 距上次 <250ms）。
            // 导航态单击已先执行过，双击时再 Back 只是抵消，无副作用
            if (now_ - lastOkClick_ < kDblClickMs) {
                if (editing_)
                    ExitEdit(false);      // 取消编辑（回滚快照）
                lastOkClick_ = 0;
                Back();                   // 返回上一级
            }
            else if (editing_) {
                ExitEdit(true);           // 提交并退出编辑
                lastOkClick_ = now_;
            }
            else {
                Press();                  // 执行 / 进入编辑
                lastOkClick_ = now_;
            }
            break;
        case DotInput::KEY_BACK:
            if (click)
                Back();
            break;
        default:
            break;
        }
        return;
    }

    // 长按：OK 回主页；上下键跳列表首/尾（仅导航态）
    if (ev == DotInput::EV_LONG)
    {
        if (k == DotInput::KEY_OK) {
            HomeScreen(nullptr);
        }
        else if (!editing_ && page) {
            if (k == DotInput::KEY_UP)
                SetFocus(0);
            else if (k == DotInput::KEY_DOWN)
                SetFocus(page->ItemCount() - 1);
        }
    }
}

// ============================ 5. 列表页渲染（最小版） ============================
void DotUI::renderPage()
{
    DotPage *p = CurrentPage();
    if (!p)
        return;

    switch (p->GetMode())
    {
    case DotPage::ICON:
        renderIconPage(*p);
        break; // STEP 11
    case DotPage::CUSTOM:
        renderCustom(*p);
        break;
    default:
        renderList(*p);
        break;
    }
}

// 横向轮播：焦点图标固定屏幕中心，其余图标按弹簧水平滑动。
// 不画选择框，焦点图标下方显示名称（文本来自 item，应用层维护）。
void DotUI::renderIconPage(DotPage& p) {
    const int count   = p.ItemCount();
    const int centerX = DotLayout::kScreenW / 2;

    // 图标页的水平"滚动量"直接等于焦点位置
    scroll_.target = focusS_.pos;
    focusS_.step(dt_);
    scroll_.step(dt_);

    const int iconSize = DotLayout::kIconLogical * 3;   // 放大 3 倍 = 42px
    const int iconY    = DotLayout::kIconTop;
    const int cell     = DotLayout::kIconCell;

    // 逐个画图标：相对位置决定 x；只画“至少一半在屏内”的图标，
    // 避免相邻图标只露一条边形成残块
    for (int i = 0; i < count; ++i) {
        const float rel = (float)i - scroll_.pos;
        const int x = centerX - iconSize / 2 + (int)(rel * (float)cell);
        if (x < -iconSize / 2 || x > DotLayout::kScreenW - iconSize / 2) continue;

        DotItem* it = p.Item(i);
        const uint8_t* src = (it && it->iconSrc) ? it->iconSrc
                                                 : DotRes::kIconDefault; // 兑底默认图标
        if (src) {
            u8g2_SetDrawColor(g2_, theme_.fg);
            // 大图标（边长超过 3 倍逻辑尺寸）缩放绘制到统一盒子里，
            // 小图标仍按 3 倍放大——两种来源在同一页面尺寸一致
            const int n = src[0];
            if (n > DotLayout::kIconLogical * 3)
                DrawIcon(src, x, iconY, 1, iconSize);
            else
                DrawIcon(src, x, iconY, 3);
        }
    }

    // 焦点图标的名称（大字），居中显示；基线 = kIconTop + kIconNameY（可调）。
    // 名称取 TargetIndex()（目标索引）而非 FocusIndex()（弹簧当前位置）：
    // 欠阻尼时弹簧会过冲回弹，FocusIndex() 在新旧行号间反复横跳，
    // 名称就会 A→B→A 地闪；图标用浮点位置画可以跟随回弹，
    // 但离散文本只应跟目标，到位即稳定。
    DotItem* cur = p.Item(TargetIndex());
    if (cur) {
        u8g2_SetDrawColor(g2_, theme_.fg);

        if (fontBig_) {
            u8g2_SetFont(g2_, fontBig_);
            const int tw = (int)u8g2_GetUTF8Width(g2_, cur->GetText());
            u8g2_DrawUTF8(g2_, (int16_t)(centerX - tw / 2),
                          (int16_t)(DotLayout::kIconTop + DotLayout::kIconNameY),
                          cur->GetText());
        }

        if (fontList_) u8g2_SetFont(g2_, fontList_);   // 恢复列表字体
    }

    u8g2_SetDrawColor(g2_, theme_.fg);
}
void DotUI::renderCustom(DotPage &p)
{
    if (p.drawCustom)
        p.drawCustom(*this, p);
}

void DotUI::renderList(DotPage &p)
{
    const int rows = DotLayout::kScreenH / DotLayout::kRowH;
    const int count = p.ItemCount();

    // 分页式滚动：按"目标索引"判断是否翻页。
    // 用目标而非当前值，保证底部继续下移时页面与选中框同帧起跳。
    const float maxScroll = (count > rows) ? (float)(count - rows) : 0.0f;
    float target = scroll_.target;
    const float f = (float)TargetIndex();
    if (f < target)
    {
        target = f;
    }
    else if (f >= target + (float)rows)
    {
        target = f - (float)(rows - 1); // 焦点贴底：滚到正好显示它
    }
    if (target < 0.0f)
        target = 0.0f;
    if (target > maxScroll)
        target = maxScroll;
    scroll_.target = target;

    focusS_.step(dt_); // ★ 弹簧推进（dt_ 在 Frame() 里已算好）
    scroll_.step(dt_);

    // 页面进入动画：slide_ 0→1（时间驱动，约 1/6 秒）
    if (slide_ < 1.0f)
    {
        slide_ += dt_ * 6.0f;
        if (slide_ > 1.0f)
            slide_ = 1.0f;
    }

    // 画可见行（多画一行作为滚动过渡）
    const int first = (int)scroll_.pos;
    const int last = first + rows;
    for (int i = first; i <= last && i < count; ++i)
    {
        DotItem *it = p.Item(i);
        if (!it)
            continue;

        const float rowF = (float)i - scroll_.pos; // 浮点行号！
        if (rowF < -1.0f || rowF > (float)rows)
            continue;

        drawRow(*it, rowF, (i == FocusIndex()), slide_);
    }

    // 选中框：x/y/宽度三个弹簧同步运动；XOR 反色覆盖整行文本
    const float focusRow = focusS_.pos - scroll_.pos;
    int barY = DotLayout::kListTop +
               (int)((focusRow * (float)DotLayout::kRowH) * slide_);

    if (count > 0)
    {
        syncSelectionTarget();
        selX_.step(dt_);
        selW_.step(dt_);

        // 到位兜底：所有弹簧收敛后强制对齐整数行。
        // 浮点积分收敛时仍可能残留亚像素误差（如 8.0001），
        // 而文本行 y 用整数索引算，相减会造成 1px 视觉偏移。
        if (slide_ >= 1.0f && focusS_.settled() && scroll_.settled())
        {
            barY = DotLayout::kListTop +
                   (FocusIndex() - (int)(scroll_.pos + 0.5f)) * DotLayout::kRowH;
        }

        int sx = (int)(selX_.pos + 0.5f);
        int sw = (int)(selW_.pos + 0.5f);
        const int rightLimit = DotLayout::kNavX;

        if (sx < 0)
            sx = 0;
        if (sx > rightLimit)
            sx = rightLimit;
        if (sw < 1)
            sw = 1;
        if (sx + sw > rightLimit)
            sw = rightLimit - sx;

        u8g2_SetDrawColor(g2_, 2); // XOR：底、字一起反色
        FillRoundOnce(sx, barY, sw, DotLayout::kRowH,
                      DotLayout::kSelectionRadius);
        u8g2_SetDrawColor(g2_, theme_.fg);
    }

    // 右侧导航栏：铺底色盖住列表溢出，再画滚动条
    u8g2_SetDrawColor(g2_, theme_.bg);
    FillBox(DotLayout::kNavX, 0, DotLayout::kNavW, DotLayout::kScreenH);

    if (count > rows)
        drawScrollbar(count, rows, maxScroll);
}

// 滚动条：轨道 + 按比例缩放的滑块，位置跟随滚动弹簧
void DotUI::drawScrollbar(int count, int rows, float maxScroll)
{
    const int trackX = DotLayout::kScrollBarX;
    const int trackW = DotLayout::kScrollBarW;
    const int trackY = DotLayout::kListTop + 1;
    const int trackH = rows * DotLayout::kRowH - 2;

    // 滑块高度 ∝ 可视行数/总行数，限制最小面积
    int thumbH = (trackH * rows) / count;
    if (thumbH < 8)
        thumbH = 8;
    if (thumbH > trackH)
        thumbH = trackH;

    float pos = scroll_.pos;
    if (pos < 0.0f)
        pos = 0.0f;
    if (pos > maxScroll)
        pos = maxScroll;

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
void DotUI::drawRow(DotItem &it, float rowF, bool selected, float slide)
{
    const int y = DotLayout::kListTop +
                  (int)((rowF * (float)DotLayout::kRowH) * slide);
    const int x = DotLayout::kPadItem;
    const int base = RowBaseline(y);

    u8g2_SetDrawColor(g2_, theme_.fg);

    const char *prefix = (it.type == ItemType::ITEM_CALL_PAGE)
                             ? DotRes::kPrefixPage
                             : DotRes::kPrefixFunction;
    DrawText(DotLayout::kPrefixX, base, prefix);

    switch (it.type)
    {
    case ItemType::ITEM_TEXT:
        DrawTextClipped(x, base, it.GetText(), DotLayout::kNavX);
        break;

    // 开关：左侧标签截断避让，右侧右对齐 开/关（编辑态选中行反白）
    case ItemType::ITEM_SWITCH:
    {
        const char *state = it.IsOn() ? DotRes::kTextOn : DotRes::kTextOff;
        DrawTextClipped(x, base, it.GetText(),
                        DotLayout::kWidgetRight - TextWidth(state) - 4);
        DrawValueRight(state, y, base, selected && editing_);
        break;
    }

    // 勾选框：左侧标签截断避让，右侧右对齐方块（与开关/数值布局一致）；
    // 编辑态选中行时整块反白（bg 色画框/勾，形成黑白反转）
    case ItemType::ITEM_CHECKBOX:
    {
        const int bs = 9;
        const int bx = DotLayout::kWidgetRight - bs;
        const bool inv = selected && editing_;
        if (inv) {
            FillBox(bx - 2, y + (DotLayout::kRowH - bs) / 2 - 2, bs + 4, bs + 4);
            u8g2_SetDrawColor(g2_, theme_.bg);
        }
        FrameBox(bx, y + (DotLayout::kRowH - bs) / 2, bs, bs);
        if (it.IsOn())
            FillBox(bx + 2, y + (DotLayout::kRowH - bs) / 2 + 2, bs - 4, bs - 4);
        if (inv)
            u8g2_SetDrawColor(g2_, theme_.fg);
        DrawTextClipped(x, base, it.GetText(), bx - 4);
        break;
    }

    // 数值：右侧右对齐显示；档位名表优先（如屏幕方向显示 0/90/180/270），
    // 其次 valueFmt 覆盖，最后回落默认格式
    case ItemType::ITEM_VALUE:
    {
        if (it.valueNames) {
            const int gi = (int)(it.value + 0.5f);
            snprintf(it.valueBuf, sizeof(it.valueBuf), "%s", it.valueNames[gi]);
        } else {
            snprintf(it.valueBuf, sizeof(it.valueBuf),
                     it.valueFmt ? it.valueFmt : DotRes::kFormatValue, it.value);
        }
        DrawTextClipped(x, base, it.GetText(),
                        DotLayout::kWidgetRight - TextWidth(it.valueBuf) - 4);
        DrawValueRight(it.valueBuf, y, base, selected && editing_);
        break;
    }

    // 进度条：右侧固定宽度圆角条
    case ItemType::ITEM_PROGRESS_BAR:
    {
        const int bw = DotLayout::kBarW;
        const int bh = DotLayout::kBarH;
        const int bx = DotLayout::kWidgetRight - bw;
        const int by = y + (DotLayout::kRowH - bh) / 2;

        DrawTextClipped(x, base, it.GetText(), bx - 4);

        FrameRound(bx, by, bw, bh, 2);
        const int fw = (bw - 4) * (int)it.value / 100;
        if (fw > 0)
            FillRound(bx + 2, by + 2, fw, bh - 4, 1);
        break;
    }

    // 跑马灯：仅"选中且超宽"时滚动；可选行尾状态图标
    case ItemType::ITEM_TEXT_MARQUEE:
    {
        const int tw = TextWidth(it.GetText());
        int mx = 0;
        if (tw > DotLayout::kMarqueeGate && selected)
            mx = it.marquee.step(tw, now_);
        else
            it.marquee.reset();

        DrawTextClipped(x + mx, base, it.GetText(), DotLayout::kNavX);

        if (it.statusIcon)
        {
            DrawRawBmp(it.statusIcon, DotLayout::kStatusIconX, y + 3,
                       DotLayout::kStatusIconS, DotLayout::kStatusIconS);
        }
        break;
    }

    case ItemType::ITEM_CALL_PAGE:
    case ItemType::ITEM_CALL_FUNCTION:
        DrawTextClipped(x, base, it.GetText(), DotLayout::kNavX);
        break;
    }
}

// ============================ 7. 绘制工具 ============================
int DotUI::TextWidth(const char *s)
{
    if (!s || !g2_)
        return 0;
    if (fontList_)
        u8g2_SetFont(g2_, fontList_);
    return (int)u8g2_GetUTF8Width(g2_, s);
}

// 字体上延：从 u8g2 字体头第 13 字节读（有符号）
int DotUI::FontAscent()
{
    if (!fontList_)
        return 8;
    return (int)(int8_t)fontList_[13];
}

void DotUI::DrawText(int x, int y, const char *s)
{
    if (!s || !g2_)
        return;
    if (fontList_)
        u8g2_SetFont(g2_, fontList_);
    u8g2_DrawUTF8(g2_, (int16_t)x, (int16_t)y, s);
}

void DotUI::DrawTextCenter(int cx, int y, const char *s)
{
    DrawText(cx - TextWidth(s) / 2, y, s);
}
void DotUI::DrawTextRight(int rightX, int y, const char *s)
{
    DrawText(rightX - TextWidth(s), y, s);
}

// 逐 UTF-8 码点绘制并在 maxX 截断。
// 不能按字节截：中文一个码点 3 字节，按字节切会产生乱码。
void DotUI::DrawTextClipped(int x, int y, const char *s, int maxX)
{
    if (!s || !g2_)
        return;
    if (fontList_)
        u8g2_SetFont(g2_, fontList_);

    char one[5] = {0};
    int cx = x;
    const char *p = s;

    while (*p)
    {
        const unsigned char c = (unsigned char)*p;
        int len = 1; // 由首字节判断码点长度
        if (c >= 0xF0)
            len = 4;
        else if (c >= 0xE0)
            len = 3;
        else if (c >= 0xC0)
            len = 2;

        for (int i = 0; i < len && p[i]; ++i)
            one[i] = p[i];
        one[len] = '\0';

        const int w = (int)u8g2_GetUTF8Width(g2_, one);
        if (cx + w > maxX)
            break; // 放不下就停

        u8g2_DrawUTF8(g2_, (int16_t)cx, (int16_t)y, one);
        cx += w;
        p += len;
    }
}

// 固件格式图标：icon[0]=边长 n（上限 64，支持大尺寸图标），
// 位图 MSB-first，0 位着色（1 = 透明）。
// dstSize > 0 时把图标缩放绘制到 dstSize x dstSize 的盒子里（取最近邻采样），
// 否则按 scale 放大绘制（原行为）。
void DotUI::DrawIcon(const uint8_t* icon, int x, int y, int scale, int dstSize) {
    if (!icon) return;
    const int n = icon[0];
    if (n <= 0 || n > 64) return;

    const int stride = (n + 7) >> 3;

    if (dstSize > 0) {
        // 缩放模式：目标盒 dstSize，源 n x n，最近邻采样逐像素直绘
        for (int r = 0; r < dstSize; ++r) {
            const int sr = (r * n) / dstSize;            // 源行
            for (int c = 0; c < dstSize; ++c) {
                const int sc = (c * n) / dstSize;        // 源列
                const uint8_t b = icon[1 + sr * stride + (sc >> 3)];
                if ((b >> (7 - (sc & 7))) & 1) continue; // 透明
                u8g2_DrawPixel(g2_, (int16_t)(x + c), (int16_t)(y + r));
            }
        }
        return;
    }

    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            const uint8_t b = icon[1 + r * stride + (c >> 3)];
            if ((b >> (7 - (c & 7))) & 1) continue;   // 1 = 透明，跳过

            if (scale <= 1)
                u8g2_DrawPixel(g2_, (int16_t)(x + c), (int16_t)(y + r));
            else
                u8g2_DrawBox(g2_, (int16_t)(x + c * scale),
                             (int16_t)(y + r * scale),
                             (int16_t)scale, (int16_t)scale);
        }
}

void DotUI::FillBox(int x, int y, int w, int h)
{
    u8g2_DrawBox(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h);
}
void DotUI::FrameBox(int x, int y, int w, int h)
{
    u8g2_DrawFrame(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h);
}

// 行内文本垂直居中基线：按字体度量算，不写死偏移。
//   6x12 : ascent 7, descent -2 → 基线 y+10；wqy12: ascent 8 → y+11
// 汉字墨迹比 ascent_A 更高，中文再补 1px 才视觉居中。
int DotUI::RowBaseline(int rowTop)
{
    const uint8_t *f = fontList_ ? fontList_ : DotRes::kFontSmall;
    const int ascent = (int)(int8_t)f[13];
    const int descent = (int)(int8_t)f[14];
    const int glyphH = ascent - descent;

    int top = (DotLayout::kRowH - glyphH) / 2;
    if (top < 0)
        top = 0;

    int base = rowTop + top + ascent;
    if (DotRes::kLanguage)
        base += 1; // 中文光学补偿
    return base;
}

// u8g2_DrawRBox 的坑：
//   1. 内部用无符号类型算 w-2r，宽/高不足 2r+1 时下溢成 65535，画穿屏幕；
//   2. r=1 时 1/4 圆盘是 3 像素 L 形，拼出的"圆角"两端有 1px 毛刺。
// 所以 r<=1 或尺寸退化时一律降级为普通矩形。
void DotUI::FillRound(int x, int y, int w, int h, int r)
{
    if (r <= 1 || w <= 2 * r || h <= 2 * r)
    {
        FillBox(x, y, w, h);
        return;
    }
    u8g2_DrawRBox(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r);
}
void DotUI::FrameRound(int x, int y, int w, int h, int r)
{
    if (r > 0 && (w <= 2 * r || h <= 2 * r))
    {
        FrameBox(x, y, w, h);
        return;
    }
    u8g2_DrawRFrame(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r);
}

// 裸位图：无长度头，1 位着色（与 DrawIcon 相反）
void DotUI::DrawRawBmp(const uint8_t *bmp, int x, int y, int w, int h)
{
    if (!bmp)
        return;
    const int stride = (w + 7) >> 3;
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
        {
            const uint8_t b = bmp[r * stride + (c >> 3)];
            if ((b >> (7 - (c & 7))) & 1)
                u8g2_DrawPixel(g2_, (int16_t)(x + c), (int16_t)(y + r));
        }
}

// 逐行填充圆角矩形：每个像素只绘制一次。
// XOR 模式下必须用它代替 FillRound，否则角部被反色两次出现空洞。
void DotUI::FillRoundOnce(int x, int y, int w, int h, int r)
{
    if (!g2_ || w <= 0 || h <= 0)
        return;

    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    if (r < 0)
        r = 0;

    for (int j = 0; j < h; ++j)
    {
        int inset = 0;
        if (j < r)
        {
            inset = r - 1 - j; // 上部圆角
        }
        else if (j >= h - r)
        {
            inset = r - 1 - (h - 1 - j); // 下部圆角（对称）
        }
        if (inset < 0)
            inset = 0;

        const int sx = x + inset;
        const int sw = w - inset * 2;
        if (sw <= 0)
            continue;

        u8g2_DrawHLine(g2_, (u8g2_uint_t)sx,
                       (u8g2_uint_t)(y + j), (u8g2_uint_t)sw);
    }
}

// 选中框几何：x 固定为前缀起点，宽度 = 前缀 + 间距 + 文本内容。
// 开关/数值/进度条只选中标签部分，右侧控件不参与。
void DotUI::selectionMetrics(DotItem &it, int &x, int &w)
{
    x = DotLayout::kPrefixX;
    const int lead = DotLayout::kPadItem - DotLayout::kPrefixX;

    // 标签宽度，但不得与右侧控件重叠
    auto labelClamped = [&](int reserve) -> int
    {
        const int maxX = DotLayout::kWidgetRight - reserve - 4;
        const int tw = TextWidth(it.GetText());
        return (DotLayout::kPadItem + tw > maxX)
                   ? (maxX - DotLayout::kPadItem)
                   : tw;
    };

    int content = 0;
    switch (it.type)
    {
    case ItemType::ITEM_TEXT:
    case ItemType::ITEM_CALL_PAGE:
    case ItemType::ITEM_CALL_FUNCTION:
    case ItemType::ITEM_TEXT_MARQUEE:
        content = TextWidth(it.GetText());
        break;
    case ItemType::ITEM_CHECKBOX:
        // 勾选框右置后选中框只包标签（与开关/数值一致）；9 = 右侧方块宽
        content = labelClamped(9);
        break;
    case ItemType::ITEM_SWITCH:
        content = labelClamped(TextWidth(it.IsOn() ? DotRes::kTextOn
                                                   : DotRes::kTextOff));
        break;
    case ItemType::ITEM_VALUE:
        // 数值项选中框只包住左侧标签（不包右侧数值），
        // 与右侧数值间保留间距；编辑态也用它作为唯一指示
        content = labelClamped(TextWidth(it.GetText()));
        break;
    case ItemType::ITEM_PROGRESS_BAR:
        content = labelClamped(DotLayout::kBarW);
        break;
    default:
        break;
    }

    if (content < 0)
        content = 0;
    w = lead + content;

    const int rightLimit = DotLayout::kNavX;
    if (w < 0)
        w = 0;
    if (x + w > rightLimit)
        w = rightLimit - x;
    if (w < 0)
        w = 0;
}

// 把目标项的选中框 x/宽度同步到 selX_/selW_ 的 target。
// 每帧渲染前调用，语言切换、开关状态变化引起的宽度变化也能跟上。
void DotUI::syncSelectionTarget()
{
    DotPage *p = CurrentPage();
    if (!p || p->ItemCount() == 0)
    {
        selX_.target = 0.0f;
        selW_.target = 0.0f;
        return;
    }

    int idx = TargetIndex();
    if (idx < 0)
        idx = 0;
    if (idx >= p->ItemCount())
        idx = p->ItemCount() - 1;

    int x = 0, w = 0;
    selectionMetrics(*p->Item(idx), x, w);

    w += DotLayout::kSelectionOverhang; // 右侧外扩，包围感

    const int rightLimit = DotLayout::kNavX;
    if (x < 0)
        x = 0;
    if (x > rightLimit)
        x = rightLimit;
    if (x + w > rightLimit)
        w = rightLimit - x;
    if (w < 0)
        w = 0;

    selX_.target = (float)x;
    selW_.target = (float)w;
}

// 页面切换时选中框瞬移（不做动画）
void DotUI::resetSelectionAnim()
{
    syncSelectionTarget();
    selX_.reset(selX_.target);
    selW_.reset(selW_.target);
}

// 1) 覆盖层栈（dot_ui.cpp）
void DotUI::PushOverlay(DotWidget* w) {
    if (!w || overlayDepth_ >= kMaxOverlays) return;
    overlay_[overlayDepth_++] = w;
}
void DotUI::PopOverlay() {
    if (overlayDepth_ <= 0) return;
    DotWidget* w = overlay_[--overlayDepth_];
    overlay_[overlayDepth_] = nullptr;
    DotWidget::Free(w);
}
DotWidget* DotUI::TopOverlay() {
    if (overlayDepth_ <= 0) return nullptr;
    return overlay_[overlayDepth_ - 1];
}

// 覆盖层叠画：逐层绘制（最后画的在最上面），完成后恢复前景色
void DotUI::drawOverlays() {
    for (int i = 0; i < overlayDepth_; ++i)
        if (overlay_[i]) overlay_[i]->Draw(*this);
    u8g2_SetDrawColor(g2_, theme_.fg);
}
// dot_page.cpp —— 页面池与列表项注册
#include "dot_page.h"
#include "dot_ui.h"

DotPage DotPage::s_pool_[DotPage::kMaxPages];
int      DotPage::s_count_ = 0;
constexpr int DotPage::kMaxPages;

DotPage* DotPage::AddPage(Mode mode, const char* name) {
    if (s_count_ >= kMaxPages) return nullptr;

    DotPage* p = &s_pool_[s_count_];
    p->mode_  = mode;
    p->name_  = name ? name : "";
    p->index_ = s_count_;
    p->savedFocus = 0.0f;
    p->backPage   = -1;
    p->drawCustom = nullptr;
    p->items_.clear();     // 只清长度，复用已分配内存

    ++s_count_;
    return p;
}

void DotPage::ResetPages() {
    for (int i = 0; i < s_count_; ++i) {
        DotPage& p = s_pool_[i];
        for (size_t k = 0; k < p.items_.size(); ++k)
            delete p.items_[k];      // 列表项是堆分配的，要回收
        p.items_.clear();
        p.savedFocus = 0.0f;
        p.backPage   = -1;
        p.drawCustom = nullptr;
    }
    s_count_ = 0;
}

int DotPage::PageCount() { return s_count_; }

DotPage* DotPage::GetPage(int index) {
    if (index < 0 || index >= s_count_) return nullptr;
    return &s_pool_[index];
}

// 注册列表项：按类型解释 param1/param2
//   ITEM_CALL_PAGE     param1 = DotPage*（自动回填其 backPage）
//   ITEM_SWITCH/CHECK  param1 = 初始开/关（非 0 即开）
//   ITEM_VALUE         param1 = 最小值，param2 = 最大值（0 视为 100）
//   ITEM_PROGRESS_BAR  param1 = 初始进度（0~100）
DotItem* DotPage::AddItemRaw(ItemType type, const char* description,
                               const uint8_t* iconSrc, uintptr_t param1,
                               uintptr_t param2, DotCallback callback) {
    DotItem* it = new DotItem();
    if (!it) return nullptr;

    it->type     = type;
    it->iconSrc  = iconSrc;
    it->param1   = param1;
    it->param2   = param2;
    it->callback = callback;
    it->SetText(description);

    switch (type) {
    case ItemType::ITEM_CALL_PAGE: {
        DotPage* target = dotFromUptr<DotPage*>(param1);
        if (target) target->backPage = index_;   // 子页记住父页
        break;
    }
    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        it->SetOn(param1 != 0);
        break;
    case ItemType::ITEM_VALUE:
        it->valueMin = (float)(intptr_t)param1;
        it->valueMax = param2 ? (float)(intptr_t)param2 : 100.0f;
        it->value    = it->valueMin;
        break;
    case ItemType::ITEM_PROGRESS_BAR:
        it->valueMin = 0.0f;
        it->valueMax = 100.0f;
        it->value    = dotClampf((float)(intptr_t)param1, 0.0f, 100.0f);
        break;
    default:
        break;
    }

    items_.push_back(it);
    return it;
}

int DotPage::IndexOf(const DotItem* it) const {
    if (!it) return -1;
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i] == it) return (int)i;
    return -1;
}

// 注册档位选择项：VALUE 控件的快捷封装，适合语言/屏幕方向/阻尼模式
// 这类"右侧显示自定义档位内容"的设置项。
//   量程 = 0 ~ count-1，固定步长 1；
//   右侧显示 sel.names[档位]（drawRow 优先读 valueNames）；
//   sel.wrap 为 true 时首尾循环（如 270° → 0°、过阻尼 → 欠阻尼）；
//   常驻编辑态，左右键直接切档；切档后触发 sel.callback。
// 双语档位名：应用层用 reg() 登记中英名表，语言切换时刷新 valueNames。
DotItem* DotPage::AddSelector(const char* description,
                              const DotSelectorDesc& sel) {
    DotItem* it = AddItemRaw(ItemType::ITEM_VALUE, description, nullptr,
                             0, (uintptr_t)(sel.count - 1), sel.callback);
    if (!it) return nullptr;

    it->step       = 1.0f;        // 整档切换
    it->wrap       = sel.wrap;    // 首尾循环开关
    it->valueNames = sel.names;   // 右侧档位显示内容（应用层可换表）
    it->SetValue((float)(sel.initial >= 0 && sel.initial < sel.count
                             ? sel.initial : 0));
    it->SetEditing(true);         // 常驻编辑态：左右键直接切档
    return it;
}
// mono_page.cpp —— 页面池与列表项注册
#include "mono_page.h"
#include "mono_ui.h"

MonoPage MonoPage::s_pool_[MonoPage::kMaxPages];
int      MonoPage::s_count_ = 0;

constexpr int MonoPage::kMaxPages;

// 注册新页面：从池中取一个槽位并初始化导航状态
MonoPage* MonoPage::AddPage(Mode mode, const char* name) {
    if (s_count_ >= kMaxPages) return nullptr;

    MonoPage* p = &s_pool_[s_count_];
    p->mode_      = mode;
    p->name_      = name ? name : "";
    p->index_     = s_count_;
    p->savedFocus = 0.0f;
    p->backPage   = -1;
    p->drawCustom = nullptr;
    p->items_.clear();

    ++s_count_;
    return p;
}

// 回收所有列表项并清零页面数（供 InitLayout 重新布局前调用）
void MonoPage::ResetPages() {
    for (int i = 0; i < s_count_; ++i) {
        MonoPage& p = s_pool_[i];
        for (size_t k = 0; k < p.items_.size(); ++k)
            delete p.items_[k];
        p.items_.clear();
        p.savedFocus = 0.0f;
        p.backPage   = -1;
        p.drawCustom = nullptr;
    }
    s_count_ = 0;
}

int MonoPage::PageCount() { return s_count_; }

MonoPage* MonoPage::GetPage(int index) {
    if (index < 0 || index >= s_count_) return nullptr;
    return &s_pool_[index];
}

// 注册列表项：按类型解释 param1/param2 并完成初始化。
// 类型约定：
//   ITEM_CALL_PAGE     param1 = MonoPage*（目标页，自动回填其 backPage）
//   ITEM_SWITCH/CHECK  param1 = 初始开/关（非 0 即开）
//   ITEM_VALUE         param1 = 最小值，param2 = 最大值（0 视为 100）
//   ITEM_PROGRESS_BAR  param1 = 初始进度（0~100）
MonoItem* MonoPage::AddItemRaw(ItemType       type,
                               const char*    description,
                               const uint8_t* iconSrc,
                               uintptr_t      param1,
                               uintptr_t      param2,
                               MonoCallback   callback) {
    MonoItem* it = new MonoItem();
    if (!it) return nullptr;

    it->type     = type;
    it->iconSrc  = iconSrc;
    it->param1   = param1;
    it->param2   = param2;
    it->callback = callback;
    it->SetText(description);

    switch (type) {
    case ItemType::ITEM_CALL_PAGE: {
        // 子页记住父页索引，Back 时据此返回
        MonoPage* target = monoFromUptr<MonoPage*>(param1);
        if (target) target->backPage = index_;
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
        it->value    = monoClampf((float)(intptr_t)param1, 0.0f, 100.0f);
        break;

    default:
        break;
    }

    items_.push_back(it);
    return it;
}

int MonoPage::IndexOf(const MonoItem* it) const {
    if (!it) return -1;
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i] == it) return (int)i;
    return -1;
}

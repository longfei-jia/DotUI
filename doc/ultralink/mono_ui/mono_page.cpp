// mono_page.cpp —— 页面池与加项
//
// 对应 3rdParty/mono_ui/mono_page.cpp。
#include "mono_page.h"
#include "mono_ui.h"

// 静态池定义（访问私有构造函数的唯一位置）
MonoPage MonoPage::s_pool_[MonoPage::kMaxPages];
int      MonoPage::s_count_ = 0;

// C++11 下 constexpr 静态成员需要类外定义才能被 odr-use
constexpr int MonoPage::kMaxPages;

// ---------------------------------------------------------------------------
// 页面池
// ---------------------------------------------------------------------------
MonoPage* MonoPage::AddPage(Mode mode, const char* name) {
    if (s_count_ >= kMaxPages) return nullptr;

    MonoPage* p = &s_pool_[s_count_];
    p->mode_      = mode;
    p->name_      = name ? name : "";
    p->index_     = s_count_;
    p->savedFocus = 0.0f;
    p->backPage   = -1;
    p->drawCustom = nullptr;
    p->enterSlide = 1.0f;
    p->items_.clear();

    ++s_count_;
    return p;
}

void MonoPage::ResetPages() {
    for (int i = 0; i < s_count_; ++i) {
        MonoPage& p = s_pool_[i];
        for (size_t k = 0; k < p.items_.size(); ++k)
            delete p.items_[k];
        p.items_.clear();
        p.savedFocus = 0.0f;
        p.backPage   = -1;
        p.drawCustom = nullptr;
        p.enterSlide = 1.0f;
    }
    s_count_ = 0;
}

int MonoPage::PageCount() { return s_count_; }

MonoPage* MonoPage::GetPage(int index) {
    if (index < 0 || index >= s_count_) return nullptr;
    return &s_pool_[index];
}

// ---------------------------------------------------------------------------
// 加项
// ---------------------------------------------------------------------------
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

    // param1 / param2 的语义随 type 变化 —— 与截图里的用法一一对齐
    switch (type) {
    case ItemType::ITEM_CALL_PAGE: {
        // param1 = 目标页。顺手建立返回链接，这样 Back() 不需要额外配置。
        // （对应固件 Page+0x49 的 backPage 字节）
        MonoPage* target = monoFromUptr<MonoPage*>(param1);
        if (target) {
            target->backPage = index_;
            // param2 若给了非 0，表示进页后直接定焦到该行
        }
        break;
    }

    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        // param1 = 初值（截图里写的是 true / false）
        it->SetOn(param1 != 0);
        break;

    case ItemType::ITEM_VALUE:
        // param1 = 下限，param2 = 上限；都是按整数传的
        it->valueMin = (float)(intptr_t)param1;
        it->valueMax = param2 ? (float)(intptr_t)param2 : 100.0f;
        it->value    = it->valueMin;
        break;

    case ItemType::ITEM_PROGRESS_BAR:
        // param1 = 0..100 的初值
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

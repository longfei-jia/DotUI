// mono_page.h —— 页面与页面池
//
// 对应 3rdParty/mono_ui/mono_page.h。
//
// 关键设计（从截图反推）：AddPage 是**静态方法**，返回 MonoPage*。
// 这说明页面池由类级静态存储统一管理，页面不归某个 MonoUI 实例所有。
// 这与 UltraLink 固件的实测一致 —— UI 单例 +0x12C pageCount / +0x130 cap /
// +0x134 array，页面是全局登记表。
//
// AddItem 用模板接收 param1/param2，因为作者示例里同一个位置混传了
// `pageShow`（指针）、`0`（整数）、`true`/`false`（布尔）、`nullptr`。
#ifndef MONO_PAGE_H
#define MONO_PAGE_H

#include "mono_defs.h"
#include "mono_item.h"
#include "../uvector/uvector.h"

class MonoPage {
public:
    // 页面模式。对应截图里的 MonoPage::LIST / MonoPage::ICON。
    enum Mode : uint8_t {
        LIST = 0,   // 列表页，走通用列表渲染器
        ICON = 1,   // 图标页，卡片式轮播（固件主界面就是这种）
        CUSTOM = 2, // 自绘页，交给 drawCustom（移植时放波形图/表盘用）
    };

    // =====================================================================
    // 页面池（静态）
    // =====================================================================
    static constexpr int kMaxPages = 16;

    // 建页。返回 nullptr 表示超出 kMaxPages。
    static MonoPage* AddPage(Mode mode, const char* name);

    static void      ResetPages();              // 清空（重新 InitLayout 前调用）
    static int       PageCount();
    static MonoPage* GetPage(int index);        // 越界返回 nullptr

    // =====================================================================
    // 加项
    // =====================================================================
    // 6 参数版本，与截图里的调用完全对应：
    //   AddItem(ItemType::ITEM_CHECKBOX, "Checkbox1", nullptr, true, false, nullptr)
    //                                        ^type       ^desc   ^icon  ^p1   ^p2   ^cb
    template <typename P1, typename P2>
    MonoItem* AddItem(ItemType      type,
                      const char*   description,
                      const uint8_t* iconSrc,
                      P1            param1,
                      P2            param2,
                      MonoCallback  callback) {
        return AddItemRaw(type, description, iconSrc,
                          monoToUptr(param1), monoToUptr(param2), callback);
    }

    MonoItem* AddItemRaw(ItemType       type,
                         const char*    description,
                         const uint8_t* iconSrc,
                         uintptr_t      param1,
                         uintptr_t      param2,
                         MonoCallback   callback);

    // =====================================================================
    // 访问
    // =====================================================================
    Mode        GetMode() const { return mode_; }
    const char* Name()    const { return name_ ? name_ : ""; }

    int       ItemCount() const { return (int)items_.size(); }
    MonoItem* Item(int i) const {
        if (i < 0 || i >= (int)items_.size()) return nullptr;
        return items_[i];
    }
    int IndexOf(const MonoItem* it) const;

    // 本页在页面池里的下标（用于 backPage 与光标记忆）
    int Index() const { return index_; }

    // =====================================================================
    // 状态
    // =====================================================================
    float savedFocus = 0.0f;    // 离开本页时记下的焦点（固件 Page+0x48）
    int   backPage   = -1;      // 返回目标页下标，-1 表示根页

    // 自绘页
    typedef void (*DrawFn)(MonoUI& ui, MonoPage& page);
    DrawFn drawCustom = nullptr;

    // 本页进入时的展开动画进度（由 MonoUI 驱动）
    float enterSlide = 1.0f;

private:
    MonoPage() {}
    ~MonoPage() {}

    // 页池是全局唯一的，禁止拷贝
    MonoPage(const MonoPage&) = delete;
    MonoPage& operator=(const MonoPage&) = delete;

    Mode           mode_  = LIST;
    const char*    name_  = "";
    int            index_ = -1;
    uvector<MonoItem*> items_;

    // 页池本体。放在类内声明，才能在 mono_page.cpp 里构造私有构造函数的实例。
    static MonoPage s_pool_[kMaxPages];
    static int      s_count_;
};

#endif // MONO_PAGE_H

// dot_page.h —— 页面与页面池
#ifndef DOT_PAGE_H
#define DOT_PAGE_H

#include "dot_defs.h"
#include "dot_item.h"
#include "uvector.h"

class DotPage {
public:
    enum Mode : uint8_t {
        LIST   = 0,   // 垂直列表
        ICON   = 1,   // 图标横向轮播
        CUSTOM = 2,   // 完全交给 drawCustom 绘制
    };

    static constexpr int kMaxPages = 16;

    // ---- 页面池管理 ----
    static DotPage* AddPage(Mode mode, const char* name);
    static void      ResetPages();
    static int       PageCount();
    static DotPage* GetPage(int index);      // 越界返回 nullptr

    // ---- 添加列表项：param1/param2 做类型擦除 ----
    template <typename P1, typename P2>
    DotItem* AddItem(ItemType type, const char* description,
                      const uint8_t* iconSrc, P1 param1, P2 param2,
                      DotCallback callback) {
        return AddItemRaw(type, description, iconSrc,
                          dotToUptr(param1), dotToUptr(param2), callback);
    }
    DotItem* AddItemRaw(ItemType type, const char* description,
                         const uint8_t* iconSrc, uintptr_t param1,
                         uintptr_t param2, DotCallback callback);

    Mode        GetMode() const { return mode_; }
    const char* Name()    const { return name_ ? name_ : ""; }
    int         Index()   const { return index_; }

    int       ItemCount() const { return (int)items_.size(); }
    DotItem* Item(int i) const {
        if (i < 0 || i >= (int)items_.size()) return nullptr;
        return items_[i];
    }
    int IndexOf(const DotItem* it) const;

    // ---- 导航状态（MonoUI 读写）----
    float savedFocus = 0.0f;   // 离开页面时的焦点（返回时恢复）
    int   backPage   = -1;     // 父页索引，-1 表示回主页

    // ---- 自定义绘制 ----
    typedef void (*DrawFn)(DotUI& ui, DotPage& page);
    DrawFn drawCustom = nullptr;

private:
    DotPage() {}
    ~DotPage() {}
    DotPage(const DotPage&) = delete;
    DotPage& operator=(const DotPage&) = delete;

    Mode        mode_  = LIST;
    const char* name_  = "";
    int         index_ = -1;
    uvector<DotItem*> items_;

    static DotPage s_pool_[kMaxPages];
    static int      s_count_;
};

#endif
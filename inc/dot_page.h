// dot_page.h —— 页面与页面池
#ifndef DOT_PAGE_H
#define DOT_PAGE_H

#include "dot_defs.h"
#include "dot_item.h"
#include "uvector.h"

// 档位选择项描述（AddSelector 用）：
// 右侧显示 names[当前档位]，左右键 ±1 档；wrap 为 true 时首尾循环。
// 需要双语档位名时，应用层在语言切换回调里给 item->valueNames 换表即可。
struct DotSelectorDesc {
    const char* const* names;        // 档位显示名表（长度须 ≥ count）
    int         count;               // 档位总数
    int         initial = 0;         // 初始档位
    bool        wrap    = true;      // 首尾循环（档位控件通常循环）
    DotCallback callback = nullptr;  // 切档回调（由 adjustItem 触发）
};

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

    // ---- 档位选择项：VALUE 控件快捷封装（右侧自定义内容 + 可循环）----
    // 一次完成：量程 0~count-1、固定步长 1、档位名表、初始档、
    // 循环开关、常驻编辑态（左右键直接切档）与切档回调
    DotItem* AddSelector(const char* description, const DotSelectorDesc& sel);

    Mode        GetMode() const { return mode_; }
    const char* Name()    const { return name_ ? name_ : ""; }
    int         Index()   const { return index_; }

    int       ItemCount() const { return (int)items_.size(); }
    DotItem* Item(int i) const {
        if (i < 0 || i >= (int)items_.size()) return nullptr;
        return items_[i];
    }
    int IndexOf(const DotItem* it) const;

    // ---- 导航状态（DotUI 读写）----
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
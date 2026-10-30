// mono_page.h —— 页面与页面池
//
// MonoUI 采用"页面池"而非页面树：所有页面在 InitLayout() 里静态注册，
// 运行期只做切换。页面上限 kMaxPages，超出后 AddPage 返回 nullptr。
// 层级关系通过 ITEM_CALL_PAGE 的 backPage 单向记录（子页记住父页）。
#ifndef MONO_PAGE_H
#define MONO_PAGE_H

#include "mono_defs.h"
#include "mono_item.h"
#include "uvector.h"

class MonoPage {
public:
    // 页面模式：决定 renderPage() 的分派方式
    enum Mode : uint8_t {
        LIST   = 0,   // 垂直列表（含开关/数值/进度条等控件）
        ICON   = 1,   // 图标横向轮播
        CUSTOM = 2,   // 完全交给 drawCustom 绘制
    };

    static constexpr int kMaxPages = 16;   // 页面池容量

    // ---- 页面池管理（静态接口）----
    static MonoPage* AddPage(Mode mode, const char* name);  // 注册新页面
    static void      ResetPages();                          // 清空并回收所有页面
    static int       PageCount();                           // 当前页面数
    static MonoPage* GetPage(int index);                    // 按索引取页面（越界返回 nullptr）

    // ---- 添加列表项 ----
    // param1/param2 接受指针、整数、布尔，内部做类型擦除（见 monoToUptr）
    template <typename P1, typename P2>
    MonoItem* AddItem(ItemType       type,
                      const char*    description,
                      const uint8_t* iconSrc,
                      P1             param1,
                      P2             param2,
                      MonoCallback   callback) {
        return AddItemRaw(type, description, iconSrc,
                          monoToUptr(param1), monoToUptr(param2), callback);
    }

    MonoItem* AddItemRaw(ItemType       type,
                         const char*    description,
                         const uint8_t* iconSrc,
                         uintptr_t      param1,
                         uintptr_t      param2,
                         MonoCallback   callback);

    // ---- 属性 ----
    Mode        GetMode() const { return mode_; }
    const char* Name()    const { return name_ ? name_ : ""; }
    int         Index()   const { return index_; }

    // ---- 列表项访问 ----
    int       ItemCount() const { return (int)items_.size(); }
    MonoItem* Item(int i) const {
        if (i < 0 || i >= (int)items_.size()) return nullptr;
        return items_[i];
    }
    int IndexOf(const MonoItem* it) const;   // 项索引，未找到返回 -1

    // ---- 导航状态（MonoUI 读写）----
    float savedFocus = 0.0f;   // 离开页面时的焦点位置（返回时恢复）
    int   backPage   = -1;     // 父页面索引（Back 时返回目标，-1 表示回主页）

    // ---- 自定义绘制 ----
    typedef void (*DrawFn)(MonoUI& ui, MonoPage& page);
    DrawFn drawCustom = nullptr;   // CUSTOM 模式的绘制函数

private:
    MonoPage() {}
    ~MonoPage() {}

    MonoPage(const MonoPage&) = delete;
    MonoPage& operator=(const MonoPage&) = delete;

    Mode               mode_  = LIST;
    const char*        name_  = "";
    int                index_ = -1;
    uvector<MonoItem*> items_;             // 列表项指针数组（堆分配，Reset 时回收）

    static MonoPage s_pool_[kMaxPages];   // 页面静态池
    static int      s_count_;             // 已注册页面数
};

#endif // MONO_PAGE_H

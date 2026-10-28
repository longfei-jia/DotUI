// mono_user_app.cpp —— 用户应用层
//
// 对应 3rdParty/mono_ui/mono_user_app.cpp（截图里打开的就是这个文件）。
//
// 这个文件是"框架"和"应用"的分界：
//   MonoUI::InitLayout / HomeScreen / SaveConfig 三个函数在 mono_ui.h 里只有
//   声明，实现必须落在这里。少了它，链接阶段就会报 undefined reference ——
//   这正是作者用来强制分层的机制。
//
// 下面 InitLayout 的内容与截图逐行对应（截图里的 mode:/description:/param1:
// 是 Eclipse CDT 的参数名提示，不是代码语法）。
#include "mono_ui.h"
#include "mono_page.h"
#include "mono_item.h"
#include "mono_widget.h"
#include "resources.h"

#include <string.h>

// ---------------------------------------------------------------------------
// 测试回调（对应截图 Outline 里的四个函数）
// ---------------------------------------------------------------------------
static void TestWidgetCallback(MonoUI* ui, void* arg);
static void TestTextEditor(MonoUI* ui, void* arg);
static void TestTextReader(MonoUI* ui, void* arg);
static void TestMsgBox(MonoUI* ui, void* arg);

// ===========================================================================
// 布局
// ===========================================================================
void MonoUI::InitLayout()
{
    auto pageMain = MonoPage::AddPage(MonoPage::LIST, "MonoUI");
    auto pageShow = MonoPage::AddPage(MonoPage::LIST, "Show Widgets");
    auto pageIcon = MonoPage::AddPage(MonoPage::ICON, "Icon Mode");

    pageMain->AddItem(ItemType::ITEM_CALL_PAGE,     "Show Widgets", nullptr, pageShow, 0, nullptr);
    pageMain->AddItem(ItemType::ITEM_CALL_PAGE,     "Icon Mode",    nullptr, pageIcon, 0, nullptr);
    pageMain->AddItem(ItemType::ITEM_CALL_FUNCTION, "Text Editor",  nullptr, 0, 0, TestTextEditor);
    pageMain->AddItem(ItemType::ITEM_CALL_FUNCTION, "Text Reader",  nullptr, 0, 0, TestTextReader);

    pageShow->AddItem(ItemType::ITEM_SWITCH,        "Switch",       nullptr, 0, 0, nullptr);
    pageShow->AddItem(ItemType::ITEM_CALL_FUNCTION, "Alert",        nullptr, 0, 0, TestMsgBox);
    pageShow->AddItem(ItemType::ITEM_PROGRESS_BAR,  "Progress Bar", nullptr, 0, 0, TestWidgetCallback);
    pageShow->AddItem(ItemType::ITEM_CHECKBOX,      "Checkbox1",    nullptr, true,  false,   nullptr);
    pageShow->AddItem(ItemType::ITEM_CHECKBOX,      "Checkbox2",    nullptr, false, nullptr, nullptr);
    pageShow->AddItem(ItemType::ITEM_CHECKBOX,      "Checkbox3",    nullptr, false, nullptr, nullptr);
    pageShow->AddItem(ItemType::ITEM_CHECKBOX,      "Checkbox4",    nullptr, false, nullptr, nullptr);
    pageShow->AddItem(ItemType::ITEM_CHECKBOX,      "Checkbox5",    nullptr, true,  nullptr, nullptr);

    // ---- 以下为截图之外补充的内容 ----
    // 截图里 pageIcon 只被创建、没有 AddItem（作者的一贯做法是先搭骨架）。
    // 这里补上 8 个图标项，用的就是 resources.cpp 里从固件 dump 出来的位图，
    // 这样 MonoPage::ICON 模式才有东西可看。
    pageIcon->AddItem(ItemType::ITEM_CALL_FUNCTION, "Chip",     MonoRes::kIcon0, 0, 0, nullptr);
    pageIcon->AddItem(ItemType::ITEM_CALL_FUNCTION, "Globe",    MonoRes::kIcon1, 0, 0, nullptr);
    pageIcon->AddItem(ItemType::ITEM_CALL_FUNCTION, "Connector",MonoRes::kIcon2, 0, 0, nullptr);
    pageIcon->AddItem(ItemType::ITEM_CALL_FUNCTION, "Arrow",    MonoRes::kIcon3, 0, 0, nullptr);
    pageIcon->AddItem(ItemType::ITEM_CALL_FUNCTION, "Path",     MonoRes::kIcon4, 0, 0, nullptr);
    pageIcon->AddItem(ItemType::ITEM_CALL_FUNCTION, "Stair",    MonoRes::kIcon5, 0, 0, nullptr);
    pageIcon->AddItem(ItemType::ITEM_CALL_FUNCTION, "Noise",    MonoRes::kIcon6, 0, 0, nullptr);
    pageIcon->AddItem(ItemType::ITEM_CALL_FUNCTION, "Nav",      MonoRes::kIcon7, 0, 0, nullptr);

    // 另外补两个固件里实证存在的类型，方便对照：
    //   ITEM_TEXT         —— 纯文本行，缩进 3px
    //   ITEM_TEXT_MARQUEE —— 长文本跑马灯，行尾带状态图标
    auto pageExtra = MonoPage::AddPage(MonoPage::LIST, "More");
    pageExtra->AddItem(ItemType::ITEM_TEXT,          "Plain text row", nullptr, 0, 0, nullptr);
    pageExtra->AddItem(ItemType::ITEM_VALUE,         "Threshold",    nullptr, 0, 100, nullptr);
    pageExtra->AddItem(ItemType::ITEM_TEXT_MARQUEE,  "A very long description that will scroll", nullptr, 0, 0, nullptr);
    pageExtra->AddItem(ItemType::ITEM_CALL_FUNCTION, "Save Config",  nullptr, 0, 0, nullptr);

    // 把 More 挂到主页面末尾
    pageMain->AddItem(ItemType::ITEM_CALL_PAGE, "More", nullptr, pageExtra, 0, nullptr);

    // 给跑马灯项挂状态图标（10x10 裸位图）
    if (MonoItem* mi = pageExtra->Item(2)) mi->statusIcon = MonoRes::kStatusBox;
    if (MonoItem* th = pageExtra->Item(1)) { th->value = 42.0f; th->SetRange(0.0f, 100.0f); }
}

// ===========================================================================
// 页面钩子
// ===========================================================================

// 返回主界面。根页按 Back 时由 MonoUI::Back() 调用。
void MonoUI::HomeScreen(void* arg)
{
    (void)arg;
    GotoPage(0, 0);
}

// 保存配置。真实工程里这里是写 Flash / 文件的地方。
void MonoUI::SaveConfig(void* arg)
{
    (void)arg;
    MonoWidget::Toast(*this, "Saved");
}

// ===========================================================================
// 测试回调
// ===========================================================================

// 进度条：每按一次 +25%，闭环回到 0。
// 覆盖层由 OK/BACK 关闭（见 mono_widget.h 的 KIND_PROGRESS 分支）。
static void TestWidgetCallback(MonoUI* ui, void* arg)
{
    (void)arg;
    static int s_progress = 0;

    s_progress += 25;
    if (s_progress > 100) s_progress = 0;

    MonoWidget::ProgressBar(*ui, "Progress", s_progress);
}

// 文本编辑：直接在静态缓冲上改，UP/DOWN 换字符，LEFT/RIGHT 移光标。
static void TestTextEditor(MonoUI* ui, void* arg)
{
    (void)arg;
    static char s_buf[32] = "Hello";
    MonoWidget::TextEditor(*ui, s_buf, sizeof(s_buf), "Text Editor");
}

// 文本阅读：UP/DOWN 滚动
static void TestTextReader(MonoUI* ui, void* arg)
{
    (void)arg;
    static const char kText[] =
        "MonoUI is a tiny GUI\n"
        "framework for 1-bit\n"
        "memory LCD panels.\n"
        "It uses u8g2 as the\n"
        "drawing backend and\n"
        "adds pages, items,\n"
        "spring animation and\n"
        "modal widgets on top.\n"
        "Press BACK to close.";
    MonoWidget::TextReader(*ui, "Reader", kText);
}

// 模态提示框
static void TestMsgBox(MonoUI* ui, void* arg)
{
    (void)arg;
    MonoWidget::MsgBox(*ui, "Alert", "Hello from MonoUI");
}

// dot_user_app.cpp —— 应用层：随机测试菜单 + 中英文切换
//
// 实现 DotUI 的三个应用钩子：
//   InitLayout —— 注册页面与列表项（本例随机生成，方便压力测试）
//   HomeScreen —— 返回主页
//   SaveConfig —— 保存配置（此处只弹 Toast）
//
// 多语言方案：所有菜单文本在创建时通过 reg() 登记中英文对，
// 切换语言时 applyLanguage() 统一改写 item 的文本指针。
#include "dot_ui.h"
#include "dot_page.h"
#include "dot_item.h"
#include "dot_widget.h"
#include "resources.h"
#include "dot_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

namespace {

// ===========================================================================
// 语言与文案
// ===========================================================================
enum Lang { LANG_EN = 0, LANG_CN = 1 };
Lang g_lang = LANG_CN;   // 默认中文

struct Bilingual {
    const char* en;
    const char* cn;
};

// 随机项使用的普通标签
const Bilingual kLabels[] = {
    {"Brightness", "亮度"},     {"Contrast", "对比度"},
    {"Wi-Fi", "Wi-Fi"},         {"Bluetooth", "蓝牙"},
    {"Sleep Mode", "睡眠模式"}, {"Auto Refresh", "自动刷新"},
    {"Deep Sleep", "深度睡眠"}, {"Backlight", "背光"},
    {"Volume", "音量"},         {"Sensor", "传感器"},
    {"Low Power", "低功耗"},    {"Debug", "调试"},
    {"About", "关于"},          {"Reset", "重置"},
    {"Calibrate", "校准"},      {"Update", "更新"},
    {"Time Zone", "时区"},      {"Language", "语言"},
    {"Battery", "电池"},        {"Memory", "内存"},
};
constexpr int kLabelCount = (int)(sizeof(kLabels) / sizeof(kLabels[0]));

// 随机项使用的长文本（测跑马灯）
const Bilingual kLongLabels[] = {
    {"A very long description that will scroll across the screen",
     "一段很长的描述，用来测试跑马灯滚动效果"},
    {"Randomly generated menu item with a long caption",
     "随机生成的菜单项，带有一段较长的说明文字"},
    {"This row demonstrates the marquee animation",
     "这一行用来演示跑马灯动画"},
};
constexpr int kLongCount = (int)(sizeof(kLongLabels) / sizeof(kLongLabels[0]));
// 图标页 8 个图标的名称（全部使用 64x64 默认方块图标，无需美术资源）
const Bilingual kIconNames[8] = {
    {"Settings", "设置"},     {"Controls", "控件"},   {"Tests", "测试"},
    {"Demo 1", "演示 1"},      {"Demo 2", "演示 2"},   {"Demo 3", "演示 3"},
    {"Demo 4", "演示 4"},      {"Demo 5", "演示 5"},
};

// 每个测试子页的条目
constexpr int kTestsPerPage = 16;

const char* const kTestEn[kTestsPerPage] = {
    "Test 1",  "Test 2",  "Test 3",  "Test 4",
    "Test 5",  "Test 6",  "Test 7",  "Test 8",
    "Test 9",  "Test 10", "Test 11", "Test 12",
    "Test 13", "Test 14", "Test 15", "Test 16",
};
const char* const kTestCn[kTestsPerPage] = {
    "测试 1",  "测试 2",  "测试 3",  "测试 4",
    "测试 5",  "测试 6",  "测试 7",  "测试 8",
    "测试 9",  "测试 10", "测试 11", "测试 12",
    "测试 13", "测试 14", "测试 15", "测试 16",
};

// ---- 文本登记表：语言切换时统一刷新 ----
struct TextEntry {
    DotItem*   item;
    const char* en;
    const char* cn;
    // 档位名表（可选，供 ITEM_VALUE 的 valueNames 双语切换）
    const char* const* namesEn;
    const char* const* namesCn;
};

constexpr int kMaxTexts = 320;
TextEntry g_texts[kMaxTexts];
int       g_textCount = 0;

// 登记 item 的中英文文本（档位名表默认空）
void reg(DotItem* item, const char* en, const char* cn,
         const char* const* namesEn = nullptr,
         const char* const* namesCn = nullptr) {
    if (!item || g_textCount >= kMaxTexts) return;
    g_texts[g_textCount].item    = item;
    g_texts[g_textCount].en      = en;
    g_texts[g_textCount].cn      = cn;
    g_texts[g_textCount].namesEn = namesEn;
    g_texts[g_textCount].namesCn = namesCn;
    ++g_textCount;
}

// 按当前语言取文案
const char* tr(const char* en, const char* cn) {
    return (g_lang == LANG_CN) ? cn : en;
}

// 应用语言：刷新所有登记文本、系统文案与字体
void applyLanguage(DotUI* ui) {
    const bool cn = (g_lang == LANG_CN);

    for (int i = 0; i < g_textCount; ++i) {
        if (g_texts[i].item) {
            g_texts[i].item->SetText(cn ? g_texts[i].cn : g_texts[i].en);
            // 档位名表跟随语言（未登记名表的项保持不变）
            if (g_texts[i].namesEn)
                g_texts[i].item->valueNames =
                    cn ? g_texts[i].namesCn : g_texts[i].namesEn;
        }
    }

    // ON/OFF 与 OK 按钮跟随语言
    DotRes::kLanguage = cn ? 1 : 0;
    DotRes::kTextOn   = cn ? "开" : "ON";
    DotRes::kTextOff  = cn ? "关" : "OFF";
    DotRes::kTextOk   = cn ? "确定" : "OK";

    // 中文需要汉字字体；英文恢复 6x12 + 大字
    if (ui) {
        if (cn)
            ui->SetFonts(DotRes::kFontChinese, DotRes::kFontChinese);
        else
            ui->SetFonts(DotRes::kFontSmall, DotRes::kFontBig);
    }

    printf("[DotUI] language = %s, texts = %d\n", cn ? "CN" : "EN", g_textCount);
    fflush(stdout);
}

// ===========================================================================
// 回调
// ===========================================================================
void onMsgBox(DotUI* ui, void*) {
    DotWidget::MsgBox(*ui, tr("Alert", "提示"), tr("Random menu", "随机菜单"));
}

void onToast(DotUI* ui, void*) {
    DotWidget::Toast(*ui, tr("Saved", "已保存"));
}

void onProgress(DotUI* ui, void*) {
    static int progress = 0;
    progress = (progress + 20) % 120;   // 循环递增，观察进度框
    DotWidget::ProgressBar(*ui, tr("Progress", "进度"), progress);
}

void onSaveConfig(DotUI* ui, void*) {
    ui->SaveConfig(nullptr);
}

void onDarkMode(DotUI* ui, void*) {
    DotItem* it = ui->CurrentItem();
    dot_set_dark_mode(it && it->IsOn());
}

// ---- 语言：VALUE 档位（0=中文 1=英文），左右键切档即刻生效 ----
void onLanguage(DotUI* ui, void*) {
    DotItem* it = ui ? ui->CurrentItem() : nullptr;
    if (!it) return;
    g_lang = ((int)(it->value + 0.5f) == 0) ? LANG_CN : LANG_EN;
    applyLanguage(ui);
}

// 测试子页条目：弹窗回显当前项名称
void onTest(DotUI* ui, void*) {
    DotItem* it = ui->CurrentItem();
    DotWidget::MsgBox(*ui, tr("Test", "测试"), it ? it->GetText() : "");
}

// 档位显示名表（drawRow 按整数值直接索引；随语言切换的表走 reg 双语机制）
const char* const kRotNames[4]     = { "0", "90", "180", "270" };   // 旋转角度（无语言差异）
const char* const kDampNamesEn[3]  = { "Under", "Crit", "Over" };   // 阻尼模式（英文）
const char* const kDampNamesCn[3]  = { "欠阻尼", "临界", "过阻尼" };  // 阻尼模式（中文）
const char* const kLangNamesEn[2]  = { "ZH", "EN" };                // 语言档位（英文界面）
const char* const kLangNamesCn[2]  = { "中", "英" };                // 语言档位（中文界面）

// ---- 屏幕方向：VALUE 档位（右侧显示 0/90/180/270），左右键切档，即刻生效 ----
void onRotationAdjust(DotUI* ui, void*) {
    // 回调由 adjustItem 触发，此时当前项就是触发源，无需经 arg 传递
    DotItem* it = ui ? ui->CurrentItem() : nullptr;
    if (!it) return;
    static const u8g2_cb_t* const kCbs[4] = {
        U8G2_R0, U8G2_R1, U8G2_R2, U8G2_R3 };
    ui->RotateTo(kCbs[(int)(it->value + 0.5f)]);   // 顺时针 90° 步进
}

// ---- 阻尼调节：VALUE 档位（右侧显示模式名），左右键切档 ----
void onDampingAdjust(DotUI* ui, void*) {
    DotItem* it = ui ? ui->CurrentItem() : nullptr;
    if (!it) return;
    static const float kZetas[3] = { 0.5f, 1.0f, 1.6f }; // 欠/临界/过
    ui->SetSpringZeta(kZetas[(int)(it->value + 0.5f)]);
}

// ===========================================================================
// 页面构建
// ===========================================================================
// 随机往页面里塞一个控件，覆盖全部 6 种非跳转类型
void addRandomItem(DotPage* page) {
    const Bilingual& l = kLabels[rand() % kLabelCount];
    DotItem* it = nullptr;

    switch (rand() % 6) {
    case 0:   // 纯文本
        it = page->AddItem(ItemType::ITEM_TEXT, l.en, nullptr, 0, 0, nullptr);
        break;
    case 1:   // 开关
        it = page->AddItem(ItemType::ITEM_SWITCH, l.en, nullptr, rand() & 1, 0, nullptr);
        break;
    case 2:   // 勾选框
        it = page->AddItem(ItemType::ITEM_CHECKBOX, l.en, nullptr, rand() & 1, 0, nullptr);
        break;
    case 3: { // 数值
        it = page->AddItem(ItemType::ITEM_VALUE, l.en, nullptr, 0, 100, nullptr);
        it->SetValue((float)(rand() % 101));
        break;
    }
    case 4:   // 进度条
        it = page->AddItem(ItemType::ITEM_PROGRESS_BAR, l.en, nullptr, rand() % 101, 0, nullptr);
        break;
    default: { // 跑马灯长文本
        const Bilingual& m = kLongLabels[rand() % kLongCount];
        it = page->AddItem(ItemType::ITEM_TEXT_MARQUEE, m.en, nullptr, 0, 0, nullptr);
        reg(it, m.en, m.cn);
        return;
    }
    }

    reg(it, l.en, l.cn);
}

// 测试子页：Test 1..Test 16
void addTestItems(DotPage* page) {
    for (int i = 0; i < kTestsPerPage; ++i) {
        DotItem* it = page->AddItem(ItemType::ITEM_CALL_FUNCTION,
                                     kTestEn[i], nullptr, 0, 0, onTest);
        reg(it, kTestEn[i], kTestCn[i]);
    }
}

// 图标页：全部使用 64x64 默认方块（iconSrc = nullptr，渲染时兑底）。
// 前 3 个挂真实子页；后 5 个无子页，按下弹窗回显名称。
// 若某个图标想用美术资源，把第 3 参 nullptr 换成 DotRes::kIcons[i] 即可。
void addIconItems(DotPage* iconPage,
                  DotPage* settings, DotPage* ctrl, DotPage* tests) {
    struct Entry {
        const char* en;       // 英文名（中文名从 kIconNames 查）
        DotPage*    target;   // 非 null = 跳转子页；null = 弹窗展示项
    };
    const Entry entries[8] = {
        {kIconNames[0].en, settings}, {kIconNames[1].en, ctrl},
        {kIconNames[2].en, tests},
        {kIconNames[3].en, nullptr}, {kIconNames[4].en, nullptr},
        {kIconNames[5].en, nullptr}, {kIconNames[6].en, nullptr},
        {kIconNames[7].en, nullptr},
    };

    for (int i = 0; i < 8; ++i) {
        DotItem* it = iconPage->AddItem(
            entries[i].target ? ItemType::ITEM_CALL_PAGE
                              : ItemType::ITEM_CALL_FUNCTION,
            entries[i].en,
            nullptr,              // iconSrc = nullptr → 64x64 默认方块
            entries[i].target ? reinterpret_cast<uintptr_t>(entries[i].target) : 0,
            0,
            entries[i].target ? nullptr : onTest);
        reg(it, entries[i].en, kIconNames[i].cn);
    }
}

} // namespace

// ===========================================================================
// 应用钩子
// ===========================================================================

// 注册全部页面。主页为图标模式（默认显示），下设三个功能子页：
//   设置（深色/语言/保存）→ UI 基础功能；
//   控件（全部 8 种 ItemType 一网打尽）；
//   测试（16 个测试项，测分页滚动）。
void DotUI::InitLayout() {
    const unsigned seed = (unsigned)time(nullptr);
    srand(seed);
    printf("[DotUI] random menu seed = %u\n", seed);
    fflush(stdout);

    g_textCount = 0;   // 重新布局时重建登记表

    // ---- 页面池：主页(ICON) + 设置 + 控件 + 测试 + 5 个 Test 子页 ----
    DotPage* home     = DotPage::AddPage(DotPage::ICON, "Home");
    DotPage* settings = DotPage::AddPage(DotPage::LIST, "Settings");
    DotPage* ctrl     = DotPage::AddPage(DotPage::LIST, "Controls");
    DotPage* tests    = DotPage::AddPage(DotPage::LIST, "Tests");

    // ---- 图标主页：默认显示模式，8 个 64x64 默认方块 ----
    addIconItems(home, settings, ctrl, tests);

    // ---- 设置页：UI 基础功能 ----
    reg(settings->AddItem(ItemType::ITEM_SWITCH, "Dark Mode", nullptr,
                          false, 0, onDarkMode),
        "Dark Mode", "深色模式");
    {
        // 语言档位：0=中文(ZH) 1=英文(EN)，右侧显示中/英 或 ZH/EN
        DotItem* it = settings->AddItem(ItemType::ITEM_VALUE, "Language",
                                         nullptr, 0, 1, onLanguage);
        it->step = 1.0f;
        it->valueNames = kLangNamesCn;  // 初始中文界面，随语言切换刷新
        it->SetValue(0);                // 默认中文
        it->SetEditing(true);           // 常驻编辑态：左右键直接切档
        reg(it, "Language", "语言", kLangNamesEn, kLangNamesCn);
    }
    reg(settings->AddItem(ItemType::ITEM_CALL_FUNCTION, "Save Config", nullptr,
                          0, 0, onSaveConfig),
        "Save Config", "保存配置");

    // ---- 屏幕方向 + 阻尼调节：VALUE 档位，右侧显示档位，左右键切档，即刻生效 ----
    {
        DotItem* it = settings->AddItem(ItemType::ITEM_VALUE, "Rotation",
                                         nullptr, 0, 3, onRotationAdjust);
        it->step = 1.0f;             // 每次左右键 ±1 档（顺时针 90°）
        it->valueNames = kRotNames;  // 右侧显示 0/90/180/270（角度值）
        it->wrap = true;             // 270° 后继续正向回到 0°，循环切换
        it->SetValue(0);             // 默认 0°
        it->SetEditing(true);        // 常驻编辑态：左右键直接切档
        reg(it, "Rotation", "屏幕方向");
    }
    {
        DotItem* it = settings->AddItem(ItemType::ITEM_VALUE, "Damping",
                                         nullptr, 0, 2, onDampingAdjust);
        it->step = 1.0f;
        it->valueNames = kDampNamesCn; // 右侧显示模式名（初始中文，随语言切换）
        it->wrap = true;               // 欠阻尼→临界→过阻尼→欠阻尼 循环
        it->SetValue(1);               // 默认临界阻尼
        it->SetEditing(true);
        reg(it, "Damping", "阻尼调节", kDampNamesEn, kDampNamesCn);
    }

    // ---- 控件页：已有全部控件类型各一 ----
    reg(ctrl->AddItem(ItemType::ITEM_TEXT, "Plain Text", nullptr, 0, 0, nullptr),
        "Plain Text", "纯文本");
    reg(ctrl->AddItem(ItemType::ITEM_SWITCH, "Switch", nullptr,
                      false, 0, nullptr),
        "Switch", "开关");
    reg(ctrl->AddItem(ItemType::ITEM_CHECKBOX, "Checkbox", nullptr,
                      true, 0, nullptr),
        "Checkbox", "勾选框");
    {
        DotItem* it = ctrl->AddItem(ItemType::ITEM_VALUE, "Value",
                                     nullptr, 0, 100, nullptr);
        it->SetValue(42.0f);
        reg(it, "Value", "数值");
    }
    reg(ctrl->AddItem(ItemType::ITEM_PROGRESS_BAR, "Progress", nullptr,
                      60, 0, nullptr),
        "Progress", "进度条");
    reg(ctrl->AddItem(ItemType::ITEM_CALL_FUNCTION, "MsgBox", nullptr,
                      0, 0, onMsgBox),
        "MsgBox", "消息框");
    reg(ctrl->AddItem(ItemType::ITEM_CALL_FUNCTION, "Toast", nullptr,
                      0, 0, onToast),
        "Toast", "提示气泡");
    reg(ctrl->AddItem(ItemType::ITEM_CALL_FUNCTION, "ProgressBar", nullptr,
                      0, 0, onProgress),
        "ProgressBar", "进度框");
    reg(ctrl->AddItem(ItemType::ITEM_TEXT_MARQUEE,
                      "A very long text to trigger marquee scrolling",
                      nullptr, 0, 0, nullptr),
        "A very long text to trigger marquee scrolling",
        "一段很长的文本，用来触发跑马灯滚动");

    // ---- 测试页：Test 1..16，超过 8 行可视区，逼出分页滚动 ----
    addTestItems(tests);

    applyLanguage(this);
}

void DotUI::HomeScreen(void* arg) {
    (void)arg;
    GotoPage(0, 0);
}

void DotUI::SaveConfig(void* arg) {
    (void)arg;
    DotWidget::Toast(*this, tr("Saved", "已保存"));
}

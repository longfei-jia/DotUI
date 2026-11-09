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

// 图标页 8 个图标的名称
const Bilingual kIconNames[8] = {
    {"Chip", "芯片"},       {"Globe", "地球"},   {"Connector", "连接器"},
    {"Arrow", "箭头"},      {"Path", "路径"},    {"Stair", "阶梯"},
    {"Noise", "噪声"},      {"Nav", "导航"},
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
};

constexpr int kMaxTexts = 320;
TextEntry g_texts[kMaxTexts];
int       g_textCount = 0;

// 登记 item 的中英文文本
void reg(DotItem* item, const char* en, const char* cn) {
    if (!item || g_textCount >= kMaxTexts) return;
    g_texts[g_textCount].item = item;
    g_texts[g_textCount].en   = en;
    g_texts[g_textCount].cn   = cn;
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
        if (g_texts[i].item)
            g_texts[i].item->SetText(cn ? g_texts[i].cn : g_texts[i].en);
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

void onLanguage(DotUI* ui, void*) {
    DotItem* it = ui->CurrentItem();
    g_lang = (it && it->IsOn()) ? LANG_CN : LANG_EN;
    applyLanguage(ui);
}

// 测试子页条目：弹窗回显当前项名称
void onTest(DotUI* ui, void*) {
    DotItem* it = ui->CurrentItem();
    DotWidget::MsgBox(*ui, tr("Test", "测试"), it ? it->GetText() : "");
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

// 图标页：每个图标进入各自的 Test 子页面
void addIconItems(DotPage* iconPage) {
    for (int i = 0; i < DotRes::kIconCount; ++i) {
        DotPage* tests = DotPage::AddPage(DotPage::LIST, "Tests");
        if (!tests) break;

        addTestItems(tests);

        DotItem* it = iconPage->AddItem(ItemType::ITEM_CALL_PAGE,
                                         kIconNames[i].en,
                                         DotRes::kIcons[i],
                                         tests, 0, nullptr);
        reg(it, kIconNames[i].en, kIconNames[i].cn);
    }
}

} // namespace

// ===========================================================================
// 应用钩子
// ===========================================================================

// 注册全部页面。条目数刻意超过可视行数，用于测试分页滚动：
//   Home 13 项 / Settings 17 项 / More 17 项 / Test 页 16 项
void DotUI::InitLayout() {
    const unsigned seed = (unsigned)time(nullptr);
    srand(seed);
    printf("[DotUI] random menu seed = %u\n", seed);
    fflush(stdout);

    g_textCount = 0;   // 重新布局时重建登记表

    DotPage* home     = DotPage::AddPage(DotPage::LIST, "Random Menu");
    DotPage* settings = DotPage::AddPage(DotPage::LIST, "Settings");
    DotPage* icons    = DotPage::AddPage(DotPage::ICON, "Icons");
    DotPage* more     = DotPage::AddPage(DotPage::LIST, "More");

    // ---- 主页：3 个入口 + 10 个随机项 ----
    reg(home->AddItem(ItemType::ITEM_CALL_PAGE, "Settings", nullptr,
                      settings, 0, nullptr),
        "Settings", "设置");
    reg(home->AddItem(ItemType::ITEM_CALL_PAGE, "Icons", nullptr,
                      icons, 0, nullptr),
        "Icons", "图标");
    reg(home->AddItem(ItemType::ITEM_CALL_PAGE, "More", nullptr,
                      more, 0, nullptr),
        "More", "更多");

    for (int i = 0; i < 10; ++i) addRandomItem(home);

    // ---- 设置页：深色模式 + 语言（固定前两项）+ 14 个随机项 + 保存 ----
    reg(settings->AddItem(ItemType::ITEM_SWITCH, "Dark Mode", nullptr,
                          false, 0, onDarkMode),
        "Dark Mode", "深色模式");
    reg(settings->AddItem(ItemType::ITEM_SWITCH, "Language", nullptr,
                          true, 0, onLanguage),
        "Language", "语言");

    for (int i = 0; i < 14; ++i) addRandomItem(settings);

    reg(settings->AddItem(ItemType::ITEM_CALL_FUNCTION, "Save Config", nullptr,
                          0, 0, onSaveConfig),
        "Save Config", "保存配置");

    // ---- 更多页：固定测试项 + 12 个随机项 ----
    reg(more->AddItem(ItemType::ITEM_CALL_FUNCTION, "Show Alert", nullptr,
                      0, 0, onMsgBox),
        "Show Alert", "显示提示");
    reg(more->AddItem(ItemType::ITEM_CALL_FUNCTION, "Toast", nullptr,
                      0, 0, onToast),
        "Toast", "提示气泡");
    reg(more->AddItem(ItemType::ITEM_CALL_FUNCTION, "Progress", nullptr,
                      0, 0, onProgress),
        "Progress", "进度条");

    {   // 数值项示例
        DotItem* it = more->AddItem(ItemType::ITEM_VALUE, "Threshold",
                                     nullptr, 0, 100, nullptr);
        it->SetValue(42.0f);
        reg(it, "Threshold", "阈值");
    }

    {   // 跑马灯长文本示例
        const char* en = "MonoUI ported to DotUI simulator";
        const char* cn = "MonoUI 已移植到 DotUI 模拟器";
        DotItem* it = more->AddItem(ItemType::ITEM_TEXT_MARQUEE, en,
                                     nullptr, 0, 0, nullptr);
        reg(it, en, cn);
    }

    for (int i = 0; i < 12; ++i) addRandomItem(more);

    // ---- 图标页：8 个图标，各自进入 Test 子页 ----
    addIconItems(icons);

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

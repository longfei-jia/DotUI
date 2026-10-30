// mono_item.cpp —— MonoItem 的按下行为
//
// 从 mono_ui.cpp 拆出：控件"被按下后做什么"属于列表项自身的行为，
// 放在 item 自己的实现文件里，结构更清晰。
#include "mono_item.h"
#include "mono_ui.h"

MonoPage* MonoItem::TargetPage() const {
    return monoFromUptr<MonoPage*>(param1);
}

// 按下处理：根据控件类型分派动作
bool MonoItem::Activate(MonoUI* ui) {
    if (!ui) return false;

    switch (type) {
    // 进入子页面：param2 > 0 时同时定位到目标页的第 param2 项
    case ItemType::ITEM_CALL_PAGE: {
        MonoPage* tp = TargetPage();
        if (tp) {
            const int t = (int)(intptr_t)param2;
            ui->GotoPage(tp, t > 0 ? t : -1);
        }
        return true;
    }

    // 动作项 / 跑马灯项：直接触发回调
    case ItemType::ITEM_CALL_FUNCTION:
    case ItemType::ITEM_TEXT_MARQUEE:
        if (callback) callback(ui, Context());
        return true;

    // 开关 / 勾选框：翻转状态并通知
    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        Toggle();
        if (callback) callback(ui, Context());
        return true;

    // 数值项：OK 进入/退出编辑（编辑中用左右键调值）
    case ItemType::ITEM_VALUE:
        SetEditing(!IsEditing());
        return true;

    // 进度条：按下无动作（左右键直接调值）
    case ItemType::ITEM_PROGRESS_BAR:
        return true;

    default:
        return false;
    }
}

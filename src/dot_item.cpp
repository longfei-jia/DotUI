// dot_item.cpp —— DotItem 的按下行为
#include "dot_item.h"
#include "dot_ui.h"

DotPage* DotItem::TargetPage() const {
    return dotFromUptr<DotPage*>(param1);
}

// 按下处理：根据控件类型分派动作
bool DotItem::Activate(DotUI* ui) {
    if (!ui) return false;

    switch (type) {
    case ItemType::ITEM_CALL_PAGE: {
        DotPage* tp = TargetPage();
        if (tp) {
            const int t = (int)(intptr_t)param2;   // >0 时定位到目标页第 t 项
            ui->GotoPage(tp, t > 0 ? t : -1);
        }
        return true;
    }
    case ItemType::ITEM_CALL_FUNCTION:
    case ItemType::ITEM_TEXT_MARQUEE:
        if (callback) callback(ui, Context());
        return true;

    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        Toggle();
        if (callback) callback(ui, Context());
        return true;

    case ItemType::ITEM_VALUE:
        SetEditing(!IsEditing());   // OK 进入/退出编辑
        return true;

    case ItemType::ITEM_PROGRESS_BAR:
        return true;                // 按下无动作（左右键直接调值）

    default:
        return false;
    }
}
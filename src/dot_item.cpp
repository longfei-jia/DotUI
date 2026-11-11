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

    // 可编辑类型（VALUE/SWITCH/CHECKBOX/PROGRESS）不在这里处理：
    // DotUI::Press 统一拦截进入编辑态（方案 A 双态分派），
    // 上下键调值、OK 提交/双击取消全部由框架编辑态接管

    case ItemType::ITEM_PROGRESS_BAR:
        return true;                // 按下无动作（左右键直接调值）

    default:
        return false;
    }
}
// mono_widget.h —— 覆盖层控件（MsgBox / Toast / ProgressBar）
//
// 覆盖层压在页面之上：MsgBox/ProgressBar 为模态（吃掉按键），
// Toast 自动超时消失。控件从静态池分配（容量 4），用完即还。
// TextEditor / TextReader 可按同样方式补回。
#ifndef MONO_WIDGET_H
#define MONO_WIDGET_H

#include "mono_defs.h"
#include "mono_ui.h"
#include "resources.h"

#include <stdio.h>

class MonoWidget {
public:
    enum Kind : uint8_t {
        KIND_NONE     = 0,
        KIND_MSG      = 1,   // 消息框：标题 + 单行正文 + OK 按钮
        KIND_TOAST    = 2,   // 气泡：底部居中，超时消失
        KIND_PROGRESS = 3,   // 进度：标题 + 进度条 + 百分比
    };

    static constexpr int kPoolSize = 4;

    // ---- 静态池 ----
    static MonoWidget* pool() {
        static MonoWidget s[kPoolSize];
        return s;
    }

    static MonoWidget* Alloc() {
        MonoWidget* p = pool();
        for (int i = 0; i < kPoolSize; ++i) {
            if (!p[i].used_) {
                p[i].reset();
                p[i].used_ = true;
                return &p[i];
            }
        }
        return nullptr;   // 池耗尽
    }

    static void Free(MonoWidget* w) {
        if (w) w->used_ = false;
    }

    // ---- 工厂：弹出各类控件 ----

    // 消息框；onClose 在用户按 OK/BACK 关闭时回调
    static void MsgBox(MonoUI& ui, const char* title, const char* text,
                       MonoCallback onClose = nullptr) {
        MonoWidget* w = Alloc();
        if (!w) return;
        w->kind_    = KIND_MSG;
        w->title_   = title;
        w->text_    = text;
        w->onClose_ = onClose;
        ui.PushOverlay(w);
    }

    // 气泡提示，ms 毫秒后自动消失
    static void Toast(MonoUI& ui, const char* text, uint32_t ms = MonoAnim::kToastMs) {
        MonoWidget* w = Alloc();
        if (!w) return;
        w->kind_     = KIND_TOAST;
        w->text_     = text;
        w->expireAt_ = ui.Now() + ms;
        ui.PushOverlay(w);
    }

    // 进度框；返回句柄可用 SetPercent 更新进度
    static MonoWidget* ProgressBar(MonoUI& ui, const char* title, int percent = 0) {
        MonoWidget* w = Alloc();
        if (!w) return nullptr;
        w->kind_    = KIND_PROGRESS;
        w->title_   = title;
        w->percent_ = monoClampi(percent, 0, 100);
        ui.PushOverlay(w);
        return w;
    }

    // ---- 运行期更新接口 ----
    int  GetPercent() const          { return percent_; }
    void SetPercent(int p)           { percent_ = monoClampi(p, 0, 100); }
    void SetText(const char* t)      { text_ = t; }

    // Toast 超时判断（用差值比较避免毫秒回绕）
    bool IsExpired(uint32_t now) const {
        return kind_ == KIND_TOAST && (int32_t)(now - expireAt_) > 0;
    }

    // 每帧更新：Toast 到期自动弹出
    void Update(MonoUI& ui) {
        if (IsExpired(ui.Now())) ui.PopOverlay();
    }

    // 按键处理。返回 true 表示事件已被吃掉（模态）；
    // Toast 不吃按键，任意键只是提前关掉它。
    bool OnKey(MonoUI& ui, MonoInput::Key k) {
        switch (kind_) {
        case KIND_TOAST:
            ui.PopOverlay();
            return false;

        case KIND_MSG:
            if (k == MonoInput::KEY_OK || k == MonoInput::KEY_BACK) {
                if (onClose_) onClose_(&ui, nullptr);
                ui.PopOverlay();
            }
            return true;

        case KIND_PROGRESS:
            if (k == MonoInput::KEY_BACK || k == MonoInput::KEY_OK)
                ui.PopOverlay();
            return true;

        default:
            return false;
        }
    }

    // 按类型绘制
    void Draw(MonoUI& ui) {
        switch (kind_) {
        case KIND_MSG:      drawMsg(ui);      break;
        case KIND_TOAST:    drawToast(ui);    break;
        case KIND_PROGRESS: drawProgress(ui); break;
        default: break;
        }
    }

private:
    MonoWidget() {}
    MonoWidget(const MonoWidget&) = delete;
    MonoWidget& operator=(const MonoWidget&) = delete;

    void reset() {
        kind_     = KIND_NONE;
        title_    = nullptr;
        text_     = nullptr;
        percent_  = 0;
        expireAt_ = 0;
        onClose_  = nullptr;
    }

    static void setColor(MonoUI& ui, uint8_t c) {
        u8g2_SetDrawColor(ui.G2(), c);
    }

    // 消息框：居中卡片 = 标题 + 分隔线 + 单行正文 + OK 按钮
    // 正文刻意保持单行：1-bit 小屏上多行折行收益低，长文案用 Toast/翻页表达
    void drawMsg(MonoUI& ui) {
        const int w = 108;
        const int h = 52;
        const int x = (MonoLayout::kScreenW - w) / 2;
        const int y = (MonoLayout::kScreenH - h) / 2;

        setColor(ui, ui.Theme().bg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(x, y, w, h, 4);

        if (title_) {
            ui.DrawTextCenter(x + w / 2, y + 14, title_);
            ui.FillBox(x + 5, y + 16, w - 10, 1);
        }

        if (text_) {
            ui.DrawTextCenter(x + w / 2, y + 32, text_);
        }

        // 右下角 OK 按钮
        const int bw = 30, bh = 12;
        const int bx = x + w - bw - 6;
        const int by = y + h - bh - 3;
        setColor(ui, ui.Theme().bg);
        ui.FillRound(bx, by, bw, bh, 3);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(bx, by, bw, bh, 3);
        ui.DrawTextCenter(bx + bw / 2, by + 9, MonoRes::kTextOk);
    }

    // 气泡：宽度随文本自适应，屏幕下 1/4 处居中，实底反色
    void drawToast(MonoUI& ui) {
        if (!text_) return;
        const int tw = ui.TextWidth(text_);
        const int w  = tw + 14;
        const int h  = 20;
        const int x  = (MonoLayout::kScreenW - w) / 2;
        const int y  = MonoLayout::kScreenH * 3 / 4 - h / 2;

        setColor(ui, ui.Theme().fg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().bg);
        ui.DrawText(x + 7, y + 14, text_);
        setColor(ui, ui.Theme().fg);
    }

    // 进度框：标题 + 圆角进度条 + 百分比文本
    void drawProgress(MonoUI& ui) {
        const int w   = 100;
        const int h   = 44;
        const int x   = (MonoLayout::kScreenW - w) / 2;
        const int y   = (MonoLayout::kScreenH - h) / 2;
        const int asc = ui.FontAscent();

        setColor(ui, ui.Theme().bg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(x, y, w, h, 4);

        if (title_) ui.DrawTextCenter(x + w / 2, y + asc + 3, title_);

        const int tx = x + 8, tw = w - 16, ty = y + h - 20, th = 8;
        ui.FrameRound(tx, ty, tw, th, 2);
        const int fill = (tw - 4) * percent_ / 100;
        if (fill > 0) ui.FillRound(tx + 2, ty + 2, fill, th - 4, 1);

        char buf[8];
        snprintf(buf, sizeof buf, "%d%%", percent_);
        ui.DrawTextCenter(x + w / 2, y + h - 4, buf);
    }

    Kind        kind_     = KIND_NONE;
    const char* title_    = nullptr;
    const char* text_     = nullptr;
    int         percent_  = 0;
    uint32_t    expireAt_ = 0;
    MonoCallback onClose_ = nullptr;
    bool        used_     = false;
};

#endif // MONO_WIDGET_H

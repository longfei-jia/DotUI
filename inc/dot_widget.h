// Dot_widget.h —— 覆盖层控件（MsgBox / Toast / ProgressBar）
#ifndef Dot_WIDGET_H
#define Dot_WIDGET_H

#include "Dot_defs.h"
#include "Dot_ui.h"
#include "resources.h"
#include <stdio.h>

class DotWidget {
public:
    enum Kind : uint8_t {
        KIND_NONE = 0, KIND_MSG = 1, KIND_TOAST = 2, KIND_PROGRESS = 3,
    };

    static constexpr int kPoolSize = 4;

    static DotWidget* pool() {
        static DotWidget s[kPoolSize];
        return s;
    }
    static DotWidget* Alloc() {
        DotWidget* p = pool();
        for (int i = 0; i < kPoolSize; ++i) {
            if (!p[i].used_) { p[i].reset(); p[i].used_ = true; return &p[i]; }
        }
        return nullptr;                       // 池耗尽
    }
    static void Free(DotWidget* w) { if (w) w->used_ = false; }

    // ---- 工厂：弹出各类控件 ----
    static void MsgBox(DotUI& ui, const char* title, const char* text,
                       DotCallback onClose = nullptr) {
        DotWidget* w = Alloc();
        if (!w) return;
        w->kind_    = KIND_MSG;
        w->title_   = title;
        w->text_    = text;
        w->onClose_ = onClose;
        ui.PushOverlay(w);
    }

    static void Toast(DotUI& ui, const char* text,
                      uint32_t ms = DotAnim::kToastMs) {
        DotWidget* w = Alloc();
        if (!w) return;
        w->kind_     = KIND_TOAST;
        w->text_     = text;
        w->expireAt_ = ui.Now() + ms;
        ui.PushOverlay(w);
    }

    static DotWidget* ProgressBar(DotUI& ui, const char* title, int percent = 0) {
        DotWidget* w = Alloc();
        if (!w) return nullptr;
        w->kind_    = KIND_PROGRESS;
        w->title_   = title;
        w->percent_ = dotClampi(percent, 0, 100);
        ui.PushOverlay(w);
        return w;                              // 返回句柄可 SetPercent 更新
    }

    // ---- 运行期更新 ----
    int  GetPercent() const { return percent_; }
    void SetPercent(int p)  { percent_ = dotClampi(p, 0, 100); }
    void SetText(const char* t) { text_ = t; }

    // 超时判断：差值比较，避免毫秒回绕
    bool IsExpired(uint32_t now) const {
        return kind_ == KIND_TOAST && (int32_t)(now - expireAt_) > 0;
    }

    void Update(DotUI& ui) {
        if (IsExpired(ui.Now())) ui.PopOverlay();
    }

    // 按键处理。返回 true 表示事件已被吃掉（模态）
    bool OnKey(DotUI& ui, DotInput::Key k) {
        switch (kind_) {
        case KIND_TOAST:
            ui.PopOverlay();
            return false;                      // 不吃按键
        case KIND_MSG:
            if (k == DotInput::KEY_OK || k == DotInput::KEY_BACK) {
                if (onClose_) onClose_(&ui, nullptr);
                ui.PopOverlay();
            }
            return true;
        case KIND_PROGRESS:
            // 模态：←/→ 调整进度（长按连发自然生效），OK/B 关闭
            if (k == DotInput::KEY_RIGHT)
                SetPercent(percent_ + 10);
            else if (k == DotInput::KEY_LEFT)
                SetPercent(percent_ - 10);
            else if (k == DotInput::KEY_BACK || k == DotInput::KEY_OK)
                ui.PopOverlay();
            return true;   // 其余按键仍被吃掉（模态）
        default:
            return false;
        }
    }

    void Draw(DotUI& ui) {
        switch (kind_) {
        case KIND_MSG:      drawMsg(ui);      break;
        case KIND_TOAST:    drawToast(ui);    break;
        case KIND_PROGRESS: drawProgress(ui); break;
        default: break;
        }
    }

private:
    DotWidget() {}
    DotWidget(const DotWidget&) = delete;
    DotWidget& operator=(const DotWidget&) = delete;

    void reset() {
        kind_ = KIND_NONE; title_ = nullptr; text_ = nullptr;
        percent_ = 0; expireAt_ = 0; onClose_ = nullptr;
    }

    static void setColor(DotUI& ui, uint8_t c) {
        u8g2_SetDrawColor(ui.G2(), c);
    }

    // 消息框：居中卡片 = 标题 + 分隔线 + 单行正文 + OK 按钮
    // 正文刻意保持单行：1-bit 小屏上多行折行收益低
    void drawMsg(DotUI& ui) {
        const int w = 108, h = 52;
        const int x = (DotLayout::kScreenW - w) / 2;
        const int y = (DotLayout::kScreenH - h) / 2;

        setColor(ui, ui.Theme().bg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(x, y, w, h, 4);

        if (title_) {
            ui.DrawTextCenter(x + w / 2, y + 14, title_);
            ui.FillBox(x + 5, y + 16, w - 10, 1);
        }
        if (text_) ui.DrawTextCenter(x + w / 2, y + 32, text_);

        const int bw = 30, bh = 12;
        const int bx = x + w - bw - 6, by = y + h - bh - 3;
        setColor(ui, ui.Theme().bg);
        ui.FillRound(bx, by, bw, bh, 3);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(bx, by, bw, bh, 3);
        ui.DrawTextCenter(bx + bw / 2, by + 9, DotRes::kTextOk);
    }

    // 气泡：宽度随文本自适应，屏幕下 1/4 处居中，实底反色
    void drawToast(DotUI& ui) {
        if (!text_) return;
        const int tw = ui.TextWidth(text_);
        const int w  = tw + 14, h = 20;
        const int x  = (DotLayout::kScreenW - w) / 2;
        const int y  = DotLayout::kScreenH * 3 / 4 - h / 2;

        setColor(ui, ui.Theme().fg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().bg);
        ui.DrawText(x + 7, y + 14, text_);
        setColor(ui, ui.Theme().fg);
    }

    // 进度框：标题 + 圆角进度条 + 百分比
    void drawProgress(DotUI& ui) {
        const int w = 100, h = 44;
        const int x = (DotLayout::kScreenW - w) / 2;
        const int y = (DotLayout::kScreenH - h) / 2;
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

    Kind        kind_ = KIND_NONE;
    const char* title_ = nullptr;
    const char* text_  = nullptr;
    int         percent_ = 0;
    uint32_t    expireAt_ = 0;
    DotCallback onClose_ = nullptr;
    bool        used_ = false;
};

#endif
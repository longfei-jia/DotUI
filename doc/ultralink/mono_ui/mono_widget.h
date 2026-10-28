// mono_widget.h —— 覆盖层控件
//
// 对应 3rdParty/mono_ui/mono_widget.h（注意：原项目也只有头文件，无同名 cpp，
// 说明这些控件都是内联实现 + 一个静态对象池）。
//
// 五种控件对应 MonoUI 作者示例里的四个测试函数：
//   TestMsgBox          -> MsgBox
//   TestTextEditor      -> TextEditor
//   TestTextReader      -> TextReader
//   TestWidgetCallback  -> ProgressBar（配合回调刷新百分比）
//   —— 外加固件里实证存在的 Toast（0x08012344）
//
// 用法：
//   MonoWidget::MsgBox(*this, "Alert", "Something happened");
//   MonoWidget::Toast(*this, "Saved", 1200);
#ifndef MONO_WIDGET_H
#define MONO_WIDGET_H

#include "mono_defs.h"
#include "mono_ui.h"
#include "resources.h"

#include <stdio.h>
#include <string.h>

class MonoWidget {
public:
    enum Kind : uint8_t {
        KIND_NONE     = 0,
        KIND_MSG      = 1,
        KIND_TOAST    = 2,
        KIND_PROGRESS = 3,
        KIND_EDITOR   = 4,
        KIND_READER   = 5,
    };

    static constexpr int kPoolSize = 4;

    // =====================================================================
    // 对象池
    //
    // 放在函数内静态变量里，这样整个组件只需要头文件，不用额外的 .cpp
    // 来定义类外静态成员（原项目同样只有 mono_widget.h）。
    // =====================================================================
    static MonoWidget* pool() {
        static MonoWidget s[kPoolSize];
        return s;
    }

    static MonoWidget* Alloc() {
        MonoWidget* p = pool();
        for (int i = 0; i < kPoolSize; ++i) {
            if (!p[i].used_) {
                const bool used = p[i].used_;
                (void)used;
                p[i].reset();
                p[i].used_ = true;
                return &p[i];
            }
        }
        return nullptr;
    }

    static void Free(MonoWidget* w) {
        if (w) w->used_ = false;
    }

    // =====================================================================
    // 工厂
    // =====================================================================

    // 模态提示框：标题 + 正文 + OK，OK/BACK 关闭
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

    // 气泡提示：不拦输入，到点自动消失
    static void Toast(MonoUI& ui, const char* text, uint32_t ms = MonoAnim::kToastMs) {
        MonoWidget* w = Alloc();
        if (!w) return;
        w->kind_     = KIND_TOAST;
        w->text_     = text;
        w->expireAt_ = ui.Now() + ms;
        ui.PushOverlay(w);
    }

    // 进度条：外部拿着指针反复 SetPercent 即可
    static MonoWidget* ProgressBar(MonoUI& ui, const char* title, int percent = 0) {
        MonoWidget* w = Alloc();
        if (!w) return nullptr;
        w->kind_    = KIND_PROGRESS;
        w->title_   = title;
        w->percent_ = monoClampi(percent, 0, 100);
        ui.PushOverlay(w);
        return w;
    }

    // 单行文本编辑：直接在调用者的缓冲上改
    static void TextEditor(MonoUI& ui, char* buf, size_t cap, const char* title,
                           MonoCallback onClose = nullptr) {
        if (!buf || cap < 2) return;
        MonoWidget* w = Alloc();
        if (!w) return;
        w->kind_    = KIND_EDITOR;
        w->title_   = title;
        w->editBuf_ = buf;
        w->editCap_ = cap;
        w->editPos_ = strlen(buf);
        if (w->editPos_ >= cap) w->editPos_ = cap - 1;
        w->onClose_ = onClose;
        ui.PushOverlay(w);
    }

    // 只读文本阅读器：按行滚动
    static void TextReader(MonoUI& ui, const char* title, const char* text) {
        MonoWidget* w = Alloc();
        if (!w) return;
        w->kind_   = KIND_READER;
        w->title_  = title;
        w->text_   = text;
        w->scroll_ = 0;
        ui.PushOverlay(w);
    }

    // =====================================================================
    // 状态
    // =====================================================================
    Kind    GetKind() const { return kind_; }
    int     GetPercent() const { return percent_; }
    void    SetPercent(int p) { percent_ = monoClampi(p, 0, 100); }
    void    SetText(const char* t) { text_ = t; }
    bool    IsModal() const { return kind_ != KIND_TOAST; }
    bool    IsExpired(uint32_t now) const {
        return kind_ == KIND_TOAST && (int32_t)(now - expireAt_) > 0;
    }

    // =====================================================================
    // 每帧更新
    // =====================================================================
    void Update(MonoUI& ui) {
        if (IsExpired(ui.Now())) ui.PopOverlay();
    }

    // =====================================================================
    // 输入：返回 true 表示已消费
    // =====================================================================
    bool OnKey(MonoUI& ui, MonoInput::Key k) {
        switch (kind_) {
        case KIND_TOAST:
            ui.PopOverlay();          // 任意键都关掉
            return false;             // 不吞键，页面继续响应

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

        case KIND_EDITOR:
            return keyEditor(ui, k);

        case KIND_READER:
            return keyReader(ui, k);

        default:
            return false;
        }
    }

    // =====================================================================
    // 绘制
    // =====================================================================
    void Draw(MonoUI& ui) {
        switch (kind_) {
        case KIND_MSG:      drawMsg(ui);      break;
        case KIND_TOAST:    drawToast(ui);    break;
        case KIND_PROGRESS: drawProgress(ui); break;
        case KIND_EDITOR:   drawEditor(ui);   break;
        case KIND_READER:   drawReader(ui);   break;
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
        editBuf_  = nullptr;
        editCap_  = 0;
        editPos_  = 0;
        percent_  = 0;
        scroll_   = 0;
        expireAt_ = 0;
        onClose_  = nullptr;
    }

    // ---------------------------------------------------------------------
    // 键盘：编辑框
    // ---------------------------------------------------------------------
    static const char* charset() {
        return " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_./:";
    }
    static constexpr int kCharsetLen = 68;   // 1+26+26+10+5

    static char charAt(size_t pos) {
        return charset()[pos % kCharsetLen];
    }
    static size_t charIndex(char c) {
        const char* s = charset();
        for (size_t i = 0; i < (size_t)kCharsetLen; ++i)
            if (s[i] == c) return i;
        return 0;
    }

    bool keyEditor(MonoUI& ui, MonoInput::Key k) {
        (void)ui;
        if (!editBuf_) return true;

        switch (k) {
        case MonoInput::KEY_LEFT:
            if (editPos_ > 0) --editPos_;
            break;
        case MonoInput::KEY_RIGHT:
            if (editPos_ + 1 < editCap_) ++editPos_;
            break;
        case MonoInput::KEY_UP: {
            const size_t ci = charIndex(editBuf_[editPos_]);
            editBuf_[editPos_] = charAt(ci + 1);
            break;
        }
        case MonoInput::KEY_DOWN: {
            const size_t ci = charIndex(editBuf_[editPos_]);
            editBuf_[editPos_] = charAt(ci + kCharsetLen - 1);
            break;
        }
        case MonoInput::KEY_OK:
            if (onClose_) onClose_(&ui, editBuf_);
            ui.PopOverlay();
            break;
        case MonoInput::KEY_BACK:
            ui.PopOverlay();
            break;
        default:
            break;
        }
        return true;
    }

    bool keyReader(MonoUI& ui, MonoInput::Key k) {
        (void)ui;
        switch (k) {
        case MonoInput::KEY_UP:    if (scroll_ > 0) --scroll_; break;
        case MonoInput::KEY_DOWN:  ++scroll_;                  break;
        case MonoInput::KEY_BACK:
        case MonoInput::KEY_OK:    ui.PopOverlay();            break;
        default: break;
        }
        return true;
    }

    // ---------------------------------------------------------------------
    // 绘制：提示框
    // ---------------------------------------------------------------------
    void drawMsg(MonoUI& ui) {
        const int w   = 108;
        const int x   = (MonoLayout::kScreenW - w) / 2;
        const int asc = ui.FontAscent();
        const int chW = 6;                       // 6x12 等宽字模
        const int lineH = 13;

        // ---- 先折行，再按行数决定框高 ----
        char line[2][24];
        int  nLine = 0;

        if (text_) {
            const int maxCols = (w - 12) / chW;          // 16
            const char* p = text_;

            while (*p && nLine < 2) {
                int take = 0;
                int lastSpace = -1;
                while (p[take] && take < maxCols) {
                    if (p[take] == ' ') lastSpace = take;
                    ++take;
                }
                if (p[take] && lastSpace > 0) take = lastSpace;

                int n = take < 23 ? take : 23;
                memcpy(line[nLine], p, (size_t)n);
                line[nLine][n] = '\0';
                ++nLine;

                p += take;
                while (*p == ' ') ++p;
            }
        }
        if (nLine == 0) { line[0][0] = '\0'; nLine = 1; }

        // 标题区 20 + 正文 n*13 + 按钮区 15
        const int h = 20 + nLine * lineH + 15;
        const int y = (MonoLayout::kScreenH - h) / 2;

        // 白底盖住下层
        setColor(ui, ui.Theme().bg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(x, y, w, h, 4);

        // 标题 + 分隔线（分隔线落在标题下伸部之下）
        if (title_) {
            ui.DrawTextCenter(x + w / 2, y + asc + 3, title_);
            ui.FillBox(x + 5, y + asc + 6, w - 10, 1);
        }

        for (int i = 0; i < nLine; ++i)
            if (line[i][0])
                ui.DrawTextCenter(x + w / 2, y + 20 + asc + i * lineH, line[i]);

        // OK 按钮（右下角）
        const int bw = 30, bh = 12;
        const int bx = x + w - bw - 6;
        const int by = y + h - bh - 3;

        setColor(ui, ui.Theme().bg);
        ui.FillRound(bx, by, bw, bh, 3);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(bx, by, bw, bh, 3);
        ui.DrawTextCenter(bx + bw / 2, by + asc + 4, "OK");
    }

    // ---------------------------------------------------------------------
    // 绘制：Toast
    // ---------------------------------------------------------------------
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

    // ---------------------------------------------------------------------
    // 绘制：进度条
    // ---------------------------------------------------------------------
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

        // 轨道
        const int tx = x + 8, tw = w - 16, ty = y + h - 20, th = 8;
        ui.FrameRound(tx, ty, tw, th, 2);
        setColor(ui, ui.Theme().fg);
        const int fill = (tw - 4) * percent_ / 100;
        if (fill > 0) ui.FillRound(tx + 2, ty + 2, fill, th - 4, 1);

        // 百分比
        char buf[8];
        snprintf(buf, sizeof buf, "%d%%", percent_);
        ui.DrawTextCenter(x + w / 2, y + h - 4, buf);
    }

    // ---------------------------------------------------------------------
    // 绘制：文本编辑
    // ---------------------------------------------------------------------
    void drawEditor(MonoUI& ui) {
        const int w   = 112;
        const int h   = 42;
        const int x   = (MonoLayout::kScreenW - w) / 2;
        const int y   = (MonoLayout::kScreenH - h) / 2;
        const int asc = ui.FontAscent();

        setColor(ui, ui.Theme().bg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(x, y, w, h, 4);

        if (title_) ui.DrawTextCenter(x + w / 2, y + asc + 3, title_);

        if (!editBuf_) return;

        // 可视窗口：保证光标在框内
        const int maxChars = (w - 12) / 6;
        size_t start = 0;
        if ((int)editPos_ >= maxChars) start = editPos_ - maxChars + 1;

        char view[24];
        size_t n = strlen(editBuf_ + start);
        if ((int)n > maxChars) n = maxChars;
        memcpy(view, editBuf_ + start, n);
        view[n] = '\0';

        const int tx = x + 6, ty = y + h - 8;
        ui.DrawText(tx, ty, view);

        // 光标
        const int cx = tx + (int)(editPos_ - start) * 6;
        ui.FillBox(cx, ty - asc, 5, 2);
    }

    // ---------------------------------------------------------------------
    // 绘制：文本阅读
    // ---------------------------------------------------------------------
    void drawReader(MonoUI& ui) {
        const int x   = 2;
        const int y   = 0;
        const int w   = MonoLayout::kScreenW - 4;
        const int h   = MonoLayout::kScreenH;
        const int asc = ui.FontAscent();
        const int lh  = asc + 3;

        setColor(ui, ui.Theme().bg);
        ui.FillBox(0, 0, MonoLayout::kScreenW, MonoLayout::kScreenH);
        setColor(ui, ui.Theme().fg);
        ui.FrameBox(x, y, w, h);

        int ty = y + asc + 3;
        if (title_) {
            ui.DrawText(x + 4, ty, title_);
            ui.FillBox(x + 1, ty + 3, w - 2, 1);
            ty += lh;
        }

        if (!text_) return;

        // 按行号跳过 scroll_ 行
        const char* p = text_;
        int line = 0;
        while (*p && ty < y + h - 2) {
            const char* e = p;
            while (*e && *e != '\n') ++e;
            const int len = (int)(e - p);

            if (line >= scroll_) {
                char buf[24];
                int n = len > 20 ? 20 : len;
                memcpy(buf, p, n);
                buf[n] = '\0';
                ui.DrawText(x + 4, ty, buf);
                ty += lh;
            }
            ++line;
            p = (*e == '\n') ? e + 1 : e;
        }
    }

    // ---------------------------------------------------------------------
    static void setColor(MonoUI& ui, uint8_t c) {
        u8g2_SetDrawColor(ui.G2(), c);
    }

    // =====================================================================
    // 状态
    // =====================================================================
    Kind        kind_     = KIND_NONE;
    const char* title_    = nullptr;
    const char* text_     = nullptr;
    char*       editBuf_  = nullptr;
    size_t      editCap_  = 0;
    size_t      editPos_  = 0;
    int         percent_  = 0;
    int         scroll_   = 0;
    uint32_t    expireAt_ = 0;
    MonoCallback onClose_ = nullptr;
    bool        used_     = false;
};

#endif // MONO_WIDGET_H

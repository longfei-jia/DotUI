// uvector.h —— 极简动态数组，替代 std::vector
//
// 仅提供 MonoPage::items_ 需要的最小接口：
//   push_back / size / empty / operator[] / clear
// 特点：
//   - 不可复制（指针语义数组，禁止浅拷贝）
//   - clear() 只清长度不释放内存（页面池复用，避免反复分配）
//   - 扩容失败直接 abort（UI 初始化期失败即可视）
#ifndef DOTUI_UVECTOR_H
#define DOTUI_UVECTOR_H

#include <stddef.h>
#include <stdlib.h>

template <typename T>
class uvector {
public:
    uvector() = default;
    ~uvector() { free(data_); }

    uvector(const uvector&) = delete;
    uvector& operator=(const uvector&) = delete;

    size_t size()  const { return size_; }
    bool   empty() const { return size_ == 0; }

    T&       operator[](size_t i)       { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

    void clear() { size_ = 0; }   // 保留容量，仅重置长度

    void push_back(const T& value) {
        if (size_ == cap_) {
            // 1.5 倍扩容，首块 4 个元素
            const size_t next = cap_ ? cap_ + cap_ / 2 : 4;
            T* p = static_cast<T*>(realloc(data_, next * sizeof(T)));
            if (!p) abort();
            data_ = p;
            cap_  = next;
        }
        data_[size_++] = value;
    }

private:
    T*     data_ = nullptr;
    size_t size_ = 0;
    size_t cap_  = 0;
};

#endif // DOTUI_UVECTOR_H

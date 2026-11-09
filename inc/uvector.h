#ifndef DOTUI_UVECTOR_H
#define DOTUI_UVECTOR_H

#include <stddef.h>
#include <stdlib.h>

template <typename T>
class uvector
{
public:
    uvector() = default;
    ~uvector() { free(data_); }

    uvector(const uvector &) = delete;
    uvector &operator=(const uvector &) = delete;

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    T &operator[](size_t i) { return data_[i]; }
    const T &operator[](size_t i) const { return data_[i]; }

    void clear() { size_ = 0; }

    void push_back(const T &value)
    {
        if (size_ == cap_)
        {
            const size_t next = cap_ ? cap_ + cap_ / 2 : 4;
            T *p = static_cast<T *>(realloc(data_, next * sizeof(T)));
            if (!p)
                abort();
            data_ = p;
            cap_ = next;
        }
        data_[size_++] = value;
    }

private:
    T *data_ = nullptr;
    size_t size_ = 0;
    size_t cap_ = 0;
};

#endif
#pragma once

#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <type_traits>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) { 
    }
    RawMemory(RawMemory&& other) noexcept 
        : buffer_(other.buffer_), capacity_(other.capacity_) {
        other.buffer_ = nullptr;
        other.capacity_ = 0;
    }
    RawMemory& operator=(RawMemory&& other) noexcept {
        if (this != &other) {
            Deallocate(buffer_);
            buffer_ = other.buffer_;
            capacity_ = other.capacity_;
            other.buffer_ = nullptr;
            other.capacity_ = 0;
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size) : data_(size) {
        size_ = 0;
        if (size > 0) {
            std::uninitialized_value_construct_n(data_.GetAddress(), size);
            size_ = size;
        }
    }

    Vector(const Vector& other) : data_(other.size_) {
        size_ = 0;
        if (other.size_ > 0) {
            std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
            size_ = other.size_;
        }
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                AssignFrom(rhs);
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    const_iterator cend() const noexcept {
        return end();
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            size_ = new_size;
        } else if (new_size > size_) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            size_ = new_size;
        }
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ == Capacity()) {
            const size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
            RawMemory<T> new_data(new_capacity);
            try {
                new (new_data + size_) T(std::forward<Args>(args)...);
            } catch (...) {
                throw;
            }
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> ||
                              !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
                } else {
                    std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
                }
            } catch (...) {
                std::destroy_at(new_data + size_);
                throw;
            }
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        } else {
            new (data_ + size_) T(std::forward<Args>(args)...);
        }
        ++size_;
        return data_[size_ - 1];
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        const size_t offset = pos - begin();
        assert(pos >= begin() && pos <= end());

        if (size_ == Capacity()) {
            const size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
            RawMemory<T> new_data(new_capacity);

            try {
                new (new_data + offset) T(std::forward<Args>(args)...);
            } catch (...) {
                throw;
            }

            if (offset > 0) {
                try {
                    if constexpr (std::is_nothrow_move_constructible_v<T> ||
                                  !std::is_copy_constructible_v<T>) {
                        std::uninitialized_move_n(data_.GetAddress(), offset, new_data.GetAddress());
                    } else {
                        std::uninitialized_copy_n(data_.GetAddress(), offset, new_data.GetAddress());
                    }
                } catch (...) {
                    std::destroy_at(new_data + offset);
                    throw;
                }
            }

            if (size_ > offset) {
                try {
                    if constexpr (std::is_nothrow_move_constructible_v<T> ||
                                  !std::is_copy_constructible_v<T>) {
                        std::uninitialized_move_n(
                            data_.GetAddress() + offset,
                            size_ - offset,
                            new_data.GetAddress() + offset + 1
                        );
                    } else {
                        std::uninitialized_copy_n(
                            data_.GetAddress() + offset,
                            size_ - offset,
                            new_data.GetAddress() + offset + 1
                        );
                    }
                } catch (...) {
                    std::destroy_n(new_data.GetAddress(), offset + 1);
                    throw;
                }
            }
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
            ++size_;
            return begin() + offset;
        } else {
            if (offset == size_) {
                EmplaceBack(std::forward<Args>(args)...);
                return begin() + offset;
            }
            T temp(std::forward<Args>(args)...);
            try {
                new (end()) T(std::move(*(end() - 1)));
            } catch (...) {
                throw;
            }
            ++size_;

            try {
                std::move_backward(begin() + offset, end() - 1, end());
                data_[offset] = std::move(temp);
            } catch (...) {
                std::destroy_at(data_ + size_ - 1);
                --size_;
                throw;
            }
            return begin() + offset;
        }
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    iterator Erase(const_iterator pos) {
        const size_t offset = pos - begin();
        assert(pos >= begin() && pos < end());
        std::move(begin() + offset + 1, end(), begin() + offset);
        PopBack();
        return begin() + offset;
    }

    void PopBack() noexcept {
        assert(size_ > 0);
        std::destroy_at(data_.GetAddress() + size_ - 1);
        --size_;
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);

        if (size_ > 0) {
            if constexpr (std::is_nothrow_move_constructible_v<T> ||
                          !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
            } else {
                std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
            }
        }

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    void AssignFrom(const Vector& other) {
        std::copy(other.data_.GetAddress(),
                  other.data_.GetAddress() + std::min(other.size_, size_),
                  data_.GetAddress());
        if (other.size_ > size_) {
            std::uninitialized_copy_n(other.data_.GetAddress() + size_,
                                       other.size_ - size_,
                                       data_.GetAddress() + size_);
        } else if (other.size_ < size_) {
            std::destroy_n(data_.GetAddress() + other.size_, size_ - other.size_);
        }
        size_ = other.size_;
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};

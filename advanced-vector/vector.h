#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
            : buffer_(Allocate(capacity))
            , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    RawMemory(RawMemory&& other) noexcept {
        buffer_ = std::move(other.buffer_);
        capacity_ = std::move(other.capacity_);
        other.capacity_ = 0;
        other.buffer_ = nullptr;
    }
    RawMemory& operator=(RawMemory&& rhs) noexcept {
        Deallocate(buffer_);
        buffer_ = std::move(rhs.buffer_);
        capacity_ = std::move(rhs.capacity_);
        rhs.capacity_ = 0;
        rhs.buffer_ = nullptr;
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
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
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
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
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }

    Vector() = default;

    explicit Vector(size_t size) : data_(size),
                                   size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(),
                                             size);
    }

    Vector(const Vector& other) : data_(other.size_),
                                  size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(),
                                  other.size_,
                                  data_.GetAddress());
    }

    Vector(Vector&& other) noexcept {
        data_ = std::move(other.data_);
        size_ = std::move(other.size_);
        other.size_ = 0;
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(),
                                      size_,
                                      new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(),
                                      size_,
                                      new_data.GetAddress());
        }

        std::destroy_n(data_.GetAddress(), size_);

        data_.Swap(new_data);
    }

    void Swap(Vector& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
    }

    void Resize(size_t new_size) {
        if (size_ < new_size) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress(),
                                                 new_size - size_);
        } else {
            std::destroy_n(data_.GetAddress() + new_size,
                           size_ - new_size);
        }
        size_ = new_size;
    }

    void PopBack() noexcept {
        assert(size_ > 0);
        std::destroy_n(data_.GetAddress() + size_ - 1, 1);
        --size_;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... value) {
        //тут используем std::forward()
        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : 2 * size_);
            new (new_data + size_) T(std::forward<Args>(value)...);
            TransferDataForEmplace(data_.GetAddress(), size_, new_data.GetAddress());
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        } else {
            new (data_ + size_) T(std::forward<Args>(value)...);
        }
        return data_[size_++];
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        size_t pos_index = std::distance(this->cbegin(), pos);
        if (size_ == data_.Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : 2 * size_);
            new (new_data.GetAddress() + pos_index) T(std::forward<Args>(args)...);
            TransferDataForEmplace(data_.GetAddress(),
                                   pos_index,
                                   new_data.GetAddress());
            size_t dist_to_end = std::distance(pos, cend());
            TransferDataForEmplace(data_.GetAddress() + pos_index,
                                   dist_to_end,
                                   new_data.GetAddress() + pos_index + 1);
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        } else {
            if (pos != cend()) {
                new (end()) T(std::forward<T>(*(end() - 1)));
                std::move_backward(begin() + pos_index, end() - 1, end());
                data_[pos_index] = T(std::forward<Args>(args)...);
            } else {
                new (data_.GetAddress() + pos_index) T(std::forward<Args>(args)...);
            }
        }
        ++size_;
        return begin() + pos_index;
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
            size_t pos_index = std::distance(cbegin(), pos);
            std::move(begin() + pos_index + 1, end(), begin() + pos_index);
            PopBack();
            return begin() + pos_index;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }
    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                /* Применить copy-and-swap */
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                /* Скопировать элементы из rhs, создав при необходимости новые
                   или удалив существующие */
                for (size_t i = 0; i < std::min(size_, rhs.size_); ++i) {
                    data_[i] = rhs.data_[i];
                }
                if (size_ < rhs.size_) {
                    std::uninitialized_copy_n(rhs.data_.GetAddress(),
                                              rhs.size_ - size_,
                                              data_.GetAddress() + size_);
                } else {
                    std::destroy_n(data_.GetAddress() + rhs.size_,
                                   size_ - rhs.size_);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            data_ = std::move(rhs.data_);
            size_ = std::move(rhs.size_);
            rhs.size_ = 0;
        }
        return *this;
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    void TransferDataForEmplace(iterator src, size_t size, iterator dst) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(src, size, dst);
        } else {
            std::uninitialized_copy_n(src, size, dst);
        }
    }

    // Вызывает деструкторы n объектов массива по адресу buf
    static void DestroyN(T* buf, size_t n) noexcept {
        for (size_t i = 0; i != n; ++i) {
            Destroy(buf + i);
        }
    }

    // Создаёт копию объекта elem в сырой памяти по адресу buf
    static void CopyConstruct(T* buf, const T& elem) {
        new (buf) T(elem);
    }

    // Вызывает деструктор объекта по адресу buf
    static void Destroy(T* buf) noexcept {
        buf->~T();
    }
};
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace draz::ds {

// Linked-list stack (last in, first out). The cart is a stack, so "undo
// last item" is a pop. Complaints are shown newest first from a stack too.
template <typename T>
class Stack {
public:
    Stack() = default;
    ~Stack() { clear(); }
    Stack(const Stack&) = delete;
    Stack& operator=(const Stack&) = delete;
    Stack(Stack&& other) noexcept : top_(std::move(other.top_)), size_(other.size_) { other.size_ = 0; }
    Stack& operator=(Stack&& other) noexcept {
        if (this != &other) {
            clear();
            top_ = std::move(other.top_);
            size_ = other.size_;
            other.size_ = 0;
        }
        return *this;
    }

    void push(T value) {
        auto node = std::make_unique<Node>(std::move(value));
        node->next = std::move(top_);
        top_ = std::move(node);
        ++size_;
    }

    T pop() {
        if (!top_) throw std::logic_error("pop from an empty stack");
        T value = std::move(top_->value);
        top_ = std::move(top_->next);
        --size_;
        return value;
    }

    T& top() {
        if (!top_) throw std::logic_error("top of an empty stack");
        return top_->value;
    }

    // Visits from the top (newest) down to the bottom (oldest).
    void forEach(const std::function<void(const T&)>& visit) const {
        for (const Node* n = top_.get(); n; n = n->next.get()) visit(n->value);
    }

    // Finds the first element from the top that matches.
    T* findFirst(const std::function<bool(const T&)>& match) {
        for (Node* n = top_.get(); n; n = n->next.get()) {
            if (match(n->value)) return &n->value;
        }
        return nullptr;
    }

    // Removes the first element from the top that matches, keeping the
    // order of everything else. Returns false if nothing matched.
    bool removeFirst(const std::function<bool(const T&)>& match) {
        std::unique_ptr<Node>* slot = &top_;
        while (*slot && !match((*slot)->value)) slot = &(*slot)->next;
        if (!*slot) return false;
        *slot = std::move((*slot)->next);
        --size_;
        return true;
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Unlinks one node at a time, so a long stack can't overflow the call
    // stack the way a chain of recursive destructors would.
    void clear() {
        while (top_) top_ = std::move(top_->next);
        size_ = 0;
    }

private:
    struct Node {
        explicit Node(T v) : value(std::move(v)) {}
        T value;
        std::unique_ptr<Node> next;
    };

    std::unique_ptr<Node> top_;
    std::size_t size_ = 0;
};

}  // namespace draz::ds

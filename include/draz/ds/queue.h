#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace draz::ds {

// Linked-list queue (first in, first out). Placed orders wait here, and the
// admin always processes the oldest one first.
template <typename T>
class Queue {
public:
    Queue() = default;
    ~Queue() { clear(); }
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    void enqueue(T value) {
        auto node = std::make_unique<Node>(std::move(value));
        Node* raw = node.get();
        if (rear_) {
            rear_->next = std::move(node);
        } else {
            front_ = std::move(node);
        }
        rear_ = raw;
        ++size_;
    }

    T dequeue() {
        if (!front_) throw std::logic_error("dequeue from an empty queue");
        T value = std::move(front_->value);
        front_ = std::move(front_->next);
        if (!front_) rear_ = nullptr;  // the queue is empty again
        --size_;
        return value;
    }

    const T& front() const {
        if (!front_) throw std::logic_error("front of an empty queue");
        return front_->value;
    }

    // Visits from the front (oldest) to the rear (newest).
    void forEach(const std::function<void(const T&)>& visit) const {
        for (const Node* n = front_.get(); n; n = n->next.get()) visit(n->value);
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    void clear() {
        while (front_) front_ = std::move(front_->next);
        rear_ = nullptr;
        size_ = 0;
    }

private:
    struct Node {
        explicit Node(T v) : value(std::move(v)) {}
        T value;
        std::unique_ptr<Node> next;
    };

    std::unique_ptr<Node> front_;  // owns the chain
    Node* rear_ = nullptr;         // points at the last node, owned by the chain
    std::size_t size_ = 0;
};

}  // namespace draz::ds

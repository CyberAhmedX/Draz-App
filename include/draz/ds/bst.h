#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace draz::ds {

// Binary search tree keyed by Key. Products live here, keyed by product ID,
// so finding, adding and removing a product takes O(height) steps instead
// of scanning a whole list.
template <typename Key, typename Value>
class BST {
public:
    BST() = default;
    BST(const BST&) = delete;
    BST& operator=(const BST&) = delete;
    BST(BST&&) noexcept = default;
    BST& operator=(BST&&) noexcept = default;

    // Returns false (and changes nothing) if the key is already in the tree.
    bool insert(const Key& key, Value value) {
        std::unique_ptr<Node>* slot = &root_;
        while (*slot) {
            if (key < (*slot)->key) {
                slot = &(*slot)->left;
            } else if ((*slot)->key < key) {
                slot = &(*slot)->right;
            } else {
                return false;
            }
        }
        *slot = std::make_unique<Node>(key, std::move(value));
        ++size_;
        return true;
    }

    // Follows one path down from the root, using the ordering of the keys.
    Value* find(const Key& key) {
        Node* node = root_.get();
        while (node) {
            if (key < node->key) {
                node = node->left.get();
            } else if (node->key < key) {
                node = node->right.get();
            } else {
                return &node->value;
            }
        }
        return nullptr;
    }

    const Value* find(const Key& key) const { return const_cast<BST*>(this)->find(key); }

    bool contains(const Key& key) const { return find(key) != nullptr; }

    // Removes a key. A node with two children is replaced by its in-order
    // successor (the smallest key in its right subtree), moving the
    // successor's key AND value, so the tree keeps its order.
    bool erase(const Key& key) {
        std::unique_ptr<Node>* slot = &root_;
        while (*slot && (key < (*slot)->key || (*slot)->key < key)) {
            slot = key < (*slot)->key ? &(*slot)->left : &(*slot)->right;
        }
        if (!*slot) return false;

        Node* node = slot->get();
        if (!node->left) {
            *slot = std::move(node->right);
        } else if (!node->right) {
            *slot = std::move(node->left);
        } else {
            std::unique_ptr<Node>* successor = &node->right;
            while ((*successor)->left) successor = &(*successor)->left;
            node->key = std::move((*successor)->key);
            node->value = std::move((*successor)->value);
            *successor = std::move((*successor)->right);
        }
        --size_;
        return true;
    }

    // Visits every entry in ascending key order (in-order traversal).
    void inorder(const std::function<void(const Key&, const Value&)>& visit) const {
        inorder(root_.get(), visit);
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Number of nodes on the longest root-to-leaf path. 0 for an empty tree.
    int height() const { return height(root_.get()); }

    // Rebuilds the tree so it is as short as possible: the middle key
    // becomes the root, then the same for each half. Keys added in sorted
    // order (like product IDs 101, 102, 103...) would otherwise turn the
    // tree into a long chain where every lookup walks every node.
    void rebalance() {
        std::vector<std::unique_ptr<Node>> nodes;
        nodes.reserve(size_);
        detach(std::move(root_), nodes);
        root_ = build(nodes, 0, nodes.size());
    }

    void clear() {
        root_.reset();
        size_ = 0;
    }

private:
    struct Node {
        Node(Key k, Value v) : key(std::move(k)), value(std::move(v)) {}
        Key key;
        Value value;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
    };

    template <typename NodePtr, typename Visit>
    static void inorder(NodePtr node, const Visit& visit) {
        if (!node) return;
        inorder(node->left.get(), visit);
        visit(node->key, node->value);
        inorder(node->right.get(), visit);
    }

    // Moves every node into `out` in key order, unlinking their children.
    static void detach(std::unique_ptr<Node> node, std::vector<std::unique_ptr<Node>>& out) {
        if (!node) return;
        detach(std::move(node->left), out);
        std::unique_ptr<Node> right = std::move(node->right);
        out.push_back(std::move(node));
        detach(std::move(right), out);
    }

    static std::unique_ptr<Node> build(std::vector<std::unique_ptr<Node>>& nodes, std::size_t lo, std::size_t hi) {
        if (lo >= hi) return nullptr;
        std::size_t mid = lo + (hi - lo) / 2;
        std::unique_ptr<Node> node = std::move(nodes[mid]);
        node->left = build(nodes, lo, mid);
        node->right = build(nodes, mid + 1, hi);
        return node;
    }

    static int height(const Node* node) {
        if (!node) return 0;
        int left = height(node->left.get());
        int right = height(node->right.get());
        return 1 + (left > right ? left : right);
    }

    // unique_ptr children free the whole tree when the root goes away.
    std::unique_ptr<Node> root_;
    std::size_t size_ = 0;
};

}  // namespace draz::ds

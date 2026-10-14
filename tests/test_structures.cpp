#include <algorithm>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "check.h"
#include "draz/ds/bst.h"
#include "draz/ds/queue.h"
#include "draz/ds/sort.h"
#include "draz/ds/stack.h"

using namespace draz::ds;

namespace {

std::vector<int> keys(const BST<int, std::string>& tree) {
    std::vector<int> out;
    tree.inorder([&](const int& k, const std::string&) { out.push_back(k); });
    return out;
}

}  // namespace

// ---------------------------------------------------------------- BST

TEST("bst: insert, find and in-order traversal is sorted") {
    BST<int, std::string> tree;
    for (int k : {50, 30, 70, 20, 40, 60, 80}) CHECK(tree.insert(k, "v" + std::to_string(k)));
    CHECK_EQ(tree.size(), std::size_t(7));
    CHECK(keys(tree) == (std::vector<int>{20, 30, 40, 50, 60, 70, 80}));
    CHECK(tree.find(60) != nullptr);
    CHECK_EQ(*tree.find(60), std::string("v60"));
    CHECK(tree.find(65) == nullptr);
    CHECK_EQ(tree.height(), 3);
}

TEST("bst: duplicate keys are rejected and the old value is kept") {
    BST<int, std::string> tree;
    CHECK(tree.insert(5, "first"));
    CHECK(!tree.insert(5, "second"));
    CHECK_EQ(*tree.find(5), std::string("first"));
    CHECK_EQ(tree.size(), std::size_t(1));
}

TEST("old bug: deleting a node with two children kept the wrong value") {
    BST<int, std::string> tree;
    for (int k : {50, 30, 70, 60, 80}) tree.insert(k, "v" + std::to_string(k));
    CHECK(tree.erase(50));
    // The successor (60) moved up with its own value, and nothing was lost.
    CHECK(tree.find(50) == nullptr);
    CHECK_EQ(*tree.find(60), std::string("v60"));
    CHECK_EQ(*tree.find(70), std::string("v70"));
    CHECK(keys(tree) == (std::vector<int>{30, 60, 70, 80}));
}

TEST("bst: erase a leaf, a node with one child, the root and a missing key") {
    BST<int, std::string> tree;
    for (int k : {50, 30, 70, 20, 80}) tree.insert(k, "x");
    CHECK(tree.erase(20));  // leaf
    CHECK(tree.erase(70));  // one child
    CHECK(tree.erase(50));  // root
    CHECK(!tree.erase(99));
    CHECK(keys(tree) == (std::vector<int>{30, 80}));
    CHECK_EQ(tree.size(), std::size_t(2));
}

TEST("old bug: product count grew every time it was asked for") {
    BST<int, std::string> tree;
    for (int k : {3, 1, 2}) tree.insert(k, "x");
    CHECK_EQ(tree.size(), std::size_t(3));
    CHECK_EQ(tree.size(), std::size_t(3));
    tree.erase(1);
    CHECK_EQ(tree.size(), std::size_t(2));
}

TEST("bst: rebalance turns a sorted chain into a short tree") {
    BST<int, std::string> tree;
    for (int k = 1; k <= 1000; ++k) tree.insert(k, "x");
    CHECK_EQ(tree.height(), 1000);  // sorted inserts make a chain
    tree.rebalance();
    CHECK_EQ(tree.height(), 10);  // ceil(log2(1001))
    CHECK_EQ(tree.size(), std::size_t(1000));
    std::vector<int> expected(1000);
    for (int i = 0; i < 1000; ++i) expected[i] = i + 1;
    CHECK(keys(tree) == expected);
}

TEST("bst: random inserts and erases match std::map") {
    std::mt19937 rng(12345);
    BST<int, int> tree;
    std::map<int, int> model;
    for (int step = 0; step < 20000; ++step) {
        int key = static_cast<int>(rng() % 500);
        if (rng() % 3 == 0) {
            CHECK_EQ(tree.erase(key), model.erase(key) == 1);
        } else {
            CHECK_EQ(tree.insert(key, step), model.emplace(key, step).second);
        }
    }
    CHECK_EQ(tree.size(), model.size());
    std::vector<std::pair<int, int>> got;
    tree.inorder([&](const int& k, const int& v) { got.emplace_back(k, v); });
    CHECK(got == std::vector<std::pair<int, int>>(model.begin(), model.end()));
}

// ---------------------------------------------------------------- stack

TEST("stack: last in, first out") {
    Stack<int> s;
    for (int i = 1; i <= 3; ++i) s.push(i);
    CHECK_EQ(s.top(), 3);
    CHECK_EQ(s.pop(), 3);
    CHECK_EQ(s.pop(), 2);
    CHECK_EQ(s.size(), std::size_t(1));
    CHECK_EQ(s.pop(), 1);
    CHECK(s.empty());
    CHECK_THROWS(std::logic_error, s.pop());
}

TEST("stack: remove from the middle keeps the order") {
    Stack<int> s;
    for (int i = 1; i <= 5; ++i) s.push(i);
    CHECK(s.removeFirst([](int v) { return v == 3; }));
    CHECK(!s.removeFirst([](int v) { return v == 9; }));
    std::vector<int> order;
    s.forEach([&](int v) { order.push_back(v); });
    CHECK(order == (std::vector<int>{5, 4, 2, 1}));
    CHECK(s.findFirst([](int v) { return v == 2; }) != nullptr);
}

TEST("stack: a million items are freed without a crash") {
    Stack<int> s;
    for (int i = 0; i < 1000000; ++i) s.push(i);
    CHECK_EQ(s.size(), std::size_t(1000000));
    s.clear();
    CHECK(s.empty());
}

// ---------------------------------------------------------------- queue

TEST("queue: first in, first out") {
    Queue<std::string> q;
    q.enqueue("a");
    q.enqueue("b");
    q.enqueue("c");
    CHECK_EQ(q.front(), std::string("a"));
    CHECK_EQ(q.dequeue(), std::string("a"));
    CHECK_EQ(q.dequeue(), std::string("b"));
    CHECK_EQ(q.size(), std::size_t(1));
}

TEST("old bug: the queue's rear pointed at a freed node after emptying") {
    Queue<int> q;
    q.enqueue(1);
    q.dequeue();
    CHECK(q.empty());
    q.enqueue(2);
    q.enqueue(3);
    std::vector<int> order;
    q.forEach([&](int v) { order.push_back(v); });
    CHECK(order == (std::vector<int>{2, 3}));
    CHECK_THROWS(std::logic_error, (q.dequeue(), q.dequeue(), q.dequeue()));
}

// ---------------------------------------------------------------- merge sort

TEST("merge sort matches std::stable_sort on random data") {
    std::mt19937 rng(7);
    for (int n : {0, 1, 2, 3, 7, 64, 1000, 4097}) {
        std::vector<std::pair<int, int>> items;
        for (int i = 0; i < n; ++i) items.emplace_back(static_cast<int>(rng() % 50), i);
        std::vector<std::pair<int, int>> expected = items;
        auto byFirst = [](const auto& a, const auto& b) { return a.first < b.first; };
        std::stable_sort(expected.begin(), expected.end(), byFirst);
        mergeSort(items, byFirst);
        CHECK(items == expected);  // same order, including ties: the sort is stable
    }
}

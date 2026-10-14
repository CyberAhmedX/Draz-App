#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace draz::ds {

// Merge sort: O(n log n) in every case, and stable, so items that compare
// equal (two products with the same price) keep their earlier order.
template <typename T, typename Less>
void mergeSort(std::vector<T>& items, Less less) {
    if (items.size() < 2) return;
    std::vector<T> buffer(items.size());

    // Bottom-up: merge runs of 1, then 2, then 4... so there is no recursion.
    for (std::size_t width = 1; width < items.size(); width *= 2) {
        for (std::size_t lo = 0; lo < items.size(); lo += 2 * width) {
            std::size_t mid = std::min(lo + width, items.size());
            std::size_t hi = std::min(lo + 2 * width, items.size());
            std::size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                // Take from the right only when strictly smaller: that is what keeps it stable.
                if (less(items[j], items[i])) {
                    buffer[k++] = std::move(items[j++]);
                } else {
                    buffer[k++] = std::move(items[i++]);
                }
            }
            while (i < mid) buffer[k++] = std::move(items[i++]);
            while (j < hi) buffer[k++] = std::move(items[j++]);
        }
        items.swap(buffer);
    }
}

}  // namespace draz::ds

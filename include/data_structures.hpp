#pragma once

#include <algorithm>
#include <cassert>
#include <limits>
#include <vector>

namespace gro::data_structures {

template<int LogK, typename Key, typename Id>
class IndexedHeap {
public:
    struct Element {
        Key key{};
        Id id{};
    };

    explicit IndexedHeap(Id capacity)
        : size_(0),
          elements_(capacity),
          positions_(capacity, null_index()) {}

    bool empty() const {
        return size_ == 0;
    }

    Id size() const {
        return size_;
    }

    bool contains(Id id) const {
        return positions_[id] != null_index();
    }

    Key top_key() const {
        assert(!empty());
        return elements_[0].key;
    }

    Id top_id() const {
        assert(!empty());
        return elements_[0].id;
    }

    void push_or_update(Id id, Key key) {
        if (positions_[id] == null_index()) {
            elements_[size_] = Element{key, id};
            positions_[id] = size_;
            sift_up(size_++);
            return;
        }

        Id position = positions_[id];
        Key old_key = elements_[position].key;
        elements_[position].key = key;
        if (key < old_key) {
            sift_up(position);
        } else {
            sift_down(position);
        }
    }

    Element extract_min() {
        assert(!empty());
        Element result = elements_[0];
        positions_[result.id] = null_index();
        --size_;

        if (!empty()) {
            elements_[0] = elements_[size_];
            positions_[elements_[0].id] = 0;
            sift_down(0);
        }

        return result;
    }

    void clear() {
        for (Id i = 0; i < size_; ++i) {
            positions_[elements_[i].id] = null_index();
        }
        size_ = 0;
    }

private:
    static constexpr Id branching_factor() {
        return static_cast<Id>(1 << LogK);
    }

    static constexpr Id null_index() {
        return std::numeric_limits<Id>::max();
    }

    void sift_up(Id index) {
        while (index > 0) {
            Id parent = (index - 1) >> LogK;
            if (elements_[parent].key <= elements_[index].key) {
                break;
            }
            swap_positions(parent, index);
            index = parent;
        }
    }

    void sift_down(Id index) {
        while (true) {
            Id best = index;
            Id first_child = (index << LogK) + 1;
            Id last_child = std::min(first_child + branching_factor(), size_);

            for (Id child = first_child; child < last_child; ++child) {
                if (elements_[child].key < elements_[best].key) {
                    best = child;
                }
            }

            if (best == index) {
                break;
            }

            swap_positions(index, best);
            index = best;
        }
    }

    void swap_positions(Id lhs, Id rhs) {
        std::swap(elements_[lhs], elements_[rhs]);
        positions_[elements_[lhs].id] = lhs;
        positions_[elements_[rhs].id] = rhs;
    }

    Id size_ = 0;
    std::vector<Element> elements_;
    std::vector<Id> positions_;
};

}  // namespace gro::data_structures

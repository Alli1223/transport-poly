#pragma once
// Object pool with stable integer ids. Freed slots are reused lowest-index-first, which is
// canonical (derived purely from occupancy) so a loaded save game allocates exactly the same
// ids as the original game would have - a requirement for deterministic multiplayer.
//
// Never keep references to pooled objects across operations that may create objects: the
// storage vector can reallocate. Keep ids instead.

#include "core/types.hpp"

#include <functional>
#include <optional>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

namespace tp {

template <class T, class IdT>
class Pool {
    template <bool Const>
    class Iter {
        using PoolT = std::conditional_t<Const, const Pool, Pool>;
        using Ref = std::conditional_t<Const, const T&, T&>;

    public:
        struct Item {
            IdT id;
            Ref value;
        };

        Iter(PoolT* pool, usize index) : pool_(pool), index_(index) { skip(); }
        Item operator*() const { return {IdT(static_cast<typename IdT::RepType>(index_)), *pool_->slots_[index_]}; }
        Iter& operator++() {
            ++index_;
            skip();
            return *this;
        }
        bool operator==(const Iter& o) const { return index_ == o.index_; }

    private:
        void skip() {
            while (index_ < pool_->slots_.size() && !pool_->slots_[index_]) ++index_;
        }
        PoolT* pool_;
        usize index_;
    };

public:
    using Id = IdT;

    template <class... Args>
    IdT create(Args&&... args) {
        usize index;
        if (!free_.empty()) {
            index = free_.top();
            free_.pop();
            slots_[index].emplace(std::forward<Args>(args)...);
        } else {
            index = slots_.size();
            slots_.emplace_back(std::in_place, std::forward<Args>(args)...);
        }
        ++count_;
        return IdT(static_cast<typename IdT::RepType>(index));
    }

    void destroy(IdT id) {
        TP_ASSERT(contains(id));
        slots_[id.value].reset();
        free_.push(static_cast<u32>(id.value));
        --count_;
    }

    bool contains(IdT id) const { return id.valid() && id.value < slots_.size() && slots_[id.value].has_value(); }

    T& operator[](IdT id) {
        TP_DEBUG_ASSERT(contains(id));
        return *slots_[id.value];
    }
    const T& operator[](IdT id) const {
        TP_DEBUG_ASSERT(contains(id));
        return *slots_[id.value];
    }
    T* tryGet(IdT id) { return contains(id) ? &*slots_[id.value] : nullptr; }
    const T* tryGet(IdT id) const { return contains(id) ? &*slots_[id.value] : nullptr; }

    usize size() const { return count_; }
    bool empty() const { return count_ == 0; }
    // Upper bound (exclusive) of ids currently in use; useful for id-indexed side tables.
    usize slotCount() const { return slots_.size(); }

    void clear() {
        slots_.clear();
        free_ = {};
        count_ = 0;
    }

    Iter<false> begin() { return Iter<false>(this, 0); }
    Iter<false> end() { return Iter<false>(this, slots_.size()); }
    Iter<true> begin() const { return Iter<true>(this, 0); }
    Iter<true> end() const { return Iter<true>(this, slots_.size()); }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(slots_);
        if constexpr (Ar::kReading) rebuild();
    }

private:
    void rebuild() {
        free_ = {};
        count_ = 0;
        for (usize i = 0; i < slots_.size(); ++i) {
            if (slots_[i])
                ++count_;
            else
                free_.push(static_cast<u32>(i));
        }
    }

    std::vector<std::optional<T>> slots_;
    std::priority_queue<u32, std::vector<u32>, std::greater<u32>> free_;
    usize count_ = 0;
};

} // namespace tp

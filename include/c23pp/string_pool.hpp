#pragma once
// string_pool.hpp — Thread-safe string interning pool.
// Interned strings are represented as std::string_view into stable storage.

#include <memory_resource>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace c23pp {

/// A thread-safe string interning pool.  Once a string is interned its
/// storage is stable for the lifetime of the pool; callers may keep
/// string_view handles indefinitely.
class StringPool {
public:
    StringPool() = default;
    StringPool(const StringPool&) = delete;
    StringPool& operator=(const StringPool&) = delete;

    /// Intern \p str and return a stable string_view.
    [[nodiscard]] std::string_view intern(std::string_view str) {
        std::lock_guard lock{mutex_};
        auto it = set_.find(str);
        if (it != set_.end()) return *it;

        // Allocate stable storage in the bump allocator.
        auto* buf = static_cast<char*>(arena_.allocate(str.size() + 1, 1));
        std::copy(str.begin(), str.end(), buf);
        buf[str.size()] = '\0';
        std::string_view sv{buf, str.size()};
        set_.insert(sv);
        return sv;
    }

    /// Return the number of unique strings currently interned.
    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock{mutex_};
        return set_.size();
    }

private:
    // Monotonic arena — allocations are never individually freed.
    std::pmr::monotonic_buffer_resource arena_{65536};
    mutable std::mutex mutex_;

    struct SvHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };
    struct SvEqual {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept {
            return a == b;
        }
    };
    std::unordered_set<std::string_view, SvHash, SvEqual> set_;
};

} // namespace c23pp

#pragma once
// source_manager.hpp — manages source files and buffers.
//
// Supports two modes:
//   - Eager: the entire file is read into memory before tokenisation.
//   - Lazy : bytes are read on demand via a streambuf.
//
// All filename strings are interned through a shared StringPool.

#include <c23pp/string_pool.hpp>
#include <c23pp/token.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>

namespace c23pp {

/// Owns the text of one source file or an in-memory buffer (e.g. stdin).
class SourceBuffer {
public:
    /// Construct from an already-loaded string (moves content in).
    static std::shared_ptr<SourceBuffer>
    from_string(std::string_view filename, std::string content, StringPool& pool);

    /// Load a file eagerly (whole file into memory).
    static std::shared_ptr<SourceBuffer>
    load_file(const std::filesystem::path& path, StringPool& pool);

    /// Load from an istream (e.g. stdin) — always reads fully into memory.
    static std::shared_ptr<SourceBuffer>
    from_stream(std::istream& is, std::string_view filename, StringPool& pool);

    [[nodiscard]] std::string_view filename() const noexcept { return filename_; }
    [[nodiscard]] std::string_view content()  const noexcept { return content_;  }

    /// Line-start offsets for fast line/col computation.
    [[nodiscard]] SourceLocation location_at(std::size_t offset) const;

private:
    SourceBuffer() = default;
    std::string_view filename_;
    std::string      content_;
    std::vector<std::size_t> line_starts_; // offset of each line's first byte
};

/// Central registry of all source buffers in a translation unit.
/// Thread-safe for reads after construction.
class SourceManager {
public:
    explicit SourceManager(StringPool& pool, bool eager = true)
        : pool_{pool}, eager_{eager} {}

    /// Add a source buffer directly (e.g. stdin content).
    void add_buffer(std::shared_ptr<SourceBuffer> buf);

    /// Load (and register) a file.  Returns nullptr on error.
    std::shared_ptr<SourceBuffer>
    get_or_load(const std::filesystem::path& path);

    /// Check whether a file has already been loaded.
    [[nodiscard]] bool has_file(const std::filesystem::path& path) const;

    /// Search for \p header in \p search_dirs and return the first match.
    std::optional<std::filesystem::path>
    find_include(std::string_view header,
                 const std::vector<std::filesystem::path>& search_dirs,
                 bool system_include) const;

    [[nodiscard]] StringPool& pool() noexcept { return pool_; }

private:
    StringPool& pool_;
    bool        eager_;
    // map from canonical path → buffer
    std::unordered_map<std::string, std::shared_ptr<SourceBuffer>> files_;
    mutable std::mutex mutex_;
};

} // namespace c23pp

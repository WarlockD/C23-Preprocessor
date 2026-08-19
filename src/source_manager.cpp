// source_manager.cpp
#include <c23pp/source_manager.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace c23pp {

// ---------------------------------------------------------------------------
// SourceBuffer
// ---------------------------------------------------------------------------

static void build_line_starts(std::vector<std::size_t>& ls,
                               std::string_view content) {
    ls.clear();
    ls.push_back(0);
    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') ls.push_back(i + 1);
    }
}

std::shared_ptr<SourceBuffer>
SourceBuffer::from_string(std::string_view filename,
                          std::string       content,
                          StringPool&       pool) {
    auto buf = std::shared_ptr<SourceBuffer>(new SourceBuffer());
    buf->filename_ = pool.intern(filename);
    buf->content_  = std::move(content);
    build_line_starts(buf->line_starts_, buf->content_);
    return buf;
}

std::shared_ptr<SourceBuffer>
SourceBuffer::load_file(const std::filesystem::path& path, StringPool& pool) {
    std::ifstream ifs{path, std::ios::binary};
    if (!ifs)
        throw std::runtime_error("Cannot open file: " + path.string());
    std::string content{std::istreambuf_iterator<char>(ifs),
                        std::istreambuf_iterator<char>()};
    return from_string(path.string(), std::move(content), pool);
}

std::shared_ptr<SourceBuffer>
SourceBuffer::from_stream(std::istream&    is,
                          std::string_view filename,
                          StringPool&      pool) {
    std::string content{std::istreambuf_iterator<char>(is),
                        std::istreambuf_iterator<char>()};
    return from_string(filename, std::move(content), pool);
}

SourceLocation SourceBuffer::location_at(std::size_t offset) const {
    // Binary-search line_starts_ for the line that contains offset.
    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    --it;
    std::size_t line = static_cast<std::size_t>(it - line_starts_.begin()) + 1;
    std::size_t col  = offset - *it + 1;
    return {filename_,
            static_cast<std::uint32_t>(line),
            static_cast<std::uint32_t>(col)};
}

// ---------------------------------------------------------------------------
// SourceManager
// ---------------------------------------------------------------------------

void SourceManager::add_buffer(std::shared_ptr<SourceBuffer> buf) {
    std::lock_guard lock{mutex_};
    files_.emplace(std::string(buf->filename()), std::move(buf));
}

std::shared_ptr<SourceBuffer>
SourceManager::get_or_load(const std::filesystem::path& path) {
    auto canonical = std::filesystem::weakly_canonical(path).string();
    {
        std::lock_guard lock{mutex_};
        if (auto it = files_.find(canonical); it != files_.end())
            return it->second;
    }
    auto buf = SourceBuffer::load_file(path, pool_);
    {
        std::lock_guard lock{mutex_};
        files_.emplace(canonical, buf);
    }
    return buf;
}

bool SourceManager::has_file(const std::filesystem::path& path) const {
    auto canonical = std::filesystem::weakly_canonical(path).string();
    std::lock_guard lock{mutex_};
    return files_.contains(canonical);
}

std::optional<std::filesystem::path>
SourceManager::find_include(std::string_view                           header,
                             const std::vector<std::filesystem::path>& search_dirs,
                             bool /*system_include*/) const {
    for (const auto& dir : search_dirs) {
        auto candidate = dir / header;
        if (std::filesystem::exists(candidate)) return candidate;
    }
    return std::nullopt;
}

} // namespace c23pp

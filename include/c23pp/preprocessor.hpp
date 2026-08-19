#pragma once
// preprocessor.hpp — C23 macro preprocessor.
//
// The Preprocessor walks the token stream produced by the Lexer and handles:
//   #include / #include_next
//   #define / #undef
//   #if / #ifdef / #ifndef / #elif / #elifdef / #elifndef / #else / #endif
//   #line / #error / #warning / #pragma
//   __has_include / __has_c_attribute / __has_embed / __has_extension (C23)
//   Predefined macros (__FILE__, __LINE__, __DATE__, __TIME__, etc.)
//   Object-like and function-like macros with # and ## operators
//   Variadic macros (__VA_ARGS__, __VA_OPT__)
//
// Optional features (controlled by PPOptions):
//   - Emit macro expansions to a separate ostream.
//   - Load all #includes eagerly vs on demand.
//   - Process multiple files concurrently using std::async.
//   - Accept input from / emit output to arbitrary std::ostream.

#include <c23pp/lexer.hpp>
#include <c23pp/source_manager.hpp>
#include <c23pp/string_pool.hpp>
#include <c23pp/token.hpp>

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace c23pp {

/// Configuration for the preprocessor.
struct PPOptions {
    /// Additional include search paths (system paths follow user paths).
    std::vector<std::filesystem::path> include_paths;
    std::vector<std::filesystem::path> system_include_paths;

    /// Pre-defined macros supplied on the command line (-D name=value).
    std::vector<std::pair<std::string, std::string>> defines;

    /// Un-defined macros supplied on the command line (-U name).
    std::vector<std::string> undefines;

    /// If non-null, each macro expansion is written here in addition to the
    /// normal output.  The caller owns the stream.
    std::ostream* macro_expansion_log{nullptr};

    /// Load #included files into memory eagerly (default) or lazily.
    bool eager_load{true};

    /// When true, process top-level translation units concurrently.
    bool parallel{false};

    /// Retain comment tokens in output (default: strip them).
    bool keep_comments{false};

    /// Emit #line markers in output (GCC-style line markers).
    bool emit_line_markers{true};

    /// Target standard (informational, used to enable C23 extensions).
    std::string std_version{"c23"};
};

/// Run the C23 preprocessor on \p input_files, writing the result to \p out.
/// Diagnostics are forwarded to \p on_diag.
///
/// When PPOptions::parallel is true and multiple files are supplied, each
/// file's translation unit is processed on its own std::async task; outputs
/// are serialised in order.
///
/// Returns true on success (no error-level diagnostics).
bool preprocess(
    const std::vector<std::filesystem::path>& input_files,
    std::ostream&                             out,
    const PPOptions&                          opts,
    StringPool&                               pool,
    DiagHandler                               on_diag = {});

/// Lower-level entry point: preprocess a single SourceBuffer.
/// Included files are resolved through \p sm.
bool preprocess_buffer(
    const SourceBuffer& buf,
    std::ostream&       out,
    const PPOptions&    opts,
    SourceManager&      sm,
    DiagHandler         on_diag = {});

} // namespace c23pp

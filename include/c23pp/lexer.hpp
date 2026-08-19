#pragma once
// lexer.hpp — C23 preprocessing-token lexer built on cpp-peglib.
//
// The Lexer converts a SourceBuffer into a flat vector of Tokens.
// It handles:
//   - Trigraph removal (C23 still disallows them via translation phase 1 but
//     we accept them for compatibility and emit a warning).
//   - Line-splicing (phase 2).
//   - Tokenisation (phase 3) using a PEG grammar.
//   - Comment stripping (replaced by a single space token).

#include <c23pp/string_pool.hpp>
#include <c23pp/token.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace c23pp {

class SourceBuffer;

/// Diagnostic severity
enum class DiagLevel { Note, Warning, Error, Fatal };

/// A diagnostic emitted during lexing or preprocessing.
struct Diagnostic {
    DiagLevel     level;
    SourceLocation loc;
    std::string   message;
};

using DiagHandler = std::function<void(const Diagnostic&)>;

/// Tokenise a single SourceBuffer.
/// Returns the list of tokens (including Whitespace and Newline tokens so
/// that the preprocessor can reconstruct lines exactly).
[[nodiscard]] std::vector<Token>
lex(const SourceBuffer& buf,
    StringPool&         pool,
    DiagHandler         on_diag = {});

/// Convenience: lex a raw string (useful for tests and macro re-expansion).
[[nodiscard]] std::vector<Token>
lex_string(std::string_view src,
           std::string_view filename,
           StringPool&      pool,
           DiagHandler      on_diag = {});

} // namespace c23pp

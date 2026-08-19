#pragma once
// token.hpp — C23 preprocessing token types and the Token struct.

#include <c23pp/string_pool.hpp>
#include <cstdint>
#include <string_view>

namespace c23pp {

/// Preprocessing token kinds (C23 §6.4).
enum class TokenKind : std::uint8_t {
    // ---- Structural ----
    Eof = 0,
    Newline,        ///< logical newline (line-ending)
    Whitespace,     ///< run of horizontal whitespace

    // ---- Comments (retained optionally) ----
    LineComment,
    BlockComment,

    // ---- Literals ----
    HeaderName,     ///< <foo.h> or "foo.h" (only after #include)
    Identifier,     ///< identifiers and keywords
    PPNumber,       ///< preprocessing-number
    CharConst,      ///< character-constant  'x', L'x', u'x', U'x', u8'x'
    StringLiteral,  ///< string-literal  "s", L"s", u"s", U"s", u8"s"

    // ---- Punctuators ----
    // Single-char punctuators are stored inline; complex ones get these kinds.
    HashHash,       ///< ##
    DotDotDot,      ///< ...
    ArrowOp,        ///< ->
    Increment,      ///< ++
    Decrement,      ///< --
    ShiftLeft,      ///< <<
    ShiftRight,     ///< >>
    LessEq,         ///< <=
    GreaterEq,      ///< >=
    EqualEq,        ///< ==
    NotEq,          ///< !=
    AndAnd,         ///< &&
    OrOr,           ///< ||
    PlusEq,         ///< +=
    MinusEq,        ///< -=
    StarEq,         ///< *=
    SlashEq,        ///< /=
    PercentEq,      ///< %=
    AmpEq,          ///< &=
    PipeEq,         ///< |=
    CaretEq,        ///< ^=
    ShiftLeftEq,    ///< <<=
    ShiftRightEq,   ///< >>=
    ColonColon,     ///< ::  (C23 _BitInt syntax)
    Punctuator,     ///< any other single-character punctuator

    // ---- Preprocessor-specific ----
    Hash,           ///< # at start of directive line
    Invalid,        ///< unrecognised character
};

/// Source location (file, line, column).
struct SourceLocation {
    std::string_view filename;   ///< interned filename
    std::uint32_t    line{1};
    std::uint32_t    col{1};
};

/// A single preprocessing token.
struct Token {
    TokenKind      kind{TokenKind::Invalid};
    std::string_view spelling;  ///< interned spelling (stable storage)
    SourceLocation   loc;
    bool             leading_space{false}; ///< preceded by whitespace

    [[nodiscard]] bool is(TokenKind k) const noexcept { return kind == k; }
    [[nodiscard]] bool is_eof() const noexcept { return kind == TokenKind::Eof; }
    [[nodiscard]] bool is_newline() const noexcept { return kind == TokenKind::Newline; }
    [[nodiscard]] bool is_identifier() const noexcept { return kind == TokenKind::Identifier; }
    [[nodiscard]] bool is_identifier(std::string_view name) const noexcept {
        return kind == TokenKind::Identifier && spelling == name;
    }
};

} // namespace c23pp

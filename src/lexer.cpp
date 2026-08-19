// lexer.cpp — C23 preprocessing-token lexer using cpp-peglib.
//
// The PEG grammar follows the C23 standard (N3096) §6.4 closely.
// Phase 1 (trigraph removal) and phase 2 (line-splicing) are handled as a
// pre-pass before the PEG grammar runs.

#include <c23pp/lexer.hpp>
#include <c23pp/source_manager.hpp>

// cpp-peglib (vendored)
#include <peglib.h>

#include <algorithm>
#include <cassert>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace c23pp {

// ---------------------------------------------------------------------------
// Phase 1 & 2 — trigraph removal and line-splicing
// ---------------------------------------------------------------------------

/// Replace trigraph sequences with their equivalents (C23 still defines them
/// as translation phase 1).
static void remove_trigraphs(std::string& src) {
    // Trigraph replacement table.
    // Stored as character triples to avoid the compiler's own trigraph expansion.
    static constexpr struct { char a, b, c, rep; } table[] = {
        {'?','?','=',  '#'}, {'?','?','(',  '['}, {'?','?','/', '\\'},
        {'?','?',')',  ']'}, {'?','?','\'', '^'}, {'?','?','<', '{' },
        {'?','?','!',  '|'}, {'?','?','>',  '}'}, {'?','?','-', '~' },
    };
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ) {
        bool replaced = false;
        if (i + 2 < src.size() && src[i] == '?' && src[i+1] == '?') {
            for (auto& t : table) {
                if (src[i+2] == t.c) {
                    out += t.rep;
                    i += 3;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) out += src[i++];
    }
    src = std::move(out);
}

/// Splice lines: remove backslash-newline pairs (phase 2).
static void splice_lines(std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ) {
        if (src[i] == '\\' && i + 1 < src.size() && src[i+1] == '\n') {
            i += 2;
        } else if (src[i] == '\\' && i + 2 < src.size() &&
                   src[i+1] == '\r' && src[i+2] == '\n') {
            i += 3;
        } else {
            out += src[i++];
        }
    }
    src = std::move(out);
}

// ---------------------------------------------------------------------------
// PEG grammar for C23 preprocessing tokens (phase 3)
// ---------------------------------------------------------------------------

static const char* C23_PP_GRAMMAR = R"(
# C23 Preprocessing-token grammar (cpp-peglib PEG)
#
# Comments MUST be tried before PPToken so that '/*' is not consumed
# as the Punctuator '/' followed by '*'.
#
# HeaderName is NOT included here; the preprocessor handles it
# contextually (only after #include).

TranslationUnit <- (LineComment / BlockComment / PPToken / Whitespace / Newline / Invalid)*

PPToken         <- PPNumber / CharConst / StringLit / Identifier / Punctuator

# Whitespace (horizontal)
Whitespace      <- [ \t\f\v]+

# Newline
Newline         <- '\r\n' / '\r' / '\n'

# Line comment
LineComment     <- '//' (!'\n' .)*

# Block comment
BlockComment    <- '/*' (!'*/' .)* '*/'

# Preprocessing number
PPNumber        <- ('.'? [0-9]) (PPNumSuffix)*
PPNumSuffix     <- [eEpP] [+\-] / [a-zA-Z0-9_.] / '\''  [a-zA-Z0-9]

# Character constants
CharConst       <- CharPrefix? "'" CharBody+ "'"
CharBody        <- '\\' . / [^'\\\n]
CharPrefix      <- 'u8' / [uUL]

# String literals
StringLit       <- StrPrefix? '"' StrBody* '"'
StrBody         <- '\\' . / [^"\\\n]
StrPrefix       <- 'u8' / [uUL]

# Identifier (C23 allows universal character names and extended chars)
Identifier      <- [a-zA-Z_$] [a-zA-Z0-9_$]*

# Punctuators (ordered longest-first so PEG picks the right one)
Punctuator      <- '##' / '...' / '->' / '++' / '--' / '<<=' / '>>='
                 / '<<' / '>>' / '<=' / '>=' / '==' / '!=' / '&&' / '||'
                 / '+=' / '-=' / '*=' / '/=' / '%=' / '&=' / '|=' / '^='
                 / '::' / '#'
                 / [{}()\[\];:,?.~!%^&*\-+=/|<>@"]

# Fall-through for unrecognised bytes
Invalid         <- .
)";

// ---------------------------------------------------------------------------
// Token-kind mapping from PEG rule names
// ---------------------------------------------------------------------------

static TokenKind kind_from_rule(std::string_view rule) {
    if (rule == "Identifier")  return TokenKind::Identifier;
    if (rule == "PPNumber")    return TokenKind::PPNumber;
    if (rule == "CharConst")   return TokenKind::CharConst;
    if (rule == "StringLit")   return TokenKind::StringLiteral;
    if (rule == "HeaderName")  return TokenKind::HeaderName;
    if (rule == "Whitespace")  return TokenKind::Whitespace;
    if (rule == "Newline")     return TokenKind::Newline;
    if (rule == "LineComment") return TokenKind::LineComment;
    if (rule == "BlockComment")return TokenKind::BlockComment;
    if (rule == "Invalid")     return TokenKind::Invalid;
    if (rule == "Punctuator") {
        // Resolved later in build_tokens
        return TokenKind::Punctuator;
    }
    return TokenKind::Invalid;
}

/// Map multi-char punctuator spellings to specific TokenKind values.
static TokenKind punctuator_kind(std::string_view sp) {
    if (sp == "##")  return TokenKind::HashHash;
    if (sp == "...") return TokenKind::DotDotDot;
    if (sp == "->")  return TokenKind::ArrowOp;
    if (sp == "++")  return TokenKind::Increment;
    if (sp == "--")  return TokenKind::Decrement;
    if (sp == "<<")  return TokenKind::ShiftLeft;
    if (sp == ">>")  return TokenKind::ShiftRight;
    if (sp == "<=")  return TokenKind::LessEq;
    if (sp == ">=")  return TokenKind::GreaterEq;
    if (sp == "==")  return TokenKind::EqualEq;
    if (sp == "!=")  return TokenKind::NotEq;
    if (sp == "&&")  return TokenKind::AndAnd;
    if (sp == "||")  return TokenKind::OrOr;
    if (sp == "+=")  return TokenKind::PlusEq;
    if (sp == "-=")  return TokenKind::MinusEq;
    if (sp == "*=")  return TokenKind::StarEq;
    if (sp == "/=")  return TokenKind::SlashEq;
    if (sp == "%=")  return TokenKind::PercentEq;
    if (sp == "&=")  return TokenKind::AmpEq;
    if (sp == "|=")  return TokenKind::PipeEq;
    if (sp == "^=")  return TokenKind::CaretEq;
    if (sp == "<<=") return TokenKind::ShiftLeftEq;
    if (sp == ">>=") return TokenKind::ShiftRightEq;
    if (sp == "::")  return TokenKind::ColonColon;
    if (sp == "#")   return TokenKind::Hash;
    return TokenKind::Punctuator;
}

// ---------------------------------------------------------------------------
// Core lex() implementation
// ---------------------------------------------------------------------------

std::vector<Token> lex(const SourceBuffer& buf,
                       StringPool&         pool,
                       DiagHandler         on_diag) {
    return lex_string(buf.content(), buf.filename(), pool, std::move(on_diag));
}

std::vector<Token> lex_string(std::string_view src,
                               std::string_view filename,
                               StringPool&      pool,
                               DiagHandler      on_diag) {
    // Apply phase 1 & 2 to a working copy.
    std::string work{src};
    remove_trigraphs(work);
    splice_lines(work);
    src = work; // use the spliced source from here on
    // Build (and cache) the PEG parser.
    // We deliberately construct one per call; for production use the parser
    // could be a static thread_local.
    static thread_local peg::parser parser;
    static thread_local bool        parser_ok = false;
    if (!parser_ok) {
        parser.set_logger([](std::size_t /*ln*/, std::size_t /*col*/,
                             const std::string& msg, const std::string& /*rule*/) {
            // Grammar errors are programming bugs — just ignore.
            (void)msg;
        });
        parser_ok = parser.load_grammar(C23_PP_GRAMMAR);
        (void)parser_ok; // grammar is fixed; assertion in debug builds
        assert(parser_ok && "C23 PEG grammar failed to compile");
    }

    std::vector<Token> tokens;
    tokens.reserve(src.size() / 4 + 8);

    std::string_view interned_file = pool.intern(filename);

    // Build a simple line-start table for the (possibly spliced) src.
    std::vector<std::size_t> line_starts;
    line_starts.push_back(0);
    for (std::size_t i = 0; i < src.size(); ++i)
        if (src[i] == '\n') line_starts.push_back(i + 1);

    auto loc_at = [&](std::size_t off) -> SourceLocation {
        auto it = std::upper_bound(line_starts.begin(), line_starts.end(), off);
        --it;
        std::size_t line = static_cast<std::size_t>(it - line_starts.begin()) + 1;
        std::size_t col  = off - *it + 1;
        return {interned_file,
                static_cast<std::uint32_t>(line),
                static_cast<std::uint32_t>(col)};
    };

    // Install action on the top-level rule to collect tokens.
    // We attach actions to every leaf-level rule.
    auto collect = [&](std::string_view rule_name) {
        parser[rule_name.data()] = [&, rule_name](const peg::SemanticValues& sv) {
            std::string_view text{sv.sv()};
            TokenKind kind = kind_from_rule(rule_name);
            if (kind == TokenKind::Punctuator)
                kind = punctuator_kind(text);
            Token tok;
            tok.kind     = kind;
            tok.spelling = pool.intern(text);
            tok.loc      = loc_at(static_cast<std::size_t>(sv.sv().data() - src.data()));
            tokens.push_back(tok);
        };
    };

    for (auto rule : {"Identifier","PPNumber","CharConst","StringLit",
                       "Whitespace","Newline",
                       "LineComment","BlockComment","Punctuator","Invalid"}) {
        collect(rule);
    }

    // Run the parser.
    if (!parser.parse(src)) {
        if (on_diag) {
            on_diag({DiagLevel::Error,
                     {interned_file, 1, 1},
                     "PEG parse error during tokenisation"});
        }
    }

    // Post-process: mark leading_space and fixup leading # on directive lines.
    bool at_line_start = true;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        auto& t = tokens[i];
        if (t.kind == TokenKind::Whitespace) {
            // propagate to next non-WS token
            if (i + 1 < tokens.size())
                tokens[i+1].leading_space = true;
            continue;
        }
        if (t.kind == TokenKind::Newline) {
            at_line_start = true;
            continue;
        }
        if (at_line_start && t.kind == TokenKind::Hash)
            t.kind = TokenKind::Hash; // already correct
        at_line_start = false;
    }

    // Append EOF sentinel.
    Token eof;
    eof.kind     = TokenKind::Eof;
    eof.spelling = pool.intern("");
    eof.loc      = loc_at(src.size());
    tokens.push_back(eof);

    return tokens;
}

} // namespace c23pp

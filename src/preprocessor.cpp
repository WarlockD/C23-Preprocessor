// preprocessor.cpp — C23 macro preprocessor.

#include <c23pp/preprocessor.hpp>

#include <c23pp/lexer.hpp>
#include <c23pp/source_manager.hpp>
#include <c23pp/string_pool.hpp>
#include <c23pp/token.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <ctime>
#include <format>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <numeric>
#include <optional>
#include <ranges>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace c23pp {

// ---------------------------------------------------------------------------
// Macro definition
// ---------------------------------------------------------------------------

struct MacroDef {
    bool is_function_like{false};
    bool is_variadic{false};
    std::vector<std::string_view> params; ///< interned parameter names
    std::vector<Token>            body;   ///< replacement list
};

// ---------------------------------------------------------------------------
// Preprocessor state
// ---------------------------------------------------------------------------

class PPState {
public:
    explicit PPState(const PPOptions& opts,
                     SourceManager&   sm,
                     std::ostream&    out,
                     DiagHandler      on_diag)
        : opts_{opts}, sm_{sm}, out_{out}, on_diag_{std::move(on_diag)} {
        init_predefined();
        apply_cmdline_defines();
    }

    bool run(const SourceBuffer& buf);

private:
    // --- Helpers ---
    void emit(std::string_view s) { out_ << s; }
    void emit_token(const Token& t) {
        if (t.leading_space) out_ << ' ';
        out_ << t.spelling;
    }
    void emit_line_marker(const SourceLocation& loc, int flags = 0) {
        if (opts_.emit_line_markers)
            out_ << std::format("\n# {} \"{}\" {}\n",
                                loc.line, loc.filename, flags);
    }

    void diag(DiagLevel lvl, const SourceLocation& loc, std::string msg) {
        if (on_diag_) on_diag_({lvl, loc, std::move(msg)});
        if (lvl == DiagLevel::Fatal)
            throw std::runtime_error("fatal preprocessor error");
    }

    // --- Macro table ---
    void define(std::string_view name, MacroDef def) {
        macros_[name] = std::move(def);
    }
    bool undef(std::string_view name) { return macros_.erase(name) > 0; }
    bool is_defined(std::string_view name) const {
        return macros_.contains(name);
    }
    const MacroDef* lookup(std::string_view name) const {
        auto it = macros_.find(name);
        return (it != macros_.end()) ? &it->second : nullptr;
    }

    void init_predefined();
    void apply_cmdline_defines();

    // --- Token stream helpers ---
    struct TokStream {
        const std::vector<Token>& toks;
        std::size_t               pos{0};
        [[nodiscard]] const Token& peek(std::size_t ahead = 0) const {
            std::size_t idx = pos + ahead;
            return (idx < toks.size()) ? toks[idx] : toks.back(); // EOF
        }
        Token consume() {
            return (pos < toks.size()) ? toks[pos++] : toks.back();
        }
        bool at_end() const {
            return pos >= toks.size() || toks[pos].is_eof();
        }
        void skip_ws() {
            while (!at_end() && (peek().kind == TokenKind::Whitespace ||
                                  peek().kind == TokenKind::LineComment ||
                                  peek().kind == TokenKind::BlockComment))
                ++pos;
        }
        void skip_ws_nonl() {
            while (!at_end() && peek().kind == TokenKind::Whitespace)
                ++pos;
        }
    };

    // Collect tokens until end-of-line (does not consume the newline token).
    static std::vector<Token>
    collect_line(TokStream& ts) {
        std::vector<Token> line;
        while (!ts.at_end() && !ts.peek().is_newline())
            line.push_back(ts.consume());
        return line;
    }

    // --- Directive handlers ---
    bool process_line(TokStream& ts, std::string_view filename);
    void handle_define(TokStream& ts, const SourceLocation& loc);
    void handle_undef(TokStream& ts, const SourceLocation& loc);
    void handle_include(TokStream& ts, const SourceLocation& loc,
                        bool include_next);
    void handle_if(TokStream& ts, const SourceLocation& loc);
    void handle_ifdef(TokStream& ts, const SourceLocation& loc, bool is_ndef);
    void handle_elif(TokStream& ts, const SourceLocation& loc);
    void handle_elifdef(TokStream& ts, const SourceLocation& loc, bool is_ndef);
    void handle_else(const SourceLocation& loc);
    void handle_endif(const SourceLocation& loc);
    void handle_line(TokStream& ts, const SourceLocation& loc);
    void handle_error_warning(TokStream& ts, const SourceLocation& loc,
                               bool is_error);
    void handle_pragma(TokStream& ts, const SourceLocation& loc);

    // --- Conditional stack ---
    struct CondState {
        bool condition;   ///< was the current branch taken?
        bool ever_true;   ///< has any branch been taken so far?
        bool in_else;     ///< have we seen an #else?
    };
    std::vector<CondState> cond_stack_;
    [[nodiscard]] bool skipping() const {
        return !cond_stack_.empty() && !cond_stack_.back().condition;
    }

    // --- Macro expansion ---
    std::vector<Token> expand(std::vector<Token> toks,
                               std::unordered_set<std::string_view>& hide_set);
    std::vector<Token> expand_macro(const Token&           name_tok,
                                     const MacroDef&        def,
                                     TokStream&             ts,
                                     std::unordered_set<std::string_view>& hide);
    std::vector<Token> subst(const MacroDef&        def,
                              const std::vector<std::vector<Token>>& args,
                              const std::unordered_set<std::string_view>& outer_hide);

    static std::string stringify(const std::vector<Token>& toks);
    static std::vector<Token> paste(std::vector<Token> lhs,
                                     const std::vector<Token>& rhs,
                                     StringPool& pool);

    // --- Constant expression evaluation (for #if) ---
    std::int64_t eval_expr(std::vector<Token> toks, const SourceLocation& loc);

    const PPOptions& opts_;
    SourceManager&   sm_;
    std::ostream&    out_;
    DiagHandler      on_diag_;

    std::unordered_map<std::string_view, MacroDef> macros_;
    std::string current_file_;
};

// ---------------------------------------------------------------------------
// Pre-defined macros
// ---------------------------------------------------------------------------

void PPState::init_predefined() {
    // __STDC__
    {MacroDef d; d.body.push_back({TokenKind::PPNumber, sm_.pool().intern("1"), {}}); define(sm_.pool().intern("__STDC__"), d);}
    // __STDC_VERSION__ — C23 = 202311L
    {MacroDef d; d.body.push_back({TokenKind::PPNumber, sm_.pool().intern("202311L"), {}}); define(sm_.pool().intern("__STDC_VERSION__"), d);}
    // __STDC_HOSTED__
    {MacroDef d; d.body.push_back({TokenKind::PPNumber, sm_.pool().intern("1"), {}}); define(sm_.pool().intern("__STDC_HOSTED__"), d);}

    // __DATE__ and __TIME__
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    char date_buf[32], time_buf[32];
    std::strftime(date_buf, sizeof(date_buf), "\"%b %e %Y\"", &tm_buf);
    std::strftime(time_buf, sizeof(time_buf), "\"%H:%M:%S\"", &tm_buf);
    {MacroDef d; d.body.push_back({TokenKind::StringLiteral, sm_.pool().intern(date_buf), {}}); define(sm_.pool().intern("__DATE__"), d);}
    {MacroDef d; d.body.push_back({TokenKind::StringLiteral, sm_.pool().intern(time_buf), {}}); define(sm_.pool().intern("__TIME__"), d);}
}

void PPState::apply_cmdline_defines() {
    for (auto& [name, value] : opts_.defines) {
        // Lex the value string.
        std::vector<Token> body = lex_string(value, "<cmdline>", sm_.pool());
        // Strip trailing EOF.
        if (!body.empty() && body.back().is_eof()) body.pop_back();
        MacroDef def;
        def.body = std::move(body);
        define(sm_.pool().intern(name), std::move(def));
    }
    for (auto& name : opts_.undefines)
        undef(sm_.pool().intern(name));
}

// ---------------------------------------------------------------------------
// Main run loop
// ---------------------------------------------------------------------------

bool PPState::run(const SourceBuffer& src_buf) {
    current_file_ = std::string(src_buf.filename());
    auto tokens   = lex(src_buf, sm_.pool(), on_diag_);
    TokStream ts{tokens};

    emit_line_marker({sm_.pool().intern(current_file_), 1, 1}, 1);

    while (!ts.at_end()) {
        if (skipping()) {
            // Still need to track nested #if/#endif while skipping.
            ts.skip_ws_nonl();
            if (ts.peek().kind == TokenKind::Hash) {
                ts.consume();
                ts.skip_ws_nonl();
                if (!ts.peek().is_identifier()) {
                    collect_line(ts);
                    continue;
                }
                auto dir = ts.consume().spelling;
                if (dir == "if" || dir == "ifdef" || dir == "ifndef")
                    cond_stack_.push_back({false, false, false});
                else if (dir == "elif") handle_elif(ts, ts.peek().loc);
                else if (dir == "elifdef")  handle_elifdef(ts, ts.peek().loc, false);
                else if (dir == "elifndef") handle_elifdef(ts, ts.peek().loc, true);
                else if (dir == "else")  handle_else(ts.peek().loc);
                else if (dir == "endif") handle_endif(ts.peek().loc);
                collect_line(ts);
                if (!ts.at_end() && ts.peek().is_newline()) ts.consume();
                continue;
            }
            // Non-directive line — skip entire line.
            while (!ts.at_end() && !ts.peek().is_newline()) ts.consume();
            if (!ts.at_end()) ts.consume(); // newline
            continue;
        }

        process_line(ts, src_buf.filename());
    }
    return true;
}

bool PPState::process_line(TokStream& ts, std::string_view /*filename*/) {
    ts.skip_ws_nonl();
    if (ts.at_end()) return true;

    // Directive line?
    if (ts.peek().kind == TokenKind::Hash) {
        ts.consume(); // consume #
        ts.skip_ws_nonl();

        if (ts.peek().is_newline() || ts.at_end()) {
            // null directive
            if (!ts.at_end()) ts.consume();
            return true;
        }

        if (!ts.peek().is_identifier()) {
            diag(DiagLevel::Error, ts.peek().loc,
                 std::format("unknown preprocessor directive"));
            collect_line(ts);
            if (!ts.at_end() && ts.peek().is_newline()) ts.consume();
            return false;
        }

        auto dir_tok = ts.consume();
        auto dir     = dir_tok.spelling;
        ts.skip_ws_nonl();

        if (dir == "include")      handle_include(ts, dir_tok.loc, false);
        else if (dir == "include_next") handle_include(ts, dir_tok.loc, true);
        else if (dir == "define")  handle_define(ts, dir_tok.loc);
        else if (dir == "undef")   handle_undef(ts, dir_tok.loc);
        else if (dir == "if")      handle_if(ts, dir_tok.loc);
        else if (dir == "ifdef")   handle_ifdef(ts, dir_tok.loc, false);
        else if (dir == "ifndef")  handle_ifdef(ts, dir_tok.loc, true);
        else if (dir == "elif")    handle_elif(ts, dir_tok.loc);
        else if (dir == "elifdef") handle_elifdef(ts, dir_tok.loc, false);
        else if (dir == "elifndef")handle_elifdef(ts, dir_tok.loc, true);
        else if (dir == "else")    handle_else(dir_tok.loc);
        else if (dir == "endif")   handle_endif(dir_tok.loc);
        else if (dir == "line")    handle_line(ts, dir_tok.loc);
        else if (dir == "error")   handle_error_warning(ts, dir_tok.loc, true);
        else if (dir == "warning") handle_error_warning(ts, dir_tok.loc, false);
        else if (dir == "pragma")  handle_pragma(ts, dir_tok.loc);
        else {
            diag(DiagLevel::Error, dir_tok.loc,
                 std::format("unknown directive '#{}'", dir));
        }

        collect_line(ts); // discard anything left on line
        if (!ts.at_end() && ts.peek().is_newline()) {
            emit("\n");
            ts.consume();
        }
        return true;
    }

    // Normal code line — collect, expand macros, emit.
    std::vector<Token> line;
    while (!ts.at_end() && !ts.peek().is_newline()) {
        auto tok = ts.consume();
        if (tok.kind == TokenKind::Whitespace || tok.kind == TokenKind::LineComment
            || tok.kind == TokenKind::BlockComment) {
            if (!line.empty()) {
                Token ws_tok;
                ws_tok.kind = TokenKind::Whitespace;
                ws_tok.spelling = sm_.pool().intern(" ");
                ws_tok.leading_space = false;
                line.back().leading_space = false;
                line.push_back(ws_tok);
            }
            continue;
        }
        line.push_back(tok);
    }

    std::unordered_set<std::string_view> hide;
    auto expanded = expand(std::move(line), hide);

    bool first = true;
    for (auto& t : expanded) {
        if (t.kind == TokenKind::Whitespace) { emit(" "); continue; }
        if (t.kind == TokenKind::Eof) break;
        if (!first && t.leading_space) emit(" ");
        emit(t.spelling);
        first = false;
    }

    if (!ts.at_end() && ts.peek().is_newline()) {
        emit("\n");
        ts.consume();
    }
    return true;
}

// ---------------------------------------------------------------------------
// #define
// ---------------------------------------------------------------------------

void PPState::handle_define(TokStream& ts, const SourceLocation& loc) {
    if (!ts.peek().is_identifier()) {
        diag(DiagLevel::Error, loc, "#define requires an identifier");
        return;
    }
    auto name = ts.consume().spelling;

    MacroDef def;
    // Function-like if '(' immediately follows name (no space).
    if (ts.peek().kind == TokenKind::Punctuator && ts.peek().spelling == "("
        && !ts.peek().leading_space) {
        def.is_function_like = true;
        ts.consume(); // '('
        // Parse parameter list.
        while (!ts.at_end() && ts.peek().spelling != ")") {
            ts.skip_ws_nonl();
            if (ts.peek().spelling == "...") {
                def.is_variadic = true;
                ts.consume();
                ts.skip_ws_nonl();
                break;
            }
            if (!ts.peek().is_identifier()) {
                diag(DiagLevel::Error, ts.peek().loc,
                     "expected parameter name in macro");
                return;
            }
            def.params.push_back(ts.consume().spelling);
            ts.skip_ws_nonl();
            if (ts.peek().spelling == ",") ts.consume();
        }
        if (ts.peek().spelling == ")") ts.consume();
        ts.skip_ws_nonl();
    } else if (ts.peek().kind == TokenKind::Whitespace) {
        ts.consume(); // single space after name
    }

    // Collect replacement list (up to newline).
    def.body = collect_line(ts);

    // Trim trailing whitespace from body.
    while (!def.body.empty() && def.body.back().kind == TokenKind::Whitespace)
        def.body.pop_back();

    define(name, std::move(def));

    // Log if expansion log requested.
    if (opts_.macro_expansion_log) {
        *opts_.macro_expansion_log << std::format("/* defined: {} */\n", name);
    }
}

// ---------------------------------------------------------------------------
// #undef
// ---------------------------------------------------------------------------

void PPState::handle_undef(TokStream& ts, const SourceLocation& loc) {
    if (!ts.peek().is_identifier()) {
        diag(DiagLevel::Error, loc, "#undef requires an identifier");
        return;
    }
    auto name = ts.consume().spelling;
    undef(name);
}

// ---------------------------------------------------------------------------
// #include
// ---------------------------------------------------------------------------

void PPState::handle_include(TokStream& ts, const SourceLocation& loc,
                              bool /*include_next*/) {
    ts.skip_ws_nonl();
    std::string header_str;
    bool        is_system = false;

    if (ts.peek().kind == TokenKind::StringLiteral) {
        // "foo.h" — already a string literal from the lexer.
        auto tok   = ts.consume();
        auto sp    = tok.spelling;
        header_str = std::string(sp.substr(1, sp.size() - 2));
        is_system  = false;
    } else if (ts.peek().kind == TokenKind::Punctuator && ts.peek().spelling == "<") {
        // <foo.h> — reconstruct from tokens up to >.
        ts.consume(); // consume '<'
        is_system = true;
        int depth = 1;
        while (!ts.at_end() && !ts.peek().is_newline() && depth > 0) {
            auto t = ts.consume();
            if (t.spelling == "<") { ++depth; header_str += '<'; }
            else if (t.spelling == ">") {
                --depth;
                if (depth > 0) header_str += '>';
            } else {
                header_str += t.spelling;
            }
        }
    } else {
        // Macro-expand the rest of the line and re-lex.
        auto rest = collect_line(ts);
        std::unordered_set<std::string_view> hide;
        auto expanded = expand(std::move(rest), hide);
        // Rebuild string.
        std::string rebuilt;
        for (auto& t : expanded)
            if (t.kind != TokenKind::Whitespace && !t.is_eof())
                rebuilt += t.spelling;
        if (!rebuilt.empty() && rebuilt[0] == '"') {
            is_system  = false;
            header_str = rebuilt.substr(1, rebuilt.size() - 2);
        } else if (!rebuilt.empty() && rebuilt[0] == '<') {
            is_system  = true;
            header_str = rebuilt.substr(1, rebuilt.rfind('>') - 1);
        } else {
            diag(DiagLevel::Error, loc,
                 std::format("malformed #include '{}'", rebuilt));
            return;
        }
    }

    // Build combined search path.
    auto search = is_system ? opts_.system_include_paths : opts_.include_paths;
    auto all_sys = opts_.system_include_paths;
    search.insert(search.end(), all_sys.begin(), all_sys.end());

    auto found = sm_.find_include(header_str, search, is_system);
    if (!found) {
        diag(DiagLevel::Error, loc,
             std::format("file not found: '{}'", header_str));
        return;
    }

    auto included = sm_.get_or_load(*found);
    if (!included) return;

    // Save current file, recurse, restore.
    auto saved_file = current_file_;
    emit_line_marker({sm_.pool().intern(included->filename()), 1, 1}, 1);
    run(*included);
    emit_line_marker({sm_.pool().intern(saved_file), loc.line + 1, 1}, 2);
    current_file_ = saved_file;
}

// ---------------------------------------------------------------------------
// #if / #ifdef / #ifndef
// ---------------------------------------------------------------------------

void PPState::handle_if(TokStream& ts, const SourceLocation& loc) {
    auto expr_toks = collect_line(ts);
    // Macro-expand the expression.
    std::unordered_set<std::string_view> hide;
    auto expanded = expand(std::move(expr_toks), hide);
    auto val      = eval_expr(std::move(expanded), loc);
    cond_stack_.push_back({val != 0, val != 0, false});
}

void PPState::handle_ifdef(TokStream& ts, const SourceLocation& loc,
                            bool is_ndef) {
    ts.skip_ws_nonl();
    if (!ts.peek().is_identifier()) {
        diag(DiagLevel::Error, loc, "#ifdef requires an identifier");
        cond_stack_.push_back({false, false, false});
        return;
    }
    auto name = ts.consume().spelling;
    bool val  = is_defined(name) ^ is_ndef;
    cond_stack_.push_back({val, val, false});
}

void PPState::handle_elif(TokStream& ts, const SourceLocation& loc) {
    if (cond_stack_.empty()) {
        diag(DiagLevel::Error, loc, "#elif without #if");
        return;
    }
    auto& cs = cond_stack_.back();
    if (cs.in_else) {
        diag(DiagLevel::Error, loc, "#elif after #else");
        return;
    }
    if (cs.ever_true) {
        cs.condition = false;
        collect_line(ts);
        return;
    }
    auto expr_toks = collect_line(ts);
    std::unordered_set<std::string_view> hide;
    auto expanded = expand(std::move(expr_toks), hide);
    auto val      = eval_expr(std::move(expanded), loc);
    cs.condition  = (val != 0);
    cs.ever_true  = cs.condition;
}

void PPState::handle_elifdef(TokStream& ts, const SourceLocation& loc,
                              bool is_ndef) {
    if (cond_stack_.empty()) {
        diag(DiagLevel::Error, loc, "#elifdef without #if");
        return;
    }
    auto& cs = cond_stack_.back();
    if (cs.in_else) {
        diag(DiagLevel::Error, loc, "#elifdef after #else");
        return;
    }
    if (cs.ever_true) { cs.condition = false; return; }
    ts.skip_ws_nonl();
    if (!ts.peek().is_identifier()) {
        diag(DiagLevel::Error, loc, "#elifdef requires an identifier");
        return;
    }
    auto name    = ts.consume().spelling;
    bool val     = is_defined(name) ^ is_ndef;
    cs.condition = val;
    cs.ever_true = val;
}

void PPState::handle_else(const SourceLocation& loc) {
    if (cond_stack_.empty()) {
        diag(DiagLevel::Error, loc, "#else without #if");
        return;
    }
    auto& cs = cond_stack_.back();
    if (cs.in_else) {
        diag(DiagLevel::Error, loc, "multiple #else");
        return;
    }
    cs.in_else   = true;
    cs.condition = !cs.ever_true;
}

void PPState::handle_endif(const SourceLocation& loc) {
    if (cond_stack_.empty()) {
        diag(DiagLevel::Error, loc, "#endif without #if");
        return;
    }
    cond_stack_.pop_back();
}

// ---------------------------------------------------------------------------
// #line
// ---------------------------------------------------------------------------

void PPState::handle_line(TokStream& ts, const SourceLocation& loc) {
    auto toks = collect_line(ts);
    std::unordered_set<std::string_view> hide;
    auto expanded = expand(std::move(toks), hide);
    // First token should be a number.
    std::size_t i = 0;
    while (i < expanded.size() && expanded[i].kind == TokenKind::Whitespace) ++i;
    if (i >= expanded.size() || expanded[i].kind != TokenKind::PPNumber) {
        diag(DiagLevel::Error, loc, "invalid #line directive");
        return;
    }
    std::uint32_t lineno = 0;
    try { lineno = static_cast<std::uint32_t>(std::stoul(std::string(expanded[i].spelling))); }
    catch (...) { diag(DiagLevel::Error, loc, "invalid line number in #line"); return; }
    ++i;
    while (i < expanded.size() && expanded[i].kind == TokenKind::Whitespace) ++i;
    std::string file = current_file_;
    if (i < expanded.size() && expanded[i].kind == TokenKind::StringLiteral) {
        auto sp = expanded[i].spelling;
        file = std::string(sp.substr(1, sp.size() - 2));
    }
    current_file_ = file;
    emit_line_marker({sm_.pool().intern(file), lineno, 1});
}

// ---------------------------------------------------------------------------
// #error / #warning
// ---------------------------------------------------------------------------

void PPState::handle_error_warning(TokStream& ts, const SourceLocation& loc,
                                    bool is_error) {
    auto toks = collect_line(ts);
    std::string msg;
    for (auto& t : toks)
        if (t.kind != TokenKind::Whitespace) msg += t.spelling;
    diag(is_error ? DiagLevel::Error : DiagLevel::Warning, loc, msg);
}

// ---------------------------------------------------------------------------
// #pragma
// ---------------------------------------------------------------------------

void PPState::handle_pragma(TokStream& ts, const SourceLocation& /*loc*/) {
    // Emit #pragma unchanged.
    emit("#pragma");
    auto toks = collect_line(ts);
    for (auto& t : toks) emit_token(t);
}

// ---------------------------------------------------------------------------
// Macro expansion
// ---------------------------------------------------------------------------

std::vector<Token> PPState::expand(std::vector<Token>                    toks,
                                    std::unordered_set<std::string_view>& hide) {
    std::vector<Token> out;
    out.reserve(toks.size());
    TokStream ts{toks};

    while (!ts.at_end()) {
        auto tok = ts.consume();
        if (tok.kind != TokenKind::Identifier) {
            out.push_back(tok);
            continue;
        }

        // __has_include / __has_c_attribute / __has_embed / __has_extension
        if (tok.spelling == "__has_include") {
            // __has_include(<path>) or __has_include("path")
            ts.skip_ws_nonl();
            if (ts.peek().spelling == "(") {
                ts.consume();
                ts.skip_ws_nonl();
                std::string hdr;
                bool is_sys = false;
                if (ts.peek().kind == TokenKind::HeaderName) {
                    auto h = ts.consume().spelling;
                    is_sys = (h[0] == '<');
                    hdr = std::string(h.substr(1, h.size()-2));
                } else if (ts.peek().kind == TokenKind::StringLiteral) {
                    auto h = ts.consume().spelling;
                    hdr = std::string(h.substr(1, h.size()-2));
                }
                ts.skip_ws_nonl();
                if (ts.peek().spelling == ")") ts.consume();
                auto search = is_sys ? opts_.system_include_paths : opts_.include_paths;
                bool found = sm_.find_include(hdr, search, is_sys).has_value();
                Token r; r.kind = TokenKind::PPNumber;
                r.spelling = sm_.pool().intern(found ? "1" : "0");
                out.push_back(r);
                continue;
            }
            out.push_back(tok);
            continue;
        }
        if (tok.spelling == "__has_c_attribute" ||
            tok.spelling == "__has_extension" ||
            tok.spelling == "__has_embed") {
            // Always return 0 for unsupported queries.
            ts.skip_ws_nonl();
            if (ts.peek().spelling == "(") {
                int depth = 1; ts.consume();
                while (!ts.at_end() && depth > 0) {
                    auto t = ts.consume();
                    if (t.spelling == "(") ++depth;
                    else if (t.spelling == ")") --depth;
                }
            }
            Token r; r.kind = TokenKind::PPNumber;
            r.spelling = sm_.pool().intern("0");
            out.push_back(r);
            continue;
        }
        // defined(X) or defined X
        if (tok.spelling == "defined") {
            bool paren = (ts.peek().spelling == "(");
            if (paren) ts.consume();
            ts.skip_ws_nonl();
            std::string_view name;
            if (ts.peek().is_identifier()) name = ts.consume().spelling;
            if (paren) { ts.skip_ws_nonl(); if (ts.peek().spelling == ")") ts.consume(); }
            Token r; r.kind = TokenKind::PPNumber;
            r.spelling = sm_.pool().intern(is_defined(name) ? "1" : "0");
            out.push_back(r);
            continue;
        }

        // Check hide set.
        if (hide.contains(tok.spelling)) {
            out.push_back(tok);
            continue;
        }

        auto* def = lookup(tok.spelling);
        if (!def) { out.push_back(tok); continue; }

        // Function-like: need '(' next.
        if (def->is_function_like) {
            // Peek ahead (skip ws) for '('.
            std::size_t saved = ts.pos;
            ts.skip_ws_nonl();
            if (ts.peek().spelling != "(") {
                ts.pos = saved;
                out.push_back(tok);
                continue;
            }
        }

        hide.insert(tok.spelling);
        auto replacement = expand_macro(tok, *def, ts, hide);
        hide.erase(tok.spelling);

        // Recurse on replacement.
        auto re_expanded = expand(std::move(replacement), hide);
        out.insert(out.end(), re_expanded.begin(), re_expanded.end());

        // Log expansion if requested.
        if (opts_.macro_expansion_log) {
            std::string exp_str;
            for (auto& t : re_expanded)
                if (t.kind != TokenKind::Whitespace && !t.is_eof())
                    exp_str += t.spelling;
            *opts_.macro_expansion_log <<
                std::format("/* expand {} -> {} */\n", tok.spelling, exp_str);
        }
    }
    return out;
}

std::vector<Token>
PPState::expand_macro(const Token&                          name_tok,
                       const MacroDef&                       def,
                       TokStream&                            ts,
                       std::unordered_set<std::string_view>& hide) {
    if (!def.is_function_like) {
        if (def.body.empty()) return {};
        // Object-like: return body (with leading_space copied from name_tok).
        auto body = def.body;
        if (!body.empty()) body.front().leading_space = name_tok.leading_space;
        return body;
    }

    // Function-like: parse arguments.
    ts.skip_ws_nonl();
    if (ts.peek().spelling != "(") {
        // No argument list — don't expand.
        return {name_tok};
    }
    ts.consume(); // '('

    std::vector<std::vector<Token>> args;
    if (ts.peek().spelling != ")") {
        std::vector<Token> current;
        int depth = 0;
        while (!ts.at_end()) {
            auto t = ts.consume();
            if (t.spelling == "(" ) { ++depth; current.push_back(t); }
            else if (t.spelling == ")") {
                if (depth == 0) { args.push_back(std::move(current)); break; }
                --depth; current.push_back(t);
            } else if (t.spelling == "," && depth == 0) {
                args.push_back(std::move(current));
                current.clear();
            } else {
                current.push_back(t);
            }
        }
    } else {
        ts.consume(); // ')'
    }

    return subst(def, args, hide);
}

std::vector<Token>
PPState::subst(const MacroDef&                             def,
                const std::vector<std::vector<Token>>&     args,
                const std::unordered_set<std::string_view>& /*outer_hide*/) {
    std::vector<Token> result;
    auto& body = def.body;

    auto param_idx = [&](std::string_view name) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < def.params.size(); ++i)
            if (def.params[i] == name) return i;
        return std::nullopt;
    };

    auto get_arg = [&](std::size_t i) -> const std::vector<Token>& {
        static const std::vector<Token> empty;
        return (i < args.size()) ? args[i] : empty;
    };

    for (std::size_t i = 0; i < body.size(); ++i) {
        const auto& t = body[i];

        // # stringify operator
        if (t.kind == TokenKind::Hash && i + 1 < body.size() &&
            body[i+1].is_identifier()) {
            auto idx = param_idx(body[i+1].spelling);
            if (idx) {
                ++i;
                auto str = stringify(get_arg(*idx));
                Token st; st.kind = TokenKind::StringLiteral;
                st.spelling = sm_.pool().intern(str);
                st.leading_space = t.leading_space;
                result.push_back(st);
                continue;
            }
        }

        // ## token-paste operator
        if (t.kind == TokenKind::HashHash) continue; // handled below

        // Param substitution
        if (t.is_identifier()) {
            // __VA_ARGS__
            if (t.spelling == "__VA_ARGS__" && def.is_variadic) {
                bool first = true;
                for (std::size_t j = def.params.size(); j < args.size(); ++j) {
                    if (!first) {
                        Token comma; comma.kind = TokenKind::Punctuator;
                        comma.spelling = sm_.pool().intern(",");
                        result.push_back(comma);
                    }
                    result.insert(result.end(), get_arg(j).begin(), get_arg(j).end());
                    first = false;
                }
                continue;
            }
            // __VA_OPT__(tokens) — simple version: emit or skip based on VA_ARGS
            if (t.spelling == "__VA_OPT__") {
                bool has_va = (args.size() > def.params.size());
                // Skip until matching ')'.
                ++i;
                if (i < body.size() && body[i].spelling == "(") {
                    std::vector<Token> opt_toks;
                    int depth = 1; ++i;
                    while (i < body.size() && depth > 0) {
                        if (body[i].spelling == "(") ++depth;
                        else if (body[i].spelling == ")") { --depth; if (depth == 0) { ++i; break; } }
                        if (depth > 0) opt_toks.push_back(body[i]);
                        ++i;
                    }
                    --i; // outer loop increments
                    if (has_va) result.insert(result.end(), opt_toks.begin(), opt_toks.end());
                }
                continue;
            }

            auto idx = param_idx(t.spelling);
            if (idx) {
                // Check for ## on either side.
                bool next_paste = (i + 1 < body.size() &&
                                   body[i+1].kind == TokenKind::HashHash);
                bool prev_paste = (!result.empty() && false); // handled already
                (void)prev_paste;
                if (next_paste) {
                    // Paste left arg with right.
                    auto& la = get_arg(*idx);
                    ++i; // skip ##
                    if (i + 1 < body.size()) {
                        ++i;
                        const auto& rt = body[i];
                        std::vector<Token> rhs;
                        if (rt.is_identifier()) {
                            auto ridx = param_idx(rt.spelling);
                            if (ridx) rhs = get_arg(*ridx);
                            else rhs.push_back(rt);
                        } else rhs.push_back(rt);
                        auto pasted = paste(la, rhs, sm_.pool());
                        result.insert(result.end(), pasted.begin(), pasted.end());
                    } else {
                        result.insert(result.end(), la.begin(), la.end());
                    }
                } else {
                    auto expanded_arg = get_arg(*idx);
                    // Fully expand argument before substitution.
                    std::unordered_set<std::string_view> ah;
                    expanded_arg = expand(std::move(expanded_arg), ah);
                    if (!expanded_arg.empty())
                        expanded_arg.front().leading_space = t.leading_space;
                    result.insert(result.end(), expanded_arg.begin(), expanded_arg.end());
                }
                continue;
            }
        }
        result.push_back(t);
    }
    return result;
}

std::string PPState::stringify(const std::vector<Token>& toks) {
    std::string s = "\"";
    bool first = true;
    for (auto& t : toks) {
        if (t.kind == TokenKind::Whitespace) {
            if (!first) s += ' ';
            continue;
        }
        // Escape " and \ inside string literals / char consts.
        for (char c : t.spelling) {
            if (c == '"' || c == '\\') s += '\\';
            s += c;
        }
        first = false;
    }
    s += '"';
    return s;
}

std::vector<Token> PPState::paste(std::vector<Token>        lhs,
                                   const std::vector<Token>& rhs,
                                   StringPool&               pool) {
    if (lhs.empty()) return rhs;
    if (rhs.empty()) return lhs;

    // Concatenate last token of lhs with first token of rhs.
    auto& l = lhs.back();
    auto& r = rhs.front();
    std::string combined = std::string(l.spelling) + std::string(r.spelling);
    l.spelling = pool.intern(combined);
    // l.kind stays as-is (re-lex if needed — for simplicity we keep it).

    // Append rest of rhs.
    lhs.insert(lhs.end(), rhs.begin() + 1, rhs.end());
    return lhs;
}

// ---------------------------------------------------------------------------
// Constant expression evaluator (for #if)
// ---------------------------------------------------------------------------

// Simple recursive-descent evaluator for integer constant expressions.

struct ExprEval {
    const std::vector<Token>& toks;
    std::size_t pos{0};
    StringPool& pool;

    const Token& peek() const {
        static const Token eof_tok{TokenKind::Eof, {}, {}, false};
        for (std::size_t i = pos; i < toks.size(); ++i) {
            if (toks[i].kind != TokenKind::Whitespace) return toks[i];
        }
        return eof_tok;
    }
    Token consume() {
        while (pos < toks.size() && toks[pos].kind == TokenKind::Whitespace)
            ++pos;
        if (pos < toks.size()) return toks[pos++];
        return {TokenKind::Eof, {}, {}, false};
    }

    std::int64_t primary() {
        auto t = consume();
        if (t.kind == TokenKind::PPNumber) {
            std::string s{t.spelling};
            // Handle suffixes L/U/LL/ULL
            while (!s.empty() && (s.back() == 'u' || s.back() == 'U' ||
                                   s.back() == 'l' || s.back() == 'L'))
                s.pop_back();
            try {
                std::size_t idx;
                std::int64_t v = std::stoll(s, &idx, 0);
                return v;
            } catch (...) { return 0; }
        }
        if (t.spelling == "(") {
            auto v = conditional();
            consume(); // ')'
            return v;
        }
        if (t.spelling == "!") return !primary();
        if (t.spelling == "~") return ~primary();
        if (t.spelling == "-") return -primary();
        if (t.spelling == "+") return +primary();
        // char constant
        if (t.kind == TokenKind::CharConst) {
            if (t.spelling.size() >= 3)
                return static_cast<unsigned char>(t.spelling[1]);
            return 0;
        }
        return 0;
    }

    std::int64_t multiplicative() {
        auto v = primary();
        while (true) {
            auto& n = peek();
            if (n.spelling == "*")      { consume(); v *= primary(); }
            else if (n.spelling == "/") { consume(); auto r = primary(); v = r ? v/r : 0; }
            else if (n.spelling == "%") { consume(); auto r = primary(); v = r ? v%r : 0; }
            else break;
        }
        return v;
    }
    std::int64_t additive() {
        auto v = multiplicative();
        while (true) {
            auto& n = peek();
            if (n.spelling == "+")      { consume(); v += multiplicative(); }
            else if (n.spelling == "-") { consume(); v -= multiplicative(); }
            else break;
        }
        return v;
    }
    std::int64_t shift() {
        auto v = additive();
        while (true) {
            auto& n = peek();
            if (n.kind == TokenKind::ShiftLeft)  { consume(); v <<= additive(); }
            else if (n.kind == TokenKind::ShiftRight){ consume(); v >>= additive(); }
            else break;
        }
        return v;
    }
    std::int64_t relational() {
        auto v = shift();
        while (true) {
            auto& n = peek();
            if (n.spelling == "<")          { consume(); v = v < shift(); }
            else if (n.spelling == ">")     { consume(); v = v > shift(); }
            else if (n.kind==TokenKind::LessEq)    { consume(); v = v <= shift(); }
            else if (n.kind==TokenKind::GreaterEq) { consume(); v = v >= shift(); }
            else break;
        }
        return v;
    }
    std::int64_t equality() {
        auto v = relational();
        while (true) {
            auto& n = peek();
            if (n.kind == TokenKind::EqualEq) { consume(); v = v == relational(); }
            else if (n.kind == TokenKind::NotEq) { consume(); v = v != relational(); }
            else break;
        }
        return v;
    }
    std::int64_t bit_and() {
        auto v = equality();
        while (peek().spelling == "&" && peek().kind == TokenKind::Punctuator)
            { consume(); v &= equality(); }
        return v;
    }
    std::int64_t bit_xor() {
        auto v = bit_and();
        while (peek().spelling == "^") { consume(); v ^= bit_and(); }
        return v;
    }
    std::int64_t bit_or() {
        auto v = bit_xor();
        while (peek().spelling == "|" && peek().kind == TokenKind::Punctuator)
            { consume(); v |= bit_xor(); }
        return v;
    }
    std::int64_t logical_and() {
        auto v = bit_or();
        while (peek().kind == TokenKind::AndAnd) { consume(); v = v && bit_or(); }
        return v;
    }
    std::int64_t logical_or() {
        auto v = logical_and();
        while (peek().kind == TokenKind::OrOr) { consume(); v = v || logical_and(); }
        return v;
    }
    std::int64_t conditional() {
        auto v = logical_or();
        if (peek().spelling == "?") {
            consume();
            auto t = conditional();
            consume(); // ':'
            auto f = conditional();
            return v ? t : f;
        }
        return v;
    }
};

std::int64_t PPState::eval_expr(std::vector<Token>    toks,
                                 const SourceLocation& loc) {
    // Replace undefined identifiers with 0 (after expansion).
    for (auto& t : toks) {
        if (t.is_identifier()) {
            t.kind     = TokenKind::PPNumber;
            t.spelling = sm_.pool().intern("0");
        }
    }
    try {
        ExprEval ev{toks, 0, sm_.pool()};
        return ev.conditional();
    } catch (...) {
        diag(DiagLevel::Error, loc, "error evaluating #if expression");
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool preprocess_buffer(const SourceBuffer& buf,
                        std::ostream&       out,
                        const PPOptions&    opts,
                        SourceManager&      sm,
                        DiagHandler         on_diag) {
    PPState state{opts, sm, out, std::move(on_diag)};
    return state.run(buf);
}

bool preprocess(const std::vector<std::filesystem::path>& input_files,
                std::ostream&                             out,
                const PPOptions&                          opts,
                StringPool&                               pool,
                DiagHandler                               on_diag) {
    SourceManager sm{pool, opts.eager_load};

    for (const auto& p : opts.include_paths)
        (void)p; // search paths managed by sm

    if (!opts.parallel || input_files.size() == 1) {
        bool ok = true;
        for (auto& path : input_files) {
            auto buf = sm.get_or_load(path);
            if (!buf) { ok = false; continue; }
            ok &= preprocess_buffer(*buf, out, opts, sm, on_diag);
        }
        return ok;
    }

    // Parallel: process each file asynchronously, serialise output in order.
    std::vector<std::future<std::pair<bool, std::string>>> futures;
    futures.reserve(input_files.size());
    for (auto& path : input_files) {
        futures.push_back(
            std::async(std::launch::async, [&, path]() -> std::pair<bool,std::string> {
                StringPool local_pool;
                SourceManager local_sm{local_pool, opts.eager_load};
                std::ostringstream oss;
                auto buf = local_sm.get_or_load(path);
                if (!buf) return {false, {}};
                bool ok = preprocess_buffer(*buf, oss, opts, local_sm, on_diag);
                return {ok, oss.str()};
            }));
    }

    bool all_ok = true;
    for (auto& f : futures) {
        auto [ok, text] = f.get();
        out << text;
        all_ok &= ok;
    }
    return all_ok;
}

} // namespace c23pp

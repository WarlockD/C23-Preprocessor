// test_lexer.cpp — unit tests for the C23 lexer.
#include <c23pp/lexer.hpp>
#include <c23pp/string_pool.hpp>
#include <c23pp/token.hpp>

#include <functional>
#include <iostream>
#include <string>

int register_test(std::string, std::function<void()>);
#define ASSERT(cond) \
    do { if (!(cond)) { \
        std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond "\n"; \
        throw std::runtime_error("assertion failed"); \
    } } while (0)
#define ASSERT_EQ(a,b) \
    do { if ((a)!=(b)) { \
        std::cerr<<"FAIL ["<<__FILE__<<":"<<__LINE__<<"]: "#a" == "#b"\n"; \
        throw std::runtime_error("assertion failed"); } } while(0)
#define TEST(name) \
    static void test_##name(); \
    static int _reg_##name = register_test(#name, test_##name); \
    static void test_##name()

using namespace c23pp;

TEST(lex_identifier) {
    StringPool pool;
    auto toks = lex_string("hello", "<test>", pool);
    // Should have: Identifier + Eof
    ASSERT(toks.size() >= 2);
    ASSERT_EQ(toks[0].kind, TokenKind::Identifier);
    ASSERT_EQ(toks[0].spelling, "hello");
}

TEST(lex_pp_number) {
    StringPool pool;
    auto toks = lex_string("42", "<test>", pool);
    ASSERT(toks[0].kind == TokenKind::PPNumber);
    ASSERT_EQ(toks[0].spelling, "42");
}

TEST(lex_string_literal) {
    StringPool pool;
    auto toks = lex_string("\"hello world\"", "<test>", pool);
    ASSERT(toks[0].kind == TokenKind::StringLiteral);
}

TEST(lex_char_const) {
    StringPool pool;
    auto toks = lex_string("'a'", "<test>", pool);
    ASSERT(toks[0].kind == TokenKind::CharConst);
}

TEST(lex_line_comment) {
    StringPool pool;
    auto toks = lex_string("x // comment\ny", "<test>", pool);
    bool found_ident_x = false, found_ident_y = false;
    for (auto& t : toks) {
        if (t.kind == TokenKind::Identifier && t.spelling == "x") found_ident_x = true;
        if (t.kind == TokenKind::Identifier && t.spelling == "y") found_ident_y = true;
    }
    ASSERT(found_ident_x);
    ASSERT(found_ident_y);
}

TEST(lex_block_comment) {
    StringPool pool;
    auto toks = lex_string("a /* middle */ b", "<test>", pool);
    int idents = 0;
    for (auto& t : toks)
        if (t.kind == TokenKind::Identifier) ++idents;
    ASSERT_EQ(idents, 2);
}

TEST(lex_hash_hash_punctuator) {
    StringPool pool;
    auto toks = lex_string("##", "<test>", pool);
    ASSERT(toks[0].kind == TokenKind::HashHash);
}

TEST(lex_newline_tracked) {
    StringPool pool;
    auto toks = lex_string("a\nb", "<test>", pool);
    bool has_newline = false;
    for (auto& t : toks)
        if (t.kind == TokenKind::Newline) has_newline = true;
    ASSERT(has_newline);
}

TEST(lex_line_splice) {
    StringPool pool;
    // Backslash-newline should be removed (line splice, phase 2).
    auto toks = lex_string("hel\\\nlo", "<test>", pool);
    bool found = false;
    for (auto& t : toks)
        if (t.kind == TokenKind::Identifier && t.spelling == "hello") found = true;
    ASSERT(found);
}

TEST(lex_location_tracking) {
    StringPool pool;
    auto toks = lex_string("int\n   x", "<myfile.c>", pool);
    // Find identifier 'x'.
    for (auto& t : toks) {
        if (t.kind == TokenKind::Identifier && t.spelling == "x") {
            ASSERT_EQ(t.loc.line, 2u);
            ASSERT(t.loc.col >= 4u);
            ASSERT_EQ(t.loc.filename, "<myfile.c>");
        }
    }
}

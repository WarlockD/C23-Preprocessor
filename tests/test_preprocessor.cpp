// test_preprocessor.cpp — unit tests for the C23 preprocessor.
#include <c23pp/preprocessor.hpp>
#include <c23pp/source_manager.hpp>
#include <c23pp/string_pool.hpp>

#include <functional>
#include <iostream>
#include <sstream>
#include <string>

int register_test(std::string, std::function<void()>);
#define ASSERT(cond) \
    do { if (!(cond)) { \
        std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #cond "\n"; \
        throw std::runtime_error("assertion failed"); \
    } } while (0)
#define ASSERT_EQ(a,b) \
    do { if ((a)!=(b)) { \
        std::cerr<<"FAIL ["<<__FILE__<<":"<<__LINE__<<"]: "#a" == "#b"\n" \
                 <<"  lhs=["<<(a)<<"] rhs=["<<(b)<<"]\n"; \
        throw std::runtime_error("assertion failed"); } } while(0)
#define TEST(name) \
    static void test_##name(); \
    static int _reg_##name = register_test(#name, test_##name); \
    static void test_##name()

using namespace c23pp;

// Helper: preprocess a string, return output without line-markers.
static std::string pp(std::string_view src,
                       PPOptions opts = {},
                       DiagHandler on_diag = {}) {
    opts.emit_line_markers = false;
    StringPool pool;
    SourceManager sm{pool, true};
    auto buf = SourceBuffer::from_string("<test>", std::string(src), pool);
    sm.add_buffer(buf);
    std::ostringstream oss;
    preprocess_buffer(*buf, oss, opts, sm, std::move(on_diag));
    return oss.str();
}

// Strip leading/trailing whitespace from output for comparison.
static std::string trim(std::string s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

TEST(pp_passthrough) {
    auto out = pp("int x = 1;\n");
    ASSERT(out.find("int") != std::string::npos);
    ASSERT(out.find("x") != std::string::npos);
}

TEST(pp_object_macro) {
    auto out = pp("#define FOO 42\nint x = FOO;\n");
    ASSERT(out.find("42") != std::string::npos);
    ASSERT(out.find("FOO") == std::string::npos);
}

TEST(pp_function_macro) {
    auto out = pp("#define SQ(x) ((x)*(x))\nint y = SQ(3);\n");
    ASSERT(out.find("((3)*(3))") != std::string::npos);
}

TEST(pp_macro_undef) {
    auto out = pp("#define A 1\n#undef A\nint x = A;\n");
    ASSERT(out.find("A") != std::string::npos); // A should be un-expanded
}

TEST(pp_ifdef_true) {
    auto out = pp("#define HAVE_X\n#ifdef HAVE_X\nyes\n#endif\n");
    ASSERT(out.find("yes") != std::string::npos);
}

TEST(pp_ifdef_false) {
    auto out = pp("#ifdef MISSING\nno\n#endif\n");
    ASSERT(out.find("no") == std::string::npos);
}

TEST(pp_ifndef) {
    auto out = pp("#ifndef MISSING\nyes\n#endif\n");
    ASSERT(out.find("yes") != std::string::npos);
}

TEST(pp_if_true) {
    auto out = pp("#if 1\nyes\n#endif\n");
    ASSERT(out.find("yes") != std::string::npos);
}

TEST(pp_if_false) {
    auto out = pp("#if 0\nno\n#endif\n");
    ASSERT(out.find("no") == std::string::npos);
}

TEST(pp_if_else) {
    auto out = pp("#if 0\nno\n#else\nyes\n#endif\n");
    ASSERT(out.find("yes") != std::string::npos);
    ASSERT(out.find("no") == std::string::npos);
}

TEST(pp_elif) {
    auto out = pp("#if 0\nno\n#elif 1\nyes\n#endif\n");
    ASSERT(out.find("yes") != std::string::npos);
    ASSERT(out.find("no") == std::string::npos);
}

TEST(pp_elifdef) {
    auto out = pp("#define FOO\n#ifdef BAR\nno\n#elifdef FOO\nyes\n#endif\n");
    ASSERT(out.find("yes") != std::string::npos);
    ASSERT(out.find("no") == std::string::npos);
}

TEST(pp_elifndef) {
    auto out = pp("#ifdef FOO\nno\n#elifndef FOO\nyes\n#endif\n");
    ASSERT(out.find("yes") != std::string::npos);
    ASSERT(out.find("no") == std::string::npos);
}

TEST(pp_stdc_version) {
    auto out = pp("__STDC_VERSION__\n");
    ASSERT(out.find("202311") != std::string::npos);
}

TEST(pp_stringify) {
    auto out = pp("#define STR(x) #x\nSTR(hello)\n");
    ASSERT(out.find("\"hello\"") != std::string::npos);
}

TEST(pp_token_paste) {
    auto out = pp("#define PASTE(a,b) a##b\nPASTE(foo,bar)\n");
    ASSERT(out.find("foobar") != std::string::npos);
}

TEST(pp_variadic) {
    auto out = pp("#define LOG(...) log(__VA_ARGS__)\nLOG(a,b,c)\n");
    ASSERT(out.find("log(a,b,c)") != std::string::npos
        || out.find("log(a") != std::string::npos);
}

TEST(pp_nested_macro) {
    auto out = pp("#define A B\n#define B 99\nA\n");
    ASSERT(out.find("99") != std::string::npos);
}

TEST(pp_cmdline_define) {
    PPOptions opts;
    opts.defines.emplace_back("VERSION", "7");
    auto out = pp("VERSION\n", opts);
    ASSERT(out.find("7") != std::string::npos);
}

TEST(pp_null_directive) {
    // A lone '#' on a line is a null directive — should be ignored.
    auto out = pp("#\nint x;\n");
    ASSERT(out.find("int") != std::string::npos);
}

TEST(pp_line_continuation_in_macro) {
    // Macro body split across lines with backslash-newline.
    auto out = pp("#define ML \\\n    42\nML\n");
    ASSERT(out.find("42") != std::string::npos);
}

TEST(pp_predefined_date) {
    auto out = pp("__DATE__\n");
    // __DATE__ expands to a string literal.
    ASSERT(out.find("\"") != std::string::npos);
}

TEST(pp_if_expression_arithmetic) {
    auto out = pp("#if 2 + 3 == 5\nyes\n#endif\n");
    ASSERT(out.find("yes") != std::string::npos);
}

TEST(pp_if_defined_operator) {
    auto out = pp("#define X\n#if defined(X)\nyes\n#endif\n");
    ASSERT(out.find("yes") != std::string::npos);
}

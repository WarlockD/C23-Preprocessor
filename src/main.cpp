// main.cpp — C23 preprocessor command-line driver.
//
// Usage: c23pp [options] [file...]
//
// Options:
//   -I<dir>          Add directory to user include search path
//   -isystem <dir>   Add directory to system include search path
//   -D<name>[=<val>] Define a macro
//   -U<name>         Undefine a macro
//   -o <file>        Write output to <file> (default: stdout)
//   --expand-log <f> Write macro-expansion log to <f>
//   --no-line-marks  Do not emit #line markers
//   --keep-comments  Retain comment tokens
//   --lazy           Do not eagerly load #included files
//   --parallel       Process multiple top-level files concurrently
//   --std=<ver>      Target standard (default: c23)
//   -                Read from stdin

#include <c23pp/preprocessor.hpp>
#include <c23pp/string_pool.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_usage(std::string_view prog) {
    std::cerr << std::format(
        "Usage: {} [options] [file...]\n"
        "\n"
        "Options:\n"
        "  -I<dir>              Add user include path\n"
        "  -isystem <dir>       Add system include path\n"
        "  -D<name>[=<value>]   Define macro\n"
        "  -U<name>             Undefine macro\n"
        "  -o <file>            Output file (default: stdout)\n"
        "  --expand-log <file>  Macro-expansion log output file\n"
        "  --no-line-marks      Suppress #line markers\n"
        "  --keep-comments      Keep comment tokens\n"
        "  --lazy               Lazy file loading\n"
        "  --parallel           Parallel processing of multiple inputs\n"
        "  --std=<ver>          Target standard (default: c23)\n"
        "  -                    Read from stdin\n",
        prog);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    c23pp::PPOptions opts;
    std::vector<std::filesystem::path> input_files;
    std::string output_file;
    std::string expand_log_file;
    bool read_stdin = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};

        if (arg.starts_with("-I") && arg.size() > 2) {
            opts.include_paths.emplace_back(arg.substr(2));
        } else if (arg == "-I" && i + 1 < argc) {
            opts.include_paths.emplace_back(argv[++i]);
        } else if (arg == "-isystem" && i + 1 < argc) {
            opts.system_include_paths.emplace_back(argv[++i]);
        } else if (arg.starts_with("-D") && arg.size() > 2) {
            std::string def{arg.substr(2)};
            auto eq = def.find('=');
            if (eq == std::string::npos)
                opts.defines.emplace_back(def, "1");
            else
                opts.defines.emplace_back(def.substr(0, eq), def.substr(eq + 1));
        } else if (arg == "-D" && i + 1 < argc) {
            std::string def{argv[++i]};
            auto eq = def.find('=');
            if (eq == std::string::npos)
                opts.defines.emplace_back(def, "1");
            else
                opts.defines.emplace_back(def.substr(0, eq), def.substr(eq + 1));
        } else if (arg.starts_with("-U") && arg.size() > 2) {
            opts.undefines.emplace_back(std::string(arg.substr(2)));
        } else if (arg == "-U" && i + 1 < argc) {
            opts.undefines.emplace_back(argv[++i]);
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--expand-log" && i + 1 < argc) {
            expand_log_file = argv[++i];
        } else if (arg == "--no-line-marks") {
            opts.emit_line_markers = false;
        } else if (arg == "--keep-comments") {
            opts.keep_comments = true;
        } else if (arg == "--lazy") {
            opts.eager_load = false;
        } else if (arg == "--parallel") {
            opts.parallel = true;
        } else if (arg.starts_with("--std=")) {
            opts.std_version = std::string(arg.substr(6));
        } else if (arg == "-" ) {
            read_stdin = true;
        } else if (arg.starts_with("-")) {
            std::cerr << std::format("Unknown option: {}\n", arg);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        } else {
            input_files.emplace_back(arg);
        }
    }

    // Open output stream.
    std::ofstream  out_file;
    std::ostream*  out = &std::cout;
    if (!output_file.empty()) {
        out_file.open(output_file);
        if (!out_file) {
            std::cerr << std::format("Cannot open output file: {}\n", output_file);
            return EXIT_FAILURE;
        }
        out = &out_file;
    }

    // Open macro-expansion log.
    std::ofstream expand_log;
    if (!expand_log_file.empty()) {
        expand_log.open(expand_log_file);
        if (!expand_log) {
            std::cerr << std::format("Cannot open expand-log file: {}\n", expand_log_file);
            return EXIT_FAILURE;
        }
        opts.macro_expansion_log = &expand_log;
    }

    // Diagnostic handler — write to stderr.
    bool had_error = false;
    auto on_diag = [&](const c23pp::Diagnostic& d) {
        std::string_view level;
        switch (d.level) {
            case c23pp::DiagLevel::Note:    level = "note";    break;
            case c23pp::DiagLevel::Warning: level = "warning"; break;
            case c23pp::DiagLevel::Error:   level = "error";   had_error = true; break;
            case c23pp::DiagLevel::Fatal:   level = "fatal";   had_error = true; break;
        }
        std::cerr << std::format("{}:{}:{}: {}: {}\n",
            d.loc.filename, d.loc.line, d.loc.col, level, d.message);
    };

    c23pp::StringPool pool;

    // Read from stdin if requested.
    if (read_stdin) {
        auto buf = c23pp::SourceBuffer::from_stream(std::cin, "<stdin>", pool);
        c23pp::SourceManager sm{pool, opts.eager_load};
        sm.add_buffer(buf);
        c23pp::preprocess_buffer(*buf, *out, opts, sm, on_diag);
        return had_error ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    if (input_files.empty()) {
        // Default to stdin if no files given.
        auto buf = c23pp::SourceBuffer::from_stream(std::cin, "<stdin>", pool);
        c23pp::SourceManager sm{pool, opts.eager_load};
        sm.add_buffer(buf);
        c23pp::preprocess_buffer(*buf, *out, opts, sm, on_diag);
        return had_error ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    bool ok = c23pp::preprocess(input_files, *out, opts, pool, on_diag);
    return (ok && !had_error) ? EXIT_SUCCESS : EXIT_FAILURE;
}

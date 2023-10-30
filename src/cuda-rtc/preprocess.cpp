// Simplified version of jitify2_preprocess.hpp
// Changes:
//  - The "include-style" is removed. Files are always saved as C++ source.
//  - Sources and headers are not deserialized, this is up to the user now.
//  - The C-arrays can be static, since in most cases they shouldn't be shared across translation units.

#include "jitify2.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>

void write_binary_as_c_array(
        const std::string& varname,
        std::istream& istream,
        std::ostream& ostream,
        bool make_static
) {
    ostream << "#pragma once\n";
    if (make_static)
        ostream << "static ";
    ostream << "const unsigned char " << varname << "[] = {\n";

    constexpr const char* hex_digits = "0123456789abcdef";
    unsigned int n = 0;
    while (true) {
        unsigned char c;
        istream.read((char*) &c, 1);
        if (!istream.good())
            break;
        ostream << '0' << 'x' << hex_digits[c >> 4] << hex_digits[c & 0xF] << ",";
        if (++n % 16 == 0)
            ostream << "\n";
    }
    ostream << "\n};\n";
}

// Replaces non-alphanumeric characters with '_' and
// prepends '_' if the string begins with a digit.
std::string sanitize_varname(const std::string& s) {
    std::string r = s;
    if (std::isdigit(r[0]))
        r = '_' + r;
    for (char& it: r)
        if (!std::isalnum(it))
            it = '_';
    return r;
}

bool read_file(const std::string& fullpath, std::string* content) {
    std::ifstream file(fullpath.c_str(), std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    content->resize(size);
    file.read(&(*content)[0], size);
    return true;
}

bool make_directories_for(const std::string& filename) {
    using jitify2::detail::make_directories;
    using jitify2::detail::path_base;
    if (!make_directories(path_base(filename))) {
        std::cerr << "Error creating directories for output file " << filename
                  << std::endl;
        return false;
    }
    return true;
}

struct Options {
    std::string shared_headers_filename;
    std::string output_dir;
    std::string varname_prefix;
    jitify2::StringVec other_options;
    jitify2::StringVec source_filenames;
    bool verbose = false;

    bool parse(int argc, char* argv[]) {
        const char* arg_c;
        while ((arg_c = *++argv)) {
            std::string arg = arg_c;
            if (arg[0] == '-') {
                if (arg == "-s" || arg == "--shared-headers") {
                    arg_c = *++argv;
                    if (!arg_c) {
                        std::cerr << "Expected filename after -s" << std::endl;
                        return false;
                    }
                    shared_headers_filename = arg_c;
                } else if (arg == "-o" || arg == "--output-directory") {
                    arg_c = *++argv;
                    if (!arg_c) {
                        std::cerr << "Expected directory after -o / --output-directory"
                                  << std::endl;
                        return false;
                    }
                    output_dir = arg_c;
                } else if (arg == "-p" || arg == "--variable-prefix") {
                    arg_c = *++argv;
                    if (!arg_c) {
                        std::cerr << "Expected prefix after -p / --variable-prefix"
                                  << std::endl;
                        return false;
                    }
                    varname_prefix = arg_c;
                } else if (arg == "-v" || arg == "--verbose") {
                    verbose = true;
                } else {
                    other_options.push_back(arg);
                }
            } else {
                source_filenames.push_back(arg);
            }
        }
        return true;
    }
};

int main(int argc, char* argv[]) {
    using namespace jitify2;
    using jitify2::detail::path_join;

    Options options;
    options.parse(argc, argv);

    if (options.source_filenames.empty()) {
        std::cerr << "Expected at least one source file" << std::endl;
        return EXIT_FAILURE;
    }

    bool share_headers = !options.shared_headers_filename.empty();
    std::string shared_headers_varname;
    if (share_headers) {
        shared_headers_varname = sanitize_varname(
                options.varname_prefix + options.shared_headers_filename + ".jit");
    }

    StringMap all_header_sources;
    for (const std::string& source_filename : options.source_filenames) {
        // Load the source file.
        std::string source;
        if (!read_file(source_filename, &source)) {
            std::cerr << "Error reading source file " << source_filename << std::endl;
            return EXIT_FAILURE;
        }

        // Preprocess.
        PreprocessedProgram preprocessed = Program(
                source_filename, source,
                share_headers ? all_header_sources : StringMap{})
                ->preprocess(options.other_options);
        if (!preprocessed) {
            std::cerr << "Error processing source file " << source_filename << "\n"
                      << preprocessed.error() << std::endl;
            return EXIT_FAILURE;
        }
        if (options.verbose && !preprocessed->header_log().empty())
            std::cout << preprocessed->header_log() << std::endl;
        if (!preprocessed->compile_log().empty())
            std::cout << preprocessed->compile_log() << std::endl;

        // Add detected headers to existing ones.
        if (share_headers) {
            // TODO Check that there aren't weird header name issues when merging
            //      multiple source files like this.
            all_header_sources.insert(preprocessed->header_sources().begin(),
                                      preprocessed->header_sources().end());
        }

        // Serialize the preprocessed source and save it in a C++ header.
        std::stringstream ss(std::stringstream::in | std::stringstream::out | std::stringstream::binary);
        preprocessed->serialize(ss, /*include_headers = */ !share_headers);
        std::string source_varname = sanitize_varname(options.varname_prefix + source_filename + ".jit");
        std::string output_filename = path_join(options.output_dir, source_filename + ".jit.hpp");
        if (!make_directories_for(output_filename))
            return EXIT_FAILURE;
        std::ofstream file(output_filename, std::ios::binary);
        write_binary_as_c_array(source_varname, ss, file, /*make_static=*/ true);
        if (!file) {
            std::cerr << "Error writing output to " << output_filename << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Serialize the preprocessed headers and save them in a C++ source file.
    if (share_headers) {
        std::stringstream ss(std::stringstream::in | std::stringstream::out | std::stringstream::binary);
        serialization::serialize(ss, all_header_sources);
        std::string output_filename = path_join(options.output_dir, options.shared_headers_filename + ".jit.cpp");
        if (!make_directories_for(output_filename)) return EXIT_FAILURE;
        std::ofstream file(output_filename, std::ios::binary);
        write_binary_as_c_array(shared_headers_varname, ss, file, /*make_static=*/ true);
        if (!file) {
            std::cerr << "Error writing output to " << output_filename << std::endl;
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

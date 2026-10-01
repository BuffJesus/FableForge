#include "forge/temporarydirectory.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace fs = std::filesystem;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

int main() {
    try {
        static_assert(!std::is_copy_constructible_v<forge::TemporaryDirectory>);
        forge::TemporaryDirectory suite(fs::temp_directory_path() / "forge-tests", "tempdir-");
        const auto marker = suite.path() / "unowned.txt";
        std::ofstream(marker) << "unowned";
        fs::path first, second;
        {
            forge::TemporaryDirectory a(suite.path(), "same-"), b(suite.path(), "same-");
            first = a.path(); second = b.path();
            check(first != second && fs::is_directory(first) && fs::is_directory(second), "live workspaces collided");
            check(first.is_absolute() && first.parent_path() == suite.path(), "workspace escaped parent");
            fs::create_directory(first / "nested");
            std::ofstream(first / "nested/file") << "owned";
        }
        check(!fs::exists(first) && !fs::exists(second), "normal exit retained owned files");
        try {
            forge::TemporaryDirectory failed(suite.path(), "failure-");
            first = failed.path();
            std::ofstream(first / "file") << "owned";
            throw std::runtime_error("test exception");
        } catch (const std::runtime_error&) {}
        check(!fs::exists(first), "exception retained owned files");
        for (const auto* prefix : {"", "../escape-", "nested/escape-"}) {
            bool rejected = false;
            try { forge::TemporaryDirectory bad(suite.path(), prefix); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "unsafe prefix accepted");
        }
        std::ifstream in(marker); std::string value; in >> value;
        check(value == "unowned", "cleanup changed unowned sibling");
        check(std::distance(fs::directory_iterator(suite.path()), fs::directory_iterator()) == 1, "unexpected scratch entries remain");
        std::cout << "temporary directory ownership: PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

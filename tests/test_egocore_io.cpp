#include "forge/big.hpp"
#include "forge/egocore.hpp"
#include "forge/temporarydirectory.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
static void check(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
static void put(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary); out << data; out.close();
    check(bool(out), "fixture write failed");
}
static std::string get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary); check(bool(in), "fixture read failed");
    return {std::istreambuf_iterator<char>(in), {}};
}
template<class F> static bool throws(F&& f) {
    try { f(); } catch (const std::exception&) { return true; }
    return false;
}
#ifdef _WIN32
struct Lock {
    HANDLE handle;
    Lock(const fs::path& p, DWORD share) : handle(CreateFileW(p.c_str(), GENERIC_READ, share,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        check(handle != INVALID_HANDLE_VALUE, "cannot lock fixture");
    }
    ~Lock() { CloseHandle(handle); }
};
#endif

int main() {
    try {
        forge::TemporaryDirectory work(fs::temp_directory_path(), "forge-egocore-io-");
        const auto base = work.path() / "base", mod = work.path() / "Probe";
        const auto dll = mod / "Probe.dll";
        put(dll, "synthetic DLL; never executed");
        put(base / "Mods.ini", "[Mods]\nExisting.dll=1\n");
        const auto output = work.path() / "dll-output";
        forge::egocore::Report report;
#ifdef _WIN32
        {
            Lock lock(dll, 0);
            check(throws([&] { forge::egocore::installDll(mod, base, output, report); }),
                  "unreadable DLL was silently accepted");
            check(!fs::exists(output / "Mods.ini"), "failed DLL copy registered its DLL");
        }
#endif
        forge::egocore::installDll(mod, base, output, report);
        check(get(output / "Mods/Probe/Probe.dll") == get(dll), "DLL copy differs");
        check(get(output / "Mods.ini").find("Probe\\Probe.dll=1") != std::string::npos, "DLL registration missing");
        check(get(output / "Mods.ini").find("Existing.dll=1") != std::string::npos, "existing registration lost");
#ifdef _WIN32
        {
            const auto ini = get(output / "Mods.ini");
            Lock lock(output / "Mods.ini", FILE_SHARE_READ);
            check(throws([&] { forge::egocore::installDll(mod, base, output, report); }),
                  "unwritable Mods.ini was silently accepted");
            check(get(output / "Mods.ini") == ini, "failed registration changed Mods.ini");
        }
#endif
        const fs::path relative = "data/graphics/probe.big";
        forge::big::File source;
        source.setMagic("BIGB");
        auto& bank = source.addBank("TestBank", 7);
        forge::big::Entry entry;
        entry.name = "Changed"; entry.id = 11; entry.data = {1, 2, 3}; entry.length = 3; entry.subHeader = {9, 8};
        bank.entries.push_back(entry);
        entry.name = "Untouched"; entry.id = 12; entry.data = {4, 5, 6};
        bank.entries.push_back(entry);
        const auto bytes = source.serialize();
        put(base / relative, {bytes.begin(), bytes.end()});
        const auto resource = mod / "Data/graphics/probe.big/TestBank/Changed.resource";
        const auto header = resource.parent_path() / "Changed.header";
        put(resource, "new payload"); put(header, "new header");
#ifdef _WIN32
        {
            const auto failedOut = work.path() / "source-locked";
            Lock lock(base / relative, 0);
            check(throws([&] { forge::egocore::applyResourceOverrides(mod, base, failedOut, report); }),
                  "unreadable source bank was silently accepted");
            check(!fs::exists(failedOut / relative), "unreadable source bank emitted output");
        }
        for (const auto& input : {resource, header}) {
            const auto failedOut = work.path() / input.extension().string().substr(1);
            Lock lock(input, 0);
            check(throws([&] { forge::egocore::applyResourceOverrides(mod, base, failedOut, report); }),
                  "unreadable resource/header was silently accepted");
            check(!fs::exists(failedOut / relative), "unreadable override emitted a damaged bank");
        }
        const auto lockedOut = work.path() / "locked-output";
        put(lockedOut / relative, {bytes.begin(), bytes.end()});
        {
            Lock lock(lockedOut / relative, FILE_SHARE_READ);
            check(throws([&] { forge::egocore::applyResourceOverrides(mod, base, lockedOut, report); }),
                  "unwritable output bank was silently accepted");
        }
        check(get(lockedOut / relative) == get(base / relative), "failed output write changed the bank");
#endif
        const auto resourceOut = work.path() / "resource-output";
        check(forge::egocore::applyResourceOverrides(mod, base, resourceOut, report) == 1, "resource apply count");
        const auto changed = forge::big::File::open(resourceOut / relative);
        check(changed.banks().size() == 1 && changed.banks()[0].entries.size() == 2, "bank structure changed");
        const auto& first = changed.banks()[0].entries[0];
        const auto payload = changed.entryData(first);
        check(first.id == 11 && std::string(payload.begin(), payload.end()) == "new payload", "override payload differs");
        check(std::string(first.subHeader.begin(), first.subHeader.end()) == "new header", "override header differs");
        const auto& second = changed.banks()[0].entries[1];
        check(second.id == 12 && second.name == "Untouched" && second.subHeader == entry.subHeader &&
              changed.entryData(second) == entry.data, "unrelated entry changed");
        check(get(base / relative) == std::string(bytes.begin(), bytes.end()), "source bank changed");
        std::cout << "EgoCore copy, override I/O failures and successful retry: PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

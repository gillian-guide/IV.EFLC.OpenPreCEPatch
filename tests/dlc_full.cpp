#include <common.hxx>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

import loading;

// The full DLC discovery rewrite: it walks the checked path API, registers the
// one existing layout per DLC, applies it, and sets the post-discovery bytes.
//
// The apply call's contract is the game's: `sub_88FE00` is `__thiscall`, so the
// DLC manager arrives in ECX and the request pointer on the stack. The fakes below
// expose the same shape, which is what makes the test catch a cdecl capture (the
// manager would arrive on the stack and ECX would be the caller's garbage -- the
// lived fault at `0x88FE04`).

namespace
{
    int failures = 0;
    int checks = 0;
    std::vector<std::pair<int, std::string>> g_registered;
    std::vector<std::string> g_applied;
    void* g_applyThis = nullptr;
    void* g_expectedObject = nullptr;

    void Check(bool condition, std::string_view what)
    {
        ++checks;
        if (condition)
        {
            std::printf("PASS %.*s\n", static_cast<int>(what.size()), what.data());
            return;
        }
        ++failures;
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
    }

    // sub_88F440's shape: `char __stdcall(int id, const char* request, int flag)`.
    void __stdcall FakeRegister(int id, const char* path, int)
    {
        g_registered.emplace_back(id, path != nullptr ? path : "");
    }

    void __cdecl RecordApply(void* object, const void* request)
    {
        g_applyThis = object;
        g_applied.emplace_back(request != nullptr ? static_cast<const char*>(request) : "");
    }

    // The game's apply function: `this` in ECX, the request on the stack, and the
    // callee pops the argument (`ret 4`).
    static __declspec(naked) void FakeApply()
    {
        __asm
        {
            push dword ptr [esp + 4]
            push ecx
            call RecordApply
            add esp, 8
            ret 4
        }
    }

    std::filesystem::path ModuleDirectory()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return {};
            if (length < buffer.size())
            {
                buffer.resize(length);
                return std::filesystem::path(buffer).parent_path();
            }
            buffer.resize(buffer.size() * 2);
        }
    }
}

int main()
{
    const auto base = ModuleDirectory();
    Check(!base.empty(), "dlc_full: the test module directory is known");

    const auto tlad = base / "TLAD";
    const auto tbogt = base / "TBoGT";
    const auto episode = base / "Episode3";
    std::error_code ignored;
    std::filesystem::create_directories(tlad, ignored);
    std::filesystem::create_directories(tbogt, ignored);
    std::filesystem::create_directories(episode, ignored);

    const auto module = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    ::SetDlcRvasForTest(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FakeRegister) - module),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FakeApply) - module));

    uint8_t object[400]{};
    *reinterpret_cast<int*>(object) = 2;
    g_expectedObject = object;
    ::DlcDiscoveryForTest(object);

    Check(g_registered.size() == 3, "dlc_full: TLAD, TBoGT and Episode3 are registered");
    Check(g_applied.size() == 3, "dlc_full: each registration is applied");
    Check(g_registered.size() == 3 &&
              g_registered[0].first == 2 && g_registered[1].first == 4 &&
              g_registered[2].first == 6,
        "dlc_full: the ids are 2, 4 and 2*3");
    Check(!g_registered.empty() &&
              g_registered[0].second.find("TLAD") != std::string::npos,
        "dlc_full: the TLAD path is the legacy layout that exists");

    // The apply contract: the manager in ECX (recorded by the fake's `push ecx`)
    // and the registered request text on the stack.
    Check(g_applyThis == g_expectedObject,
        "dlc_full: the apply call passes the DLC manager as `this` (ECX)");
    Check(g_applied.size() == g_registered.size(),
        "dlc_full: every registration is applied exactly once");
    bool pathsMatch = g_applied.size() == g_registered.size();
    for (std::size_t index = 0; pathsMatch && index < g_applied.size(); ++index)
        pathsMatch = g_applied[index] == g_registered[index].second;
    Check(pathsMatch, "dlc_full: the apply call passes the registered request pointer");

    Check(object[0xA4] == 1, "dlc_full: the post-discovery marker is set");
    Check(object[0x166] == 2, "dlc_full: the mode byte is 2");

    std::filesystem::remove_all(tlad, ignored);
    std::filesystem::remove_all(tbogt, ignored);
    std::filesystem::remove_all(episode, ignored);

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

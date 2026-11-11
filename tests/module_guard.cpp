#include <common.hxx>

#include <cstdio>

import misc;

// The GFWL xlive arm's module-image guard: an offset is readable only when it
// lies inside the mapped image and its page is committed and accessible.

namespace
{
    int failures = 0;
    int checks = 0;

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
}

int main()
{
    const HMODULE module = GetModuleHandleW(nullptr);
    MODULEINFO info{};
    const bool gotInfo = module != nullptr &&
        GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) != FALSE;
    Check(gotInfo && info.SizeOfImage != 0, "module_guard: the process image size is known");

    if (gotInfo)
    {
        Check(::ModuleImageContainsForTest(reinterpret_cast<uintptr_t>(module), 0, 1),
            "module_guard: the image start is readable");
        Check(::ModuleImageContainsForTest(reinterpret_cast<uintptr_t>(module),
                  info.SizeOfImage - 1, 1),
            "module_guard: the last image byte is readable");
        Check(!::ModuleImageContainsForTest(reinterpret_cast<uintptr_t>(module),
                  info.SizeOfImage, 1),
            "module_guard: the first byte past the image is refused");
        Check(!::ModuleImageContainsForTest(reinterpret_cast<uintptr_t>(module),
                  info.SizeOfImage - 1, 2),
            "module_guard: a read crossing the image end is refused");
        Check(!::ModuleImageContainsForTest(0, 0, 1),
            "module_guard: a null module is refused");
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

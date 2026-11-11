#include <common.hxx>

#include <cmath>
#include <cstdio>

import vehicles;

// The checked ForceCarHeadlightShadows walk: a real object, a fake model lookup,
// and the table slot; a null object or a missing lookup keeps the 2.0f seed.

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

    int __cdecl FakeLookup(uint32_t hash, int32_t* out)
    {
        *out = hash == 0xAAu ? 5 : 9;
        return 1;
    }
}

int main()
{
    uint8_t object[0x40]{};
    *reinterpret_cast<int16_t*>(object + 0x2E) = 5;

    ::ClearFchsTableForTest();
    ::SetFchsSlotForTest(0, 0xAAu, 3.5f);
    ::SetFchsStateForTest(reinterpret_cast<uintptr_t>(object),
        reinterpret_cast<uintptr_t>(&FakeLookup));
    ::RunFchsCheckForTest();
    Check(std::fabs(::FchsOffsetForTest() - 3.5f) < 1e-6f,
        "fchs: a matching table slot publishes its offset");

    ::SetFchsStateForTest(0, reinterpret_cast<uintptr_t>(&FakeLookup));
    ::RunFchsCheckForTest();
    Check(std::fabs(::FchsOffsetForTest() - 2.0f) < 1e-6f,
        "fchs: a null object keeps the 2.0f seed");

    ::SetFchsStateForTest(reinterpret_cast<uintptr_t>(object), 0);
    ::RunFchsCheckForTest();
    Check(std::fabs(::FchsOffsetForTest() - 2.0f) < 1e-6f,
        "fchs: a missing lookup keeps the 2.0f seed");

    ::ClearFchsTableForTest();
    ::SetFchsStateForTest(reinterpret_cast<uintptr_t>(object),
        reinterpret_cast<uintptr_t>(&FakeLookup));
    ::RunFchsCheckForTest();
    Check(std::fabs(::FchsOffsetForTest() - 2.0f) < 1e-6f,
        "fchs: an empty table keeps the 2.0f seed");

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

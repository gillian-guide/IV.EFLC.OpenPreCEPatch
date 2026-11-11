#include <common.hxx>

#include <cstdio>

import common;
import shared;

// The `caches` group's model-lookup memo: successful positive indices are
// answered from the table, negative results are never cached, and a missing
// original is a clean zero.

namespace
{
    int failures = 0;
    int checks = 0;
    int calls = 0;

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
        ++calls;
        if (hash == 0xDEADu)
        {
            *out = -1;
            return 0;
        }
        *out = 42;
        return 1;
    }

    void TestCache()
    {
        Shared::ResetLookupCacheForTest();
        Shared::SetModelLookupForTest(reinterpret_cast<uintptr_t>(&FakeLookup), true);

        int32_t index = 0;
        calls = 0;
        Check(Shared::CachedLookupForTest(0x1234u, &index) == 1 && index == 42,
            "cache: the first lookup resolves through the original");
        Check(calls == 1, "cache: the original ran once");
        Check(Shared::CachedLookupForTest(0x1234u, &index) == 1 && index == 42,
            "cache: the second lookup answers the same index");
        Check(calls == 1, "cache: the second lookup did not call the original");

        calls = 0;
        Check(Shared::CachedLookupForTest(0xDEADu, &index) == 0 && index == -1,
            "cache: a negative answer is passed through");
        Check(Shared::CachedLookupForTest(0xDEADu, &index) == 0,
            "cache: the negative answer is repeated");
        Check(calls == 2, "cache: a negative result is never cached");

        Shared::SetModelLookupForTest(0, true);
        Shared::ResetLookupCacheForTest();
        Check(Shared::CachedLookupForTest(0x1u, &index) == 0 && index == -1,
            "cache: a missing original answers zero and -1");
    }
}

int main()
{
    TestCache();
    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

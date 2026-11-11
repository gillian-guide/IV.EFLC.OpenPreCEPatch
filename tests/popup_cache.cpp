#include <common.hxx>

#include <cmath>
#include <cstdio>

import misc;

// The `popup_cache` group: one model-lookup resolution and one angle scan per
// unique model hash, with the exact table semantics the transcribed check uses.

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
        *out = hash == 0xAAu ? 7 : 99;
        return 1;
    }
}

int main()
{
    ::ResetPopupCacheForTest();
    ::ClearPopupTablesForTest();
    ::SetPopupLookupForTest(reinterpret_cast<uintptr_t>(&FakeLookup));
    ::SetPopupModelForTest(7);
    ::SetPopupTableSlotForTest(0, 0xAAu, 0xAAu, 0.5f);

    calls = 0;
    ::RunPopupCheckCachedForTest();
    Check(::PopupFlagForTest() == 1, "popup_cache: the model resolves through the table");
    Check(::PopupIndexForTest() == 99, "popup_cache: the special popuplights hash is resolved first");
    Check(std::fabs(::PopupAngleForTest() - 0.5f) < 1e-6f,
        "popup_cache: the angle comes from the matching table slot");
    Check(calls == 2, "popup_cache: the special hash and the table model resolve once each");

    ::RunPopupCheckCachedForTest();
    Check(calls == 2, "popup_cache: the second run answers from the memo");
    Check(::PopupFlagForTest() == 1, "popup_cache: the memoised run keeps the verdict");

    ::ResetPopupCacheForTest();
    ::RunPopupCheckCachedForTest();
    Check(calls == 4, "popup_cache: reset forces a re-resolve");

    ::SetPopupLookupForTest(0);
    ::ResetPopupCacheForTest();
    ::RunPopupCheckCachedForTest();
    Check(::PopupFlagForTest() == 0, "popup_cache: a missing lookup is a clean skip");

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

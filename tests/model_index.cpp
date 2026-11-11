#include <common.hxx>

#include <cstdio>
#include <cstring>

import stability;

// The `model_index` group: the 38,455-name table is indexed once by the same
// hash resolver the transcribed body calls, then binary-searched. A fake
// resolver keeps this independent of the game image.

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

    uint32_t __cdecl FakeHash(const char* text)
    {
        uint32_t hash = 2166136261u;
        for (; text != nullptr && *text != '\0'; ++text)
        {
            hash ^= static_cast<uint8_t>(*text);
            hash *= 16777619u;
        }
        return hash;
    }
}

int main()
{
    ::SetModelNameIndexResolverForTest(reinterpret_cast<uintptr_t>(&FakeHash));

    const auto hash = FakeHash("infernus");
    const char* const name = ::ModelNameIndexedForTest(hash);
    Check(name != nullptr && std::strcmp(name, "**UNKNOWN**") != 0,
        "model_index: a table name is found");
    Check(name != nullptr && FakeHash(name) == hash,
        "model_index: the found name hashes to the queried value");
    Check(std::strcmp(::ModelNameIndexedForTest(hash), name) == 0,
        "model_index: the second lookup is the same answer");

    const auto absent = FakeHash("not_a_model_name_zzzz");
    const char* const missing = ::ModelNameIndexedForTest(absent);
    Check(missing != nullptr &&
              (std::strcmp(missing, "**UNKNOWN**") == 0 || FakeHash(missing) == absent),
        "model_index: a miss answers UNKNOWN (or a real hash collision)");

    ::SetModelNameIndexResolverForTest(0);
    Check(std::strcmp(::ModelNameIndexedForTest(hash), "**UNKNOWN**") == 0,
        "model_index: a missing resolver answers UNKNOWN");

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

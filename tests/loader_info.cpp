#include <common.hxx>

#include <cstdio>

import common;

// Ultimate ASI Loader's version-resource threshold: >= 6.9.0, the release that
// added the marker export. The marker itself is definitive; a readable version
// resource is checked as well.

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
    Check(!LoaderVersionAccepted(0x00050000, 0), "loader: 5.x is refused");
    Check(!LoaderVersionAccepted(0x00060008, 0xFFFFFFFF), "loader: 6.8 is refused");
    Check(LoaderVersionAccepted(0x00060009, 0), "loader: 6.9.0 is accepted");
    Check(LoaderVersionAccepted(0x0006000A, 0), "loader: 6.10 is accepted");
    Check(LoaderVersionAccepted(0x00070000, 0), "loader: 7.0 is accepted");

    const LoaderVersion loader = QueryLoaderVersion();
    Check(!loader.present, "loader: the test process has no Ultimate ASI Loader");
    Check(!loader.versionKnown, "loader: no version is reported without a loader");

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

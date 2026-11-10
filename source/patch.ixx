module;

#include <common.hxx>

export module patch;

import common;
import game;
import improvements;

// Signature scanning, byte/introspection helpers and the two patch primitives
// that need more than a direct injector call. Everything else (MakeNOP,
// MakeCALL, MakeJMP, WriteMemory) is called directly by the module that owns the
// site.

// A recovered signature is written the way IDA prints it: two characters per
// byte, `??` for a wildcard, sometimes a three-digit token. This scanner wants
// one `?` per wildcard byte, so recovered strings are normalised here, once, at
// the boundary -- the feature modules keep the recovered text verbatim.
export std::string NormaliseSignature(std::string_view recovered)
{
    std::string result;
    size_t offset = 0;
    while (offset < recovered.size())
    {
        while (offset < recovered.size() && std::isspace(static_cast<unsigned char>(recovered[offset])))
            ++offset;

        const auto start = offset;
        while (offset < recovered.size() && !std::isspace(static_cast<unsigned char>(recovered[offset])))
            ++offset;
        if (start == offset)
            break;

        const auto token = recovered.substr(start, offset - start);
        if (!result.empty())
            result.push_back(' ');

        if (token.find_first_not_of('?') == std::string_view::npos)
        {
            result.push_back('?');
            continue;
        }
        for (const auto character : token)
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
    return result;
}

// Tries each candidate signature in order and returns the first one that matches
// at least once. A one-string call is a single signature, a two-string call is a
// build variant, a three-string call is three builds. `count` is a scan hint, not
// a minimum: the scan stops after that many hits and returns what it found.
//
// The scan covers the game image's executable range (game.ixx), which is what
// the vendor scanner covers for a module -- and it keeps working when the tools
// have loaded a copy of that image instead of the image itself.
//
// The scanner caches hits per signature string for the life of the process: a
// second scan of the same string answers from that cache whatever `count` asks
// for. A signature that has to be scanned with two different counts is two
// signatures.
export template <size_t count = 1, typename... Args>
hook::pattern find_pattern(Args... args)
{
    const auto [begin, end] = ImageScanRange();
    hook::pattern pattern;
    ((pattern = hook::pattern(begin, end, NormaliseSignature(args)), !pattern.count_hint(count).empty()) || ...);
    return pattern;
}

// OpenPreCEPatch's P13 rule: a multi-site arm whose original read or patched a
// fixed set of sites refuses a partial install. A different count is a view that
// does not have the shape the arm was recovered from, and installing the subset
// that happens to match is worse than leaving the image alone.
//
// The 1:1 configuration is not guarded: with the failsafe group off this always
// answers true, so an arm keeps the original's partial write (and, for
// NoImpostorHeightCutoff on 1.0.0.0, the original's fault).
export bool ExpectMatchCount(size_t found, size_t expected, const char* what) noexcept
{
    if (found == expected)
        return true;
    if (!ImprovementEnabled(Improvement::failsafe))
        return true;

    Log::Print("arm {}: {} match(es), expected {}: skipped", what, found, expected);
    return false;
}

export bool ExpectMatchCount(hook::pattern& pattern, size_t expected, const char* what) noexcept
{
    return ExpectMatchCount(pattern.size(), expected, what);
}

export template <size_t n>
std::string pattern_str(const std::array<uint8_t, n>& bytes){
    std::string result;
    for (size_t i = 0; i < n; ++i)
        result += std::format("{:02X} ", bytes[i]);
    return result;
}

export template <std::integral T>
std::string pattern_str(T value)
{
    if constexpr (std::is_same_v<T, char>)
        return std::format("{} ", value);
    else
        return std::format("{:02X} ", value);
}

export template <std::integral T, typename... Rest>
std::string pattern_str(T value, Rest... rest)
{
    return pattern_str(value) + pattern_str(rest...);
}

export std::string pattern_str(std::string_view text)
{
    std::string result;
    for (const auto character : text)
        result += std::format("{:02X} ", static_cast<uint8_t>(character));
    return result;
}

export template <typename T>
std::array<uint8_t, sizeof(T)> to_bytes(const T& object)
{
    static_assert(std::is_trivially_copyable_v<T>, "not a TriviallyCopyable type");
    std::array<uint8_t, sizeof(T)> bytes;
    const auto* begin = reinterpret_cast<const uint8_t*>(std::addressof(object));
    std::copy(begin, begin + sizeof(T), bytes.begin());
    return bytes;
}

export template <typename T>
T& from_bytes(const std::array<uint8_t, sizeof(T)>& bytes, T& object)
{
    static_assert(std::is_trivially_copyable_v<T>, "not a TriviallyCopyable type");
    auto* begin = reinterpret_cast<uint8_t*>(std::addressof(object));
    std::copy(bytes.begin(), bytes.end(), begin);
    return object;
}

export template <class T, class T1>
T from_bytes(const T1& bytes)
{
    static_assert(std::is_trivially_copyable_v<T>, "not a TriviallyCopyable type");
    T object;
    auto* begin = reinterpret_cast<uint8_t*>(std::addressof(object));
    std::copy(std::begin(bytes), std::end(bytes) - (sizeof(T1) - sizeof(T)), begin);
    return object;
}

// A byte run that can be written and put back, for the sites that are patched
// and later restored rather than hooked.
export class raw_mem
{
public:
    raw_mem(injector::memory_pointer_tr address, std::initializer_list<uint8_t> bytes, bool offsetBack = false)
    {
        ptr = address.as_int() - (offsetBack ? bytes.size() : 0);
        new_code.assign(bytes);
        old_code.resize(new_code.size());
        ReadMemoryRaw(ptr, old_code.data(), old_code.size(), true);
    }

    void Write()
    {
        WriteMemoryRaw(ptr, new_code.data(), new_code.size(), true);
    }

    void Restore()
    {
        WriteMemoryRaw(ptr, old_code.data(), old_code.size(), true);
    }

    size_t Size() const
    {
        return old_code.size();
    }

private:
    injector::memory_pointer ptr;
    std::vector<uint8_t> old_code;
    std::vector<uint8_t> new_code;
};

// Import address table replacement that keeps the replaced target, for the
// imports a module has to intercept before the game calls them. Regular and
// delay-loaded imports are both walked; `Name@12` selects an ordinal import.
export class IATHook
{
public:
    template <typename... Hooks>
    static std::map<std::string, void*> Replace(HMODULE module, std::string_view library, Hooks&&... hooks)
    {
        std::map<std::string, void*> originals;
        Walk(module, library, [&](const Import& import)
        {
            (TryReplace(import, hooks, originals), ...);
        });
        return originals;
    }

private:
    // delayimp.h is not part of the SDK set a module unit may include, and the
    // layout is fixed by the PE format.
    struct DelayLoadDescriptor
    {
        DWORD Attributes;
        DWORD DllNameRVA;
        DWORD ModuleHandleRVA;
        DWORD ImportAddressTableRVA;
        DWORD ImportNameTableRVA;
        DWORD BoundImportAddressTableRVA;
        DWORD UnloadInformationTableRVA;
        DWORD TimeDateStamp;
    };
    static_assert(sizeof(DelayLoadDescriptor) == 32, "Invalid structure size of DelayLoadDescriptor");

    struct Import
    {
        std::string_view name;
        uint16_t ordinal{};
        void** slot{};
    };

    template <typename Visitor>
    static void Walk(HMODULE module, std::string_view library, Visitor&& visitor)
    {
        const auto base = reinterpret_cast<uintptr_t>(module);
        if (base == 0)
            return;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0)
            return;

        for (auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
             descriptor->Name != 0; ++descriptor)
        {
            if (!iequals(std::string_view(reinterpret_cast<const char*>(base + descriptor->Name)), library))
                continue;

            auto* slots = reinterpret_cast<void**>(base + descriptor->FirstThunk);
            const auto* thunks = descriptor->OriginalFirstThunk != 0
                ? reinterpret_cast<const IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk)
                : nullptr;

            for (size_t index = 0; slots[index] != nullptr; ++index)
            {
                Import import{ .slot = &slots[index] };
                if (thunks != nullptr)
                {
                    if (thunks[index].u1.AddressOfData == 0)
                        break;
                    if (IMAGE_SNAP_BY_ORDINAL(thunks[index].u1.Ordinal))
                        import.ordinal = static_cast<uint16_t>(IMAGE_ORDINAL(thunks[index].u1.Ordinal));
                    else
                        import.name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + thunks[index].u1.AddressOfData)->Name;
                }
                visitor(import);
            }
        }

        const auto& delay = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if (delay.VirtualAddress == 0 || delay.Size == 0)
            return;

        for (auto* descriptor = reinterpret_cast<const DelayLoadDescriptor*>(base + delay.VirtualAddress);
             descriptor->DllNameRVA != 0; ++descriptor)
        {
            if (descriptor->ImportNameTableRVA == 0 ||
                !iequals(std::string_view(reinterpret_cast<const char*>(base + descriptor->DllNameRVA)), library))
                continue;

            auto* slots = reinterpret_cast<void**>(base + descriptor->ImportAddressTableRVA);
            const auto* thunks = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + descriptor->ImportNameTableRVA);
            for (size_t index = 0; slots[index] != nullptr; ++index)
            {
                if (thunks[index].u1.AddressOfData == 0)
                    break;
                Import import{ .slot = &slots[index] };
                if (IMAGE_SNAP_BY_ORDINAL(thunks[index].u1.Ordinal))
                    import.ordinal = static_cast<uint16_t>(IMAGE_ORDINAL(thunks[index].u1.Ordinal));
                else
                    import.name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + thunks[index].u1.AddressOfData)->Name;
                visitor(import);
            }
        }
    }

    static void TryReplace(const Import& import, const std::pair<std::string_view, void*>& hook, std::map<std::string, void*>& originals)
    {
        auto name = hook.first;
        uint16_t ordinal{};
        if (const auto at = name.find_last_of('@'); at != std::string_view::npos)
        {
            ordinal = static_cast<uint16_t>(std::strtoul(std::string(name.substr(at + 1)).c_str(), nullptr, 10));
            name = name.substr(0, at);
        }

        const bool matches = import.ordinal != 0 ? import.ordinal == ordinal : iequals(import.name, name);
        if (!matches || import.slot == nullptr)
            return;

        DWORD previous{};
        if (!VirtualProtect(import.slot, sizeof(void*), PAGE_READWRITE, &previous))
            return;

        originals.try_emplace(std::string(hook.first), *import.slot);
        *import.slot = hook.second;

        VirtualProtect(import.slot, sizeof(void*), previous, &previous);
    }
};

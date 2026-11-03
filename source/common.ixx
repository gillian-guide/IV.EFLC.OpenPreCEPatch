// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.common — the shared types every module speaks.
//
// The FusionFix layout: one Event type with three global tiers, a Game singleton that
// owns the mapped image/build/config the handlers read, and the shared scan/byte
// helpers. `common.hxx` is third-party/platform headers only; the INI document is
// `opp.config`, imported here so Game::Option can call Config::Option.

module;

#include "common.hxx"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <functional>
#include <future>
#include <iomanip>
#include <list>
#include <map>
#include <mutex>
#include <sstream>
#include <tuple>

export module opp.common;

import opp.config;
import opp.log;

export namespace opp
{
    // Result code of a patch pass; `detail` is always a fixed literal, never an
    // allocated message (the original reports through the log).
    enum class PatchResultCode
    {
        ok,
        out_of_bounds,
        invalid_input,
        io_error,
        parse_error,
        not_found,
        unsupported_platform,
        mismatch,
    };

    struct PatchResult final
    {
        PatchResultCode code{PatchResultCode::ok};
        std::string_view detail{};
        constexpr explicit operator bool() const noexcept { return code == PatchResultCode::ok; }
    };

    // The original mod's classification enum. The numeric value is the build id
    // every build mask indexes (bit i == ordinal i); it must not change.
    enum class GameBuild : uint8_t
    {
        unknown = 0,
        v1000 = 1,
        v1040 = 5,
        v1004 = 6,
        v1060 = 7,
        v1070 = 8,
        v1080 = 9,
        v1100 = 10,
        v1110 = 11,
        v1120 = 12,
        v1130 = 13,
        v1061 = 15,
        v12043 = 18,
    };

    // The game image as it is mapped in the process. Installers write through it;
    // the standalone tools hand in a mapped copy of the same executable.
    struct GameImage
    {
        uint8_t* base{};
        size_t size{};
    };

    // A PE32 image that the dump/tests can map at its preferred base.
    struct ModuleView
    {
        const uint8_t* base{};
        size_t size{};
    };

    // One bounded signature scan result.
    struct Match
    {
        const uint8_t* address{};
    };

    // The range the original's hook::pattern(str) scans: module base up to the end
    // of the last executable section. The installers used to hand Hooking.Patterns
    // the whole SizeOfImage, which scans .rdata/.data/.rsrc too (2.6x more bytes on
    // 1.0.8.0) and can match outside code. Falls back to the whole image when the
    // view is not a readable PE32 image, so synthetic test buffers keep working.
    ModuleView ExecutableView(ModuleView image) noexcept;

    // Optional per-arm instrumentation for the offline dump tool. The ASI never
    // installs an observer, so the release path stays allocation- and call-free.
    struct PatchObserver
    {
        void* user{};
        void (*begin)(void* user, const char* name){};
        void (*end)(void* user, const char* name){};
    };

    // The FusionFix event: a mutex-guarded std::function handler list. A subscription
    // returns the token that removes it; executeAll runs a snapshot in subscription
    // order, executeAllAsync runs each handler on its own thread. The tiers are
    // Event<> (the handlers read Game).
    template <typename... Args>
    class Event : public std::function<void(Args...)>
    {
    public:
        using std::function<void(Args...)>::function;

    private:
        struct Handler
        {
            std::function<void(Args...)> callback;
            std::atomic<bool> active{true};

            explicit Handler(std::function<void(Args...)>&& fn) : callback(std::move(fn)) {}
        };

        mutable std::mutex handlersMutex;
        std::list<std::shared_ptr<Handler>> handlers;

        auto snapshot() const
        {
            std::lock_guard lock(handlersMutex);
            return std::vector<std::shared_ptr<Handler>>(handlers.begin(), handlers.end());
        }

    public:
        auto operator+=(std::function<void(Args...)>&& callback) -> std::function<void()>
        {
            auto handler = std::make_shared<Handler>(std::move(callback));
            {
                std::lock_guard lock(handlersMutex);
                handlers.push_back(handler);
            }
            return [this, handler]()
            {
                std::lock_guard lock(handlersMutex);
                handler->active = false;
                handlers.remove(handler);
            };
        }

        void executeAll(Args... args) const
        {
            for (const auto& handler : snapshot())
            {
                if (handler->active)
                    handler->callback(args...);
            }
        }

        std::vector<std::future<void>> executeAllAsync(Args... args) const
        {
            std::vector<std::future<void>> futures;
            for (const auto& handler : snapshot())
            {
                futures.emplace_back(std::async(std::launch::async,
                    [handler, values = std::tuple<Args...>(args...)]() mutable
                    {
                        if (handler->active)
                            std::apply(handler->callback, values);
                    }));
            }
            return futures;
        }
    };

    // The three tiers, run in this order by PatchGame. The publish tier is
    // load-bearing: a site's original bytes must be read (and its resume
    // published) before any install overwrites them.
    Event<>& PublishEvent() noexcept;
    Event<>& InstallEvent() noexcept;
    Event<>& PostEvent() noexcept;

    // The process-wide install context: the image the arms patch, the build the
    // original's classifier selected and the parsed INI. PatchGame initializes it
    // once; a feature handler reads it with Game::Get() / Game::Option(...).
    class Game
    {
    public:
        static Game& Get() noexcept
        {
            static Game game;
            return game;
        }

        static void Initialize(GameImage image_, GameBuild build_, const Config& config_) noexcept
        {
            Game& game = Get();
            game.image = image_;
            game.gameBase = reinterpret_cast<uintptr_t>(image_.base);
            game.build = build_;
            game.config = &config_;
        }

        // An option is on when its value parses to exactly 1. An absent key uses
        // the default the option's own arm substitutes, which is not always off.
        static bool Option(const char* key, const char* defaultValue)
        {
            const Game& game = Get();
            return game.config != nullptr && game.config->Option(key, defaultValue);
        }

        GameImage image{};
        uintptr_t gameBase{};
        GameBuild build{GameBuild::unknown};
        const Config* config{};
    };

    // The install trace. TraceArm names the arm that is running (the matrix
    // attributes every changed byte to it) and closes the previous arm's observer
    // bracket; EndArmTrace closes the last one when a tier finishes.
    void TraceArm(const char* name);
    void EndArmTrace() noexcept;
    const char* CurrentArm() noexcept;
    void SetPatchObserver(const PatchObserver& observer) noexcept;
    void LogPatternMiss(std::string_view signature) noexcept;

    // Signature normalisation: "AA ?? 0xBB" -> "AA ? BB". Hooking.Patterns treats
    // every '?' as one wildcard byte, so the two-character wildcard must be
    // collapsed before the scan.
    std::string NormalizeSignature(std::string_view signature);

    // The original's hook::pattern over the Game image, with FusionFix's build
    // variants: each argument is a signature for a different build and the first
    // one that matches wins. `count` is the number of matches the caller needs
    // (0 scans every match, which is the old PatternAll).
    inline hook::pattern make_game_pattern(std::string_view signature)
    {
        const std::string normalized = NormalizeSignature(signature);
        if (normalized.empty())
            return hook::pattern{};
        const ModuleView view =
            ExecutableView({Game::Get().image.base, Game::Get().image.size});
        const uintptr_t begin = reinterpret_cast<uintptr_t>(view.base);
        return hook::make_range_pattern(begin, begin + view.size, normalized);
    }

    template <size_t count = 1, typename... Args>
    hook::pattern find_pattern(Args... args)
    {
        hook::pattern pattern;
        ((pattern = make_game_pattern(args), !pattern.count_hint(count).empty()) || ...);
        if (pattern.empty())
        {
            std::string_view first;
            ((first = std::string_view(args), true) || ...);
            LogPatternMiss(first);
        }
        return pattern;
    }

    // FusionFix's byte helpers, verbatim where the types exist in this tree.
    inline std::string format(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        std::vector<char> v(1024);
        while (true)
        {
            va_list args2;
            va_copy(args2, args);
            int res = vsnprintf(v.data(), v.size(), fmt, args2);
            if ((res >= 0) && (res < static_cast<int>(v.size())))
            {
                va_end(args);
                va_end(args2);
                return std::string(v.data());
            }
            size_t size;
            if (res < 0)
                size = v.size() * 2;
            else
                size = static_cast<size_t>(res) + 1;
            v.clear();
            v.resize(size);
            va_end(args2);
        }
    }

    template <typename T>
    std::array<uint8_t, sizeof(T)> to_bytes(const T& object)
    {
        std::array<uint8_t, sizeof(T)> bytes;
        const uint8_t* begin = reinterpret_cast<const uint8_t*>(std::addressof(object));
        const uint8_t* end = begin + sizeof(T);
        std::copy(begin, end, std::begin(bytes));
        return bytes;
    }

    template <typename T>
    T& from_bytes(const std::array<uint8_t, sizeof(T)>& bytes, T& object)
    {
        static_assert(std::is_trivially_copyable<T>::value, "not a TriviallyCopyable type");
        uint8_t* begin_object = reinterpret_cast<uint8_t*>(std::addressof(object));
        std::copy(std::begin(bytes), std::end(bytes), begin_object);
        return object;
    }

    template <class T, class T1>
    T from_bytes(const T1& bytes)
    {
        static_assert(std::is_trivially_copyable<T>::value, "not a TriviallyCopyable type");
        T object;
        uint8_t* begin_object = reinterpret_cast<uint8_t*>(std::addressof(object));
        std::copy(std::begin(bytes), std::end(bytes) - (sizeof(T1) - sizeof(T)), begin_object);
        return object;
    }

    template <size_t n>
    std::string pattern_str(const std::array<uint8_t, n> bytes)
    {
        std::string result;
        for (size_t i = 0; i < n; i++)
        {
            result += format("%02X ", bytes[i]);
        }
        return result;
    }

    template <typename T>
    std::string pattern_str(T t)
    {
        return std::string((std::is_same<T, char>::value ? format("%c ", t) : format("%02X ", t)));
    }

    template <typename T, typename... Rest>
    std::string pattern_str(T t, Rest... rest)
    {
        return std::string((std::is_same<T, char>::value ? format("%c ", t) : format("%02X ", t)) + pattern_str(rest...));
    }

    inline std::string pattern_str(std::string_view str)
    {
        std::stringstream str_stream;
        for (const auto& item : str)
        {
            str_stream << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                       << +uint8_t(item) << " ";
        }
        return str_stream.str();
    }

    // FusionFix's module-path helpers: the path/name of this module or the
    // executable, with the retry loop for a path longer than MAX_PATH.
    template <class T = std::filesystem::path>
    T GetModulePath(HMODULE hModule)
    {
        static constexpr auto INITIAL_BUFFER_SIZE = MAX_PATH;
        static constexpr auto MAX_ITERATIONS = 7;

        if constexpr (std::is_same_v<T, std::filesystem::path>)
        {
            std::u16string ret;
            std::filesystem::path pathret;
            auto bufferSize = INITIAL_BUFFER_SIZE;
            for (size_t iterations = 0; iterations < MAX_ITERATIONS; ++iterations)
            {
                ret.resize(bufferSize);
                size_t charsReturned = 0;
                charsReturned = GetModuleFileNameW(hModule, (LPWSTR)&ret[0], bufferSize);
                if (charsReturned < ret.length())
                {
                    ret.resize(charsReturned);
                    pathret = ret;
                    return pathret;
                }
                else
                {
                    bufferSize *= 2;
                }
            }
        }
        else
        {
            T ret;
            auto bufferSize = INITIAL_BUFFER_SIZE;
            for (size_t iterations = 0; iterations < MAX_ITERATIONS; ++iterations)
            {
                ret.resize(bufferSize);
                size_t charsReturned = 0;
                if constexpr (std::is_same_v<T, std::string>)
                    charsReturned = GetModuleFileNameA(hModule, &ret[0], bufferSize);
                else
                    charsReturned = GetModuleFileNameW(hModule, &ret[0], bufferSize);
                if (charsReturned < ret.length())
                {
                    ret.resize(charsReturned);
                    return ret;
                }
                else
                {
                    bufferSize *= 2;
                }
            }
        }
        return T();
    }

    template <class T = std::filesystem::path>
    T GetThisModulePath()
    {
        HMODULE hm = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&Game::Get, &hm);
        T r = GetModulePath<T>(hm);
        if constexpr (std::is_same_v<T, std::filesystem::path>)
            return r.parent_path();
        else if constexpr (std::is_same_v<T, std::string>)
            r = r.substr(0, r.find_last_of("/\\") + 1);
        else
            r = r.substr(0, r.find_last_of(L"/\\") + 1);
        return r;
    }

    template <class T = std::filesystem::path>
    T GetThisModuleName()
    {
        HMODULE hm = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&Game::Get, &hm);
        const T moduleFileName = GetModulePath<T>(hm);

        if constexpr (std::is_same_v<T, std::filesystem::path>)
            return moduleFileName.filename();
        else if constexpr (std::is_same_v<T, std::string>)
            return moduleFileName.substr(moduleFileName.find_last_of("/\\") + 1);
        else
            return moduleFileName.substr(moduleFileName.find_last_of(L"/\\") + 1);
    }

    template <class T = std::filesystem::path>
    T GetExeModulePath()
    {
        T r = GetModulePath<T>(NULL);

        if constexpr (std::is_same_v<T, std::filesystem::path>)
            return r.parent_path();
        else if constexpr (std::is_same_v<T, std::string>)
            r = r.substr(0, r.find_last_of("/\\") + 1);
        else
            r = r.substr(0, r.find_last_of(L"/\\") + 1);
        return r;
    }

    template <class T = std::filesystem::path>
    T GetExeModuleName()
    {
        const T moduleFileName = GetModulePath<T>(NULL);
        if constexpr (std::is_same_v<T, std::filesystem::path>)
            return moduleFileName.filename();
        else if constexpr (std::is_same_v<T, std::string>)
            return moduleFileName.substr(moduleFileName.find_last_of("/\\") + 1);
        else
            return moduleFileName.substr(moduleFileName.find_last_of(L"/\\") + 1);
    }

    // FusionFix's IAT replacement, verbatim: walk the import descriptors of
    // `target_module` and replace the slot of every `dll_name!name` (or
    // `name@ordinal`) in `inputs`. Used by features that hook an imported function
    // rather than a scanned site.
    class IATHook
    {
    public:
        template <class... Ts>
        static auto Replace(HMODULE target_module, std::string_view dll_name, Ts&&... inputs)
        {
            std::map<std::string, std::future<void*>> originalPtrs;

            const DWORD_PTR instance = reinterpret_cast<DWORD_PTR>(target_module);
            const PIMAGE_NT_HEADERS ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(
                instance + reinterpret_cast<PIMAGE_DOS_HEADER>(instance)->e_lfanew);
            PIMAGE_IMPORT_DESCRIPTOR pImports = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
                instance +
                ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
                    .VirtualAddress);
            DWORD dwProtect[2];

            // Regular imports
            for (; pImports->Name != 0; pImports++)
            {
                if (_stricmp(reinterpret_cast<const char*>(instance + pImports->Name),
                             dll_name.data()) == 0)
                {
                    if (pImports->OriginalFirstThunk != 0)
                    {
                        const PIMAGE_THUNK_DATA pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(
                            instance + pImports->OriginalFirstThunk);

                        for (ptrdiff_t j = 0; pThunk[j].u1.AddressOfData != 0; j++)
                        {
                            auto pAddress =
                                reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
                            if (!pAddress) continue;
                            VirtualProtect(pAddress, sizeof(void*),
                                           PAGE_EXECUTE_READWRITE, &dwProtect[0]);
                            ([&]
                            {
                                auto name = std::string_view(std::get<0>(inputs));
                                auto num = std::string("-1");
                                if (name.contains("@"))
                                {
                                    num = name.substr(name.find_last_of("@") + 1);
                                    name = name.substr(0, name.find_last_of("@"));
                                }

                                if (pThunk[j].u1.Ordinal & IMAGE_ORDINAL_FLAG)
                                {
                                    try
                                    {
                                        if (IMAGE_ORDINAL(pThunk[j].u1.Ordinal) ==
                                            std::stoi(num.data()))
                                        {
                                            originalPtrs[std::get<0>(inputs)] =
                                                std::async(std::launch::deferred, [&]() -> void* {
                                                    return *pAddress;
                                                });
                                            originalPtrs[std::get<0>(inputs)].wait();
                                            *pAddress = std::get<1>(inputs);
                                        }
                                    }
                                    catch (...) {}
                                }
                                else if ((*pAddress &&
                                          *pAddress == (void*)GetProcAddress(
                                                            GetModuleHandleA(dll_name.data()),
                                                            name.data())) ||
                                         (strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
                                                     instance + pThunk[j].u1.AddressOfData)
                                                     ->Name,
                                                 name.data()) == 0))
                                {
                                    originalPtrs[std::get<0>(inputs)] =
                                        std::async(std::launch::deferred,
                                                   [&]() -> void* { return *pAddress; });
                                    originalPtrs[std::get<0>(inputs)].wait();
                                    *pAddress = std::get<1>(inputs);
                                }
                            }(), ...);
                            VirtualProtect(pAddress, sizeof(void*), dwProtect[0], &dwProtect[1]);
                        }
                    }
                    else
                    {
                        auto pFunctions =
                            reinterpret_cast<void**>(instance + pImports->FirstThunk);

                        for (ptrdiff_t j = 0; pFunctions[j] != nullptr; j++)
                        {
                            auto pAddress = &pFunctions[j];
                            VirtualProtect(pAddress, sizeof(void*),
                                           PAGE_EXECUTE_READWRITE, &dwProtect[0]);
                            ([&]
                            {
                                if (*pAddress &&
                                    *pAddress == (void*)GetProcAddress(
                                                      GetModuleHandleA(dll_name.data()),
                                                      std::get<0>(inputs)))
                                {
                                    originalPtrs[std::get<0>(inputs)] =
                                        std::async(std::launch::deferred,
                                                   [&]() -> void* { return *pAddress; });
                                    originalPtrs[std::get<0>(inputs)].wait();
                                    *pAddress = std::get<1>(inputs);
                                }
                            }(), ...);
                            VirtualProtect(pAddress, sizeof(void*), dwProtect[0], &dwProtect[1]);
                        }
                    }
                }
            }

            // Delay-loaded imports
            PIMAGE_DELAYLOAD_DESCRIPTOR pDelayImports =
                reinterpret_cast<PIMAGE_DELAYLOAD_DESCRIPTOR>(
                    instance +
                    ntHeader->OptionalHeader
                        .DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT]
                        .VirtualAddress);

            for (; pDelayImports->DllNameRVA != 0; pDelayImports++)
            {
                if (_stricmp(reinterpret_cast<const char*>(instance + pDelayImports->DllNameRVA),
                             dll_name.data()) == 0)
                {
                    PIMAGE_THUNK_DATA pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(
                        instance + pDelayImports->ImportNameTableRVA);
                    PIMAGE_THUNK_DATA pAddressThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(
                        instance + pDelayImports->ImportAddressTableRVA);

                    for (; pThunk->u1.AddressOfData != 0; pThunk++, pAddressThunk++)
                    {
                        auto pAddress = reinterpret_cast<void**>(pAddressThunk);
                        VirtualProtect(pAddress, sizeof(void*), PAGE_EXECUTE_READWRITE,
                                       &dwProtect[0]);
                        ([&]
                        {
                            auto name = std::string_view(std::get<0>(inputs));
                            auto num = std::string("-1");
                            if (name.contains("@"))
                            {
                                num = name.substr(name.find_last_of("@") + 1);
                                name = name.substr(0, name.find_last_of("@"));
                            }

                            if (pThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)
                            {
                                try
                                {
                                    if (IMAGE_ORDINAL(pThunk->u1.Ordinal) ==
                                        std::stoi(num.data()))
                                    {
                                        originalPtrs[std::get<0>(inputs)] =
                                            std::async(std::launch::deferred, [&]() -> void* {
                                                return *pAddress;
                                            });
                                        originalPtrs[std::get<0>(inputs)].wait();
                                        *pAddress = std::get<1>(inputs);
                                    }
                                }
                                catch (...) {}
                            }
                            else if ((*pAddress &&
                                      *pAddress == (void*)GetProcAddress(
                                                        GetModuleHandleA(dll_name.data()),
                                                        name.data())) ||
                                     (strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
                                                 instance + pThunk->u1.AddressOfData)
                                                 ->Name,
                                             name.data()) == 0))
                            {
                                originalPtrs[std::get<0>(inputs)] =
                                    std::async(std::launch::deferred,
                                               [&]() -> void* { return *pAddress; });
                                originalPtrs[std::get<0>(inputs)].wait();
                                *pAddress = std::get<1>(inputs);
                            }
                        }(), ...);
                        VirtualProtect(pAddress, sizeof(void*), dwProtect[0], &dwProtect[1]);
                    }
                }
            }

            return originalPtrs;
        }
    };

    // Rebased globals hold 32-bit VAs of game functions, not typed pointers.
    // This is the one conversion back to a callable: the caller may only use it
    // after its scan proved the address and the signature (the arm's pattern is
    // that proof), and the call's ABI is the one the recovered body transcribes.
    template <typename Fn>
    constexpr Fn GameFunction(uintptr_t address) noexcept
    {
        static_assert(std::is_pointer_v<Fn>,
                      "GameFunction returns a function pointer type");
        return reinterpret_cast<Fn>(address);
    }

    // The same conversion for a rebased 32-bit address the body reads (or
    // calls) as a typed pointer: usually a `zpatch_*` slot's value, always an
    // address the arm's scan proved is the game site its body transcribes, and
    // `T` is the pointer type that body uses. The double cast lives here, named
    // once, instead of being spelled at every site.
    template <typename T>
    constexpr T Rebased(uintptr_t address) noexcept
    {
        static_assert(std::is_pointer_v<T>, "Rebased returns a pointer type");
        return reinterpret_cast<T>(static_cast<uintptr_t>(address));
    }

    // Fundamental float/double constants used at ABI stores. The bit patterns
    // are pinned here once; modules store std::bit_cast<uint32_t>(constant) at
    // the boundary instead of spelling the hex bits in logic.
    inline constexpr float kDrivebyFovDegrees = 45.0f;
    inline constexpr float kHalfPi = 1.5707964f;          // pi/2
    inline constexpr float kDefaultConeOffset = 2.0f;
    inline constexpr double kCutsceneZero = 0.0;
    inline constexpr double kCutsceneLimit = 0.3333;      // recovered bits, not 1/3
    inline constexpr float kCutsceneEpsilon = 0.05f;
    inline constexpr float kCutsceneHalf = 0.5f;
    inline constexpr float kBikePhysicsFloor = 0.0066666667f;
    static_assert(std::bit_cast<uint32_t>(kDrivebyFovDegrees) == 0x42340000u);
    static_assert(std::bit_cast<uint32_t>(kHalfPi) == 0x3FC90FDBu);
    static_assert(std::bit_cast<uint32_t>(kDefaultConeOffset) == 0x40000000u);
    static_assert(std::bit_cast<uint64_t>(kCutsceneZero) == 0x0ull);
    static_assert(std::bit_cast<uint64_t>(kCutsceneLimit) == 0x3FD554C985F06F69ull);
    static_assert(std::bit_cast<uint32_t>(kCutsceneEpsilon) == 0x3D4CCCCDu);
    static_assert(std::bit_cast<uint32_t>(kCutsceneHalf) == 0x3F000000u);
    static_assert(std::bit_cast<uint32_t>(kBikePhysicsFloor) == 0x3BDA740Eu);

    // Deleters for C-owned resources kept RAII: the allocation itself stays the
    // original's (malloc/free), only the ownership is expressed in the type.
    struct FreeDeleter final
    {
        void operator()(void* pointer) const noexcept { std::free(pointer); }
    };

    struct FileCloser final
    {
        void operator()(std::FILE* file) const noexcept
        {
            if (file != nullptr)
                std::fclose(file);
        }
    };
} // namespace opp

namespace opp
{
    Event<>& PublishEvent() noexcept
    {
        static Event<> event;
        return event;
    }

    Event<>& InstallEvent() noexcept
    {
        static Event<> event;
        return event;
    }

    Event<>& PostEvent() noexcept
    {
        static Event<> event;
        return event;
    }

    namespace
    {
        const char* g_currentArm = "startup";
        bool g_armOpen = false;
        PatchObserver g_observer{};
    } // namespace

    void SetPatchObserver(const PatchObserver& observer) noexcept
    {
        g_observer = observer;
    }

    void TraceArm(const char* name)
    {
        // Close the previous arm's observer bracket before the next arm starts:
        // the dump's write observer diffs the image per arm and attributes the
        // changes to the arm that made them.
        EndArmTrace();
        g_currentArm = name != nullptr ? name : "?";
        g_armOpen = true;
        // The log names every arm before it runs, so a crash during the install
        // pass leaves the last arm visible.
        Log("arm %s", g_currentArm);
        if (g_observer.begin != nullptr)
            g_observer.begin(g_observer.user, g_currentArm);
    }

    void EndArmTrace() noexcept
    {
        if (!g_armOpen)
            return;
        g_armOpen = false;
        if (g_observer.end != nullptr)
            g_observer.end(g_observer.user, g_currentArm);
    }

    const char* CurrentArm() noexcept
    {
        return g_currentArm;
    }

    void LogPatternMiss(std::string_view signature) noexcept
    {
        // One line per miss: arm, then the signature it asked for. The string
        // view points at a literal in the arm, so nothing is built at runtime.
        Log("arm %s: no match for signature [%.*s]", g_currentArm,
            static_cast<int>(signature.size()), signature.data());
    }

    namespace
    {
        constexpr uint32_t kNibbleMask = 0xFu;      // one hex digit

        bool IsWhite(char c) noexcept
        {
            constexpr std::string_view kWhitespace{" \t\r\n\f\v"};
            return kWhitespace.find(c) != std::string_view::npos;
        }

        bool ParseToken(std::string_view token, int& value, bool& wildcard)
        {
            if (token == "?" || token == "??")
            {
                wildcard = true;
                value = 0;
                return true;
            }

            wildcard = false;
            size_t i = 0;
            while (i < token.size() && IsWhite(token[i]))
                ++i;

            bool negative = false;
            if (i < token.size() && (token[i] == '+' || token[i] == '-'))
            {
                negative = token[i] == '-';
                ++i;
            }

            if (i + 1 < token.size() && token[i] == '0' &&
                (token[i + 1] == 'x' || token[i + 1] == 'X'))
                i += 2;

            uint32_t accumulated = 0;
            for (; i < token.size(); ++i)
            {
                const char c = token[i];
                int digit;
                if (c >= '0' && c <= '9')
                    digit = c - '0';
                else if (c >= 'a' && c <= 'f')
                    digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    digit = c - 'A' + 10;
                else
                    break;

                accumulated = accumulated * 16u + static_cast<uint32_t>(digit);
            }

            // One signature byte.
            value = static_cast<int>((negative ? 0u - accumulated : accumulated) & 0xFFu);
            return true;
        }

        bool Normalize(std::string_view signature, std::string& out)
        {
            out.clear();
            out.reserve(signature.size());

            size_t i = 0;
            bool any = false;
            while (i < signature.size())
            {
                while (i < signature.size() && IsWhite(signature[i]))
                    ++i;
                if (i == signature.size())
                    break;

                const size_t start = i;
                while (i < signature.size() && !IsWhite(signature[i]))
                    ++i;
                const std::string_view token = signature.substr(start, i - start);

                int value = 0;
                bool wildcard = false;
                ParseToken(token, value, wildcard);

                if (!out.empty())
                    out.push_back(' ');

                if (wildcard)
                {
                    out.push_back('?');
                }
                else
                {
                    static const char kHex[] = "0123456789ABCDEF";
                    out.push_back(kHex[(value >> 4) & kNibbleMask]);
                    out.push_back(kHex[value & kNibbleMask]);
                }
                any = true;
            }
            return any;
        }
    } // namespace

    std::string NormalizeSignature(std::string_view signature)
    {
        std::string normalized;
        return Normalize(signature, normalized) ? normalized : std::string{};
    }

    ModuleView ExecutableView(ModuleView image) noexcept
    {
        const ModuleView whole{image.base, image.size};
        if (!image.base || image.size < sizeof(IMAGE_DOS_HEADER))
            return whole;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return whole;

        const size_t nt_offset = static_cast<size_t>(dos->e_lfanew);
        if (nt_offset + sizeof(IMAGE_NT_HEADERS) > image.size)
            return whole;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.base + nt_offset);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return whole;

        const size_t section_table = static_cast<size_t>(
            reinterpret_cast<uintptr_t>(IMAGE_FIRST_SECTION(nt)) -
            reinterpret_cast<uintptr_t>(image.base));
        const size_t section_count = nt->FileHeader.NumberOfSections;
        if (section_count == 0 || section_table < nt_offset ||
            section_table + section_count * sizeof(IMAGE_SECTION_HEADER) > image.size)
            return whole;

        const auto* sections =
            reinterpret_cast<const IMAGE_SECTION_HEADER*>(image.base + section_table);
        size_t end = 0;
        for (size_t i = 0; i < section_count; ++i)
        {
            const IMAGE_SECTION_HEADER& section = sections[i];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const size_t section_end = static_cast<size_t>(section.VirtualAddress) +
                                       section.Misc.VirtualSize;
            if (section_end > end)
                end = section_end;
        }

        // No executable section, or headers that point outside the mapped image:
        // keep the old whole-image behaviour rather than silently scanning less.
        if (end == 0 || end > image.size)
            return whole;
        return {image.base, end};
    }
} // namespace opp

#if defined(OPCEP_DUMP_TOOL)
// The dump records every module-relative address an installer emits, so the
// write-set baseline can canonicalise those operands. C linkage so the modules
// (this declaration) and the dump tool's plain TU (the definition) agree.
extern "C" void opp_record_dump_pointer(uint32_t value);
#endif

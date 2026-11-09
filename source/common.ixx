module;

#include <common.hxx>

export module common;

// The load-time context and the event tiers, plus the helpers every module
// shares. Imported by every module unit; imports nothing itself.

export class FusionFix
{
public:
    // Handlers are kept as shared_ptr records behind a mutex and dispatched from
    // a snapshot, so subscribing or unsubscribing from inside a handler is safe.
    // `operator+=` returns the unsubscribe token.
    template <typename... Args>
    class Event : public std::function<void(Args...)>
    {
    public:
        using std::function<void(Args...)>::function;

    private:
        struct Handler
        {
            std::function<void(Args...)> callback;
            std::atomic<bool> active{ true };

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

public:
    static Event<>& onInitEvent()
    {
        static Event<> InitEvent;
        return InitEvent;
    }
    static Event<>& onInitEventAsync()
    {
        static Event<> InitEventAsync;
        return InitEventAsync;
    }
    static Event<>& onShutdownEvent()
    {
        static Event<> ShutdownEvent;
        return ShutdownEvent;
    }
    static Event<>& onGameInitEvent()
    {
        static Event<> GameInitEvent;
        return GameInitEvent;
    }
    static Event<>& onGameProcessEvent()
    {
        static Event<> GameProcessEvent;
        return GameProcessEvent;
    }
    static Event<>& onMenuDrawingEvent()
    {
        static Event<> MenuDrawingEvent;
        return MenuDrawingEvent;
    }
    static Event<>& onMenuEnterEvent()
    {
        static Event<> MenuEnterEvent;
        return MenuEnterEvent;
    }
    static Event<>& onMenuExitEvent()
    {
        static Event<> MenuExitEvent;
        return MenuExitEvent;
    }
    static Event<bool>& onActivateApp()
    {
        static Event<bool> ActivateApp;
        return ActivateApp;
    }
    static Event<>& onBeforeReset()
    {
        static Event<> BeforeReset;
        return BeforeReset;
    }
    static Event<>& onEndScene()
    {
        static Event<> EndScene;
        return EndScene;
    }
    static Event<>& onReadGameConfig()
    {
        static Event<> ReadGameConfig;
        return ReadGameConfig;
    }
};

// One line-buffered trace file per module, opened once by the startup driver.
// A feature logs through Log::Print at the point of the decision it is tracing.
export class Log
{
public:
    static bool Open(const std::filesystem::path& path)
    {
        std::lock_guard lock(Mutex());
        auto& stream = Stream();
        if (stream.is_open())
            stream.close();
        FilePath() = path;
        stream.open(path, std::ios::out | std::ios::trunc | std::ios::binary);
        return stream.is_open();
    }

    static void Close()
    {
        std::lock_guard lock(Mutex());
        if (Stream().is_open())
            Stream().close();
    }

    static bool IsOpen()
    {
        std::lock_guard lock(Mutex());
        return Stream().is_open();
    }

    static const std::filesystem::path& Path() { return FilePath(); }

    static void Line(std::string_view text)
    {
        std::lock_guard lock(Mutex());
        auto& stream = Stream();
        if (!stream.is_open())
            return;
        stream << text << "\r\n";
        stream.flush();
    }

    template <typename... Args>
    static void Print(std::format_string<Args...> text, Args&&... args)
    {
        Line(std::format(text, std::forward<Args>(args)...));
    }

private:
    static std::mutex& Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }
    static std::ofstream& Stream()
    {
        static std::ofstream stream;
        return stream;
    }
    static std::filesystem::path& FilePath()
    {
        static std::filesystem::path path;
        return path;
    }
};

// The full path of a loaded module, or empty if the name cannot be read.
export std::wstring ModuleFileName(HMODULE module)
{
    static constexpr auto InitialBufferSize = MAX_PATH;
    static constexpr auto MaxIterations = 7;

    std::wstring name;
    auto bufferSize = InitialBufferSize;
    for (size_t iteration = 0; iteration < MaxIterations; ++iteration)
    {
        name.resize(bufferSize);
        const auto written = GetModuleFileNameW(module, name.data(), bufferSize);
        if (written < name.size())
        {
            name.resize(written);
            return name;
        }
        bufferSize *= 2;
    }
    return {};
}

export HMODULE GetThisModuleHandle()
{
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetThisModuleHandle), &module);
    return module;
}

// The install context: the module that was loaded, the executable it was loaded
// into, and that executable's image and file version. One instance, filled once
// by the startup driver before any event fires.
export class Game
{
public:
    using Version = std::array<uint16_t, 4>;

    static Game& Get()
    {
        static Game game;
        return game;
    }

    void Initialise(HMODULE module)
    {
        if (this->module != nullptr)
            return;

        this->module = module;
        modulePath = ModuleFileName(module);
        executablePath = ModuleFileName(nullptr);

        const auto image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
        imageBase = image;
        imageSize = nt->OptionalHeader.SizeOfImage;

        fileVersion = ReadFileVersion(executablePath);
    }

    HMODULE Module() const { return module; }
    const std::filesystem::path& ModulePath() const { return modulePath; }
    std::filesystem::path ModuleDirectory() const { return modulePath.parent_path(); }
    std::string ModuleName() const { return modulePath.filename().string(); }

    const std::filesystem::path& ExecutablePath() const { return executablePath; }
    std::filesystem::path ExecutableDirectory() const { return executablePath.parent_path(); }

    uintptr_t ImageBase() const { return imageBase; }
    size_t ImageSize() const { return imageSize; }
    bool Contains(uintptr_t address) const { return address >= imageBase && address - imageBase < imageSize; }

    const Version& FileVersion() const { return fileVersion; }

    std::string FileVersionString() const
    {
        return std::format("{}.{}.{}.{}", fileVersion[0], fileVersion[1], fileVersion[2], fileVersion[3]);
    }

    // The trace file the parity tools read. The shipped ASI never opens it: the
    // original writes no log at all.
    std::filesystem::path LogPath() const { return modulePath.parent_path() / L"ZolikaPatch.trace.log"; }

    // The offline tools drive the same install pipeline over a copy of a game
    // image loaded into their own address space, so the context is filled from
    // that image instead of from this process.
    void AttachImage(uintptr_t base, size_t size, const std::filesystem::path& executable, const Version& version)
    {
        imageBase = base;
        imageSize = size;
        executablePath = executable;
        fileVersion = version;
    }

private:
    std::filesystem::path WithExtension(std::wstring_view extension) const
    {
        auto path = modulePath;
        path.replace_extension(extension);
        return path;
    }

    static Version ReadFileVersion(const std::filesystem::path& path)
    {
        DWORD ignored{};
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size == 0)
            return {};

        std::vector<std::byte> block(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data()))
            return {};

        VS_FIXEDFILEINFO* info{};
        UINT length{};
        if (!VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&info), &length) || info == nullptr)
            return {};

        return { HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                 HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS) };
    }

    HMODULE module{};
    std::filesystem::path modulePath;
    std::filesystem::path executablePath;
    uintptr_t imageBase{};
    size_t imageSize{};
    Version fileVersion{};
};


// ANSI conversion with the same result as the GetModuleFileNameA form: the path
// helpers below hand out byte strings the Win32 API can take back.
export std::string Narrow(std::wstring_view text)
{
    if (text.empty())
        return {};
    const auto length = WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

export template <class T = std::filesystem::path>
T GetModulePath(HMODULE module)
{
    const auto name = ModuleFileName(module);
    if constexpr (std::is_same_v<T, std::filesystem::path>)
        return std::filesystem::path(name);
    else if constexpr (std::is_same_v<T, std::string>)
        return Narrow(name);
    else
    {
        static_assert(std::is_same_v<T, std::wstring>, "the path helpers support path, string and wstring");
        return name;
    }
}

// A string directory keeps the separator; a path drops it. Both forms answer
// "the folder the module is in".
export template <class T = std::filesystem::path>
T GetThisModulePath()
{
    const auto name = ModuleFileName(GetThisModuleHandle());
    if constexpr (std::is_same_v<T, std::filesystem::path>)
        return std::filesystem::path(name).parent_path();
    else if constexpr (std::is_same_v<T, std::string>)
        return Narrow(std::wstring_view(name).substr(0, name.find_last_of(L"/\\") + 1));
    else
        return name.substr(0, name.find_last_of(L"/\\") + 1);
}

export template <class T = std::filesystem::path>
T GetThisModuleName()
{
    const auto name = ModuleFileName(GetThisModuleHandle());
    if constexpr (std::is_same_v<T, std::filesystem::path>)
        return std::filesystem::path(name).filename();
    else if constexpr (std::is_same_v<T, std::string>)
        return Narrow(std::wstring_view(name).substr(name.find_last_of(L"/\\") + 1));
    else
        return name.substr(name.find_last_of(L"/\\") + 1);
}

export template <class T = std::filesystem::path>
T GetExeModulePath()
{
    const auto name = ModuleFileName(nullptr);
    if constexpr (std::is_same_v<T, std::filesystem::path>)
        return std::filesystem::path(name).parent_path();
    else if constexpr (std::is_same_v<T, std::string>)
        return Narrow(std::wstring_view(name).substr(0, name.find_last_of(L"/\\") + 1));
    else
        return name.substr(0, name.find_last_of(L"/\\") + 1);
}

export template <class T = std::filesystem::path>
T GetExeModuleName()
{
    const auto name = ModuleFileName(nullptr);
    if constexpr (std::is_same_v<T, std::filesystem::path>)
        return std::filesystem::path(name).filename();
    else if constexpr (std::is_same_v<T, std::string>)
        return Narrow(std::wstring_view(name).substr(name.find_last_of(L"/\\") + 1));
    else
        return name.substr(name.find_last_of(L"/\\") + 1);
}

export bool iequals(std::string_view first, std::string_view second)
{
    if (first.size() != second.size())
        return false;
    return std::equal(first.begin(), first.end(), second.begin(), second.end(),
        [](char a, char b) { return ::tolower(a) == ::tolower(b); });
}

export bool iequals(std::wstring_view first, std::wstring_view second)
{
    if (first.size() != second.size())
        return false;
    return std::equal(first.begin(), first.end(), second.begin(), second.end(),
        [](wchar_t a, wchar_t b) { return ::towlower(a) == ::towlower(b); });
}

// std::filesystem::relative with a case-insensitive comparison, which is what a
// path recovered from the game needs when it is compared with a disk path.
export std::filesystem::path lexicallyRelativeCaseIns(const std::filesystem::path& path, const std::filesystem::path& base)
{
    class input_iterator_range
    {
    public:
        input_iterator_range(const std::filesystem::path::const_iterator& first, const std::filesystem::path::const_iterator& last)
            : _first(first)
            , _last(last)
        {
        }
        std::filesystem::path::const_iterator begin() const { return _first; }
        std::filesystem::path::const_iterator end() const { return _last; }
    private:
        std::filesystem::path::const_iterator _first;
        std::filesystem::path::const_iterator _last;
    };

    if (!iequals(path.root_name().wstring(), base.root_name().wstring()) || path.is_absolute() != base.is_absolute() || (!path.has_root_directory() && base.has_root_directory()))
        return std::filesystem::path();

    std::filesystem::path::const_iterator a = path.begin(), b = base.begin();
    while (a != path.end() && b != base.end() && iequals(a->wstring(), b->wstring()))
    {
        ++a;
        ++b;
    }

    if (a == path.end() && b == base.end())
        return std::filesystem::path(".");

    int count = 0;
    for (const auto& element : input_iterator_range(b, base.end()))
    {
        if (element != "." && element != "" && element != "..")
            ++count;
        else if (element == "..")
            --count;
    }

    if (count < 0)
        return std::filesystem::path();

    std::filesystem::path result;
    for (int i = 0; i < count; ++i)
        result /= "..";

    for (const auto& element : input_iterator_range(a, path.end()))
        result /= element;

    return result;
}

export inline bool IsModuleUAL(HMODULE module)
{
    return module != nullptr && GetProcAddress(module, "IsUltimateASILoader") != nullptr;
}

// Ultimate ASI Loader calls InitializeASI itself; every other loader does not, so
// the module has to start on its own. Loaded by the UAL, that loader is one of
// this process's modules and exports the marker.
export bool IsUALPresent()
{
    HMODULE modules[512]{};
    DWORD needed{};
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return false;

    const auto count = std::min<size_t>(needed / sizeof(HMODULE), std::size(modules));
    for (size_t index = 0; index < count; ++index)
    {
        if (IsModuleUAL(modules[index]))
            return true;
    }
    return false;
}

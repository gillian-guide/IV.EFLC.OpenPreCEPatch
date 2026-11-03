// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.warnings - the startup compatibility dialogs.
//
// The game executable carries no common-controls dependency, so comctl32 v6 (and
// with it TaskDialogIndirect) is only mapped when this plugin's own activation
// context is active. The manifest is embedded as RT_MANIFEST #1 by warnings.rc,
// and CreateActCtx/ActivateActCtx below use it; without any of that a plain
// message box is the fallback.
//
// Every dialog is shown from a worker thread: TaskDialogIndirect maps comctl32,
// and loading a DLL is not allowed while the entry point runs inside the
// loader lock (DllMain, or an ASI loader that calls InitializeASI from its own
// DllMain). A worker blocks on the loader lock instead and shows the dialog once
// the entry point has returned.

module;

#include "common.hxx"

export module opp.warnings;

import opp.log;

export namespace opp
{
    struct WarningButton
    {
        std::wstring text;
        // Empty: the button closes the dialog. Otherwise the target is opened with
        // ShellExecute (an https URL or a file such as the FusionFix notice).
        std::wstring link;
    };

    struct WarningDialog
    {
        std::wstring title;
        std::wstring header;   // the bold main instruction
        std::wstring content;
        std::wstring footer;
        bool error{};          // error icon instead of the information icon
        std::vector<WarningButton> buttons;
    };

    // Shows `dialog` on a worker thread. When `exitCode` is set the process is
    // terminated with it once the dialog has been closed, so a fatal warning
    // never leaves a half-patched game running.
    void ShowWarningAsync(WarningDialog dialog, std::optional<UINT> exitCode) noexcept;

    // The warning texts, built here so every call site shows the same wording.
    WarningDialog OldAsiLoaderDialog(const std::string& loaderName,
                                     const std::string& loaderVersion,
                                     bool versionKnown);
    WarningDialog UnsupportedGameVersionDialog();
    WarningDialog LoadedTwiceDialog();
    WarningDialog FusionFixNoticeDialog(const std::wstring& iniName,
                                        const std::wstring& noticeName,
                                        const std::wstring& noticePath,
                                        const std::vector<std::wstring>& gatedLines);

    // Rewrites the FusionFix notice file at `notice` with the current gated
    // options. Returns false when it cannot be written.
    bool WriteFusionFixNotice(const std::filesystem::path& notice,
                              const std::wstring& iniName,
                              const std::vector<std::wstring>& gatedLines);
} // namespace opp

namespace opp
{
    namespace
    {
        constexpr int kButtonBase = 1000;

        using TaskDialogIndirectFn = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);

        std::wstring Widen(const std::string& text)
        {
            return std::wstring(text.begin(), text.end());
        }

        std::string Narrow(const std::wstring& text)
        {
            std::string out;
            out.reserve(text.size());
            for (const wchar_t c : text)
                out.push_back(c < 128 ? static_cast<char>(c) : '?');
            return out;
        }

        // The module this code lives in (the ASI, not the game).
        HMODULE SelfModule() noexcept
        {
            HMODULE self = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(&SelfModule), &self))
                return nullptr;
            return self;
        }

        // Activates the plugin manifest for the calling thread. Scoped: the
        // context only has to be live while the dialog is created.
        class Activation final
        {
        public:
            Activation() noexcept
            {
                const HMODULE self = SelfModule();
                if (self == nullptr)
                    return;

                ACTCTXW context{};
                context.cbSize = sizeof context;
                context.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
                context.lpResourceName = MAKEINTRESOURCEW(1);
                context.hModule = self;
                handle_ = CreateActCtxW(&context);
                if (handle_ == INVALID_HANDLE_VALUE)
                    return;
                active_ = ActivateActCtx(handle_, &cookie_) != FALSE;
            }

            ~Activation()
            {
                if (active_)
                    DeactivateActCtx(0, cookie_);
                if (handle_ != INVALID_HANDLE_VALUE)
                    ReleaseActCtx(handle_);
            }

            Activation(const Activation&) = delete;
            Activation& operator=(const Activation&) = delete;

            [[nodiscard]] bool ok() const noexcept
            {
                return active_;
            }

        private:
            HANDLE handle_{INVALID_HANDLE_VALUE};
            ULONG_PTR cookie_{};
            bool active_{};
        };

        void OpenLink(const std::wstring& link) noexcept
        {
            if (link.empty())
                return;
            ShellExecuteW(nullptr, L"open", link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        HRESULT CALLBACK DialogCallback(HWND window, UINT notification, WPARAM /*wparam*/,
                                        LPARAM lparam, LONG_PTR /*data*/) noexcept
        {
            if (notification == TDN_HYPERLINK_CLICKED)
                ShellExecuteW(window, L"open", reinterpret_cast<LPCWSTR>(lparam), nullptr,
                              nullptr, SW_SHOWNORMAL);
            return S_OK;
        }

        // The modern presentation. False when comctl32 v6 could not be activated
        // or the task dialog could not be created; the caller then falls back.
        bool ShowTaskDialog(const WarningDialog& dialog) noexcept
        {
            Activation activation;
            if (!activation.ok())
                return false;

            const HMODULE comctl = LoadLibraryW(L"comctl32.dll");
            if (comctl == nullptr)
                return false;
            const auto taskDialog = reinterpret_cast<TaskDialogIndirectFn>(
                GetProcAddress(comctl, "TaskDialogIndirect"));
            if (taskDialog == nullptr)
                return false;

            std::vector<TASKDIALOG_BUTTON> buttons;
            buttons.reserve(dialog.buttons.size());
            for (size_t i = 0; i < dialog.buttons.size(); ++i)
                buttons.push_back({kButtonBase + static_cast<int>(i),
                                   dialog.buttons[i].text.c_str()});

            TASKDIALOGCONFIG config{};
            config.cbSize = sizeof config;
            config.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT |
                             TDF_ALLOW_DIALOG_CANCELLATION;
            if (buttons.size() > 1)
                config.dwFlags |= TDF_USE_COMMAND_LINKS;
            config.pszWindowTitle = dialog.title.c_str();
            config.pszMainIcon = dialog.error ? reinterpret_cast<PCWSTR>(TD_ERROR_ICON)
                                              : reinterpret_cast<PCWSTR>(TD_INFORMATION_ICON);
            config.pszMainInstruction = dialog.header.c_str();
            config.pszContent = dialog.content.c_str();
            config.pszFooter = dialog.footer.empty() ? nullptr : dialog.footer.c_str();
            config.pButtons = buttons.data();
            config.cButtons = static_cast<UINT>(buttons.size());
            config.pfCallback = &DialogCallback;

            int clicked = 0;
            if (FAILED(taskDialog(&config, &clicked, nullptr, nullptr)))
                return false;

            const int index = clicked - kButtonBase;
            if (index >= 0 && static_cast<size_t>(index) < dialog.buttons.size())
                OpenLink(dialog.buttons[index].link);
            return true;
        }

        // The last resort, and the only one that can run while the loader lock is
        // held: a message box maps nothing comctl32.
        void ShowMessageBox(const WarningDialog& dialog) noexcept
        {
            std::wstring text = dialog.content;
            if (!dialog.footer.empty())
            {
                text += L"\n\n";
                text += dialog.footer;
            }
            for (const WarningButton& button : dialog.buttons)
            {
                if (button.link.empty())
                    continue;
                text += L"\n\n";
                text += button.text;
                text += L": ";
                text += button.link;
            }

            MessageBoxW(nullptr, text.c_str(), dialog.header.c_str(),
                        MB_OK | (dialog.error ? MB_ICONERROR : MB_ICONINFORMATION));
        }

        void ShowNow(const WarningDialog& dialog) noexcept
        {
            if (ShowTaskDialog(dialog))
                return;
            Log("warnings: task dialog unavailable; using a message box");
            ShowMessageBox(dialog);
        }

        struct WarningRequest
        {
            WarningDialog dialog;
            std::optional<UINT> exitCode;
        };

        DWORD WINAPI WarningThread(LPVOID parameter) noexcept
        {
            std::unique_ptr<WarningRequest> request(static_cast<WarningRequest*>(parameter));
            ShowNow(request->dialog);
            if (request->exitCode)
                TerminateProcess(GetCurrentProcess(), *request->exitCode);
            return 0;
        }
    } // namespace

    void ShowWarningAsync(WarningDialog dialog, std::optional<UINT> exitCode) noexcept
    {
        auto* request = new (std::nothrow) WarningRequest{std::move(dialog), exitCode};
        if (request == nullptr)
        {
            // Nothing to show with; still honour a fatal warning.
            if (exitCode)
                TerminateProcess(GetCurrentProcess(), *exitCode);
            return;
        }

        const HANDLE thread = CreateThread(nullptr, 0, &WarningThread, request, 0, nullptr);
        if (thread == nullptr)
        {
            // Creating a thread can only fail when resources are exhausted; a
            // message box is safe from this thread, comctl32 is not.
            ShowMessageBox(request->dialog);
            if (exitCode)
                TerminateProcess(GetCurrentProcess(), *exitCode);
            delete request;
            return;
        }
        CloseHandle(thread);
    }

    WarningDialog OldAsiLoaderDialog(const std::string& loaderName,
                                     const std::string& loaderVersion, bool versionKnown)
    {
        WarningDialog dialog;
        dialog.title = L"OpenPreCEPatch";
        dialog.header = L"Old ASI Loader detected!";
        dialog.content =
            L"It's recommended to use latest Ultimate ASI Loader as either dinput8.dll or "
            L"xlive.dll.";
        if (loaderName.empty())
        {
            dialog.footer = L"No Ultimate ASI Loader was found in this process.";
        }
        else if (versionKnown)
        {
            dialog.footer = L"Detected " + Widen(loaderName) + L" " + Widen(loaderVersion) +
                            L"; this plugin expects Ultimate ASI Loader 6.9.0 or newer.";
        }
        else
        {
            dialog.footer = L"Detected " + Widen(loaderName) +
                            L"; this plugin expects Ultimate ASI Loader 6.9.0 or newer.";
        }
        dialog.buttons = {
            {L"Download Ultimate ASI Loader",
             L"https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest"},
            {L"Continue", L""}};
        return dialog;
    }

    WarningDialog UnsupportedGameVersionDialog()
    {
        WarningDialog dialog;
        dialog.title = L"OpenPreCEPatch";
        dialog.header = L"Unsupported game version.";
        dialog.content =
            L"Downgrade the game to use this mod, although note that if you don't have any "
            L"other reasons to downgrade, you're likely using this mod by accident, in which "
            L"case simply remove this mod. The game will now exit";
        dialog.error = true;
        dialog.buttons = {{L"Close the program", L""}};
        return dialog;
    }

    WarningDialog LoadedTwiceDialog()
    {
        WarningDialog dialog;
        dialog.title = L"OpenPreCEPatch";
        dialog.header = L"OpenPreCEPatch was loaded twice!";
        dialog.content =
            L"Make sure that you don't have multiple ASI loaders installed! The game will now "
            L"exit.";
        dialog.error = true;
        dialog.buttons = {{L"Close the program", L""}};
        return dialog;
    }

    WarningDialog FusionFixNoticeDialog(const std::wstring& iniName,
                                        const std::wstring& noticeName,
                                        const std::wstring& noticePath,
                                        const std::vector<std::wstring>& gatedLines)
    {
        WarningDialog dialog;
        dialog.title = L"OpenPreCEPatch";
        dialog.header = L"FusionFix is installed.";
        dialog.content = L"These " + iniName +
                         L" options are enabled but were switched off because FusionFix owns "
                         L"the same feature:\n\n";
        for (const std::wstring& line : gatedLines)
        {
            dialog.content += L"  ";
            dialog.content += line;
            dialog.content += L"\n";
        }
        dialog.content += L"\nThe full list is written to ";
        dialog.content += noticeName;
        dialog.content += L".";
        dialog.footer = L"This notice is shown once. Delete " + noticeName +
                        L" to show it again.";
        dialog.buttons = {{L"Open the notice", noticePath}, {L"Continue", L""}};
        return dialog;
    }

    bool WriteFusionFixNotice(const std::filesystem::path& notice,
                              const std::wstring& iniName,
                              const std::vector<std::wstring>& gatedLines)
    {
        std::ofstream file(notice, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;

        file << "OpenPreCEPatch: FusionFix compatibility notice\r\n"
                "FusionFix is installed next to the game executable. These "
             << Narrow(iniName)
             << " options are enabled but were switched off because FusionFix owns the "
                "same feature:\r\n\r\n";
        for (const std::wstring& line : gatedLines)
            file << "  " << Narrow(line) << "\r\n";
        file << "\r\nThis file is rewritten on every launch. Delete it to show the notice "
                "dialog again.\r\n";
        return file.good();
    }
} // namespace opp

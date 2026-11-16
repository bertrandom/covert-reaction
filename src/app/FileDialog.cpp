// (IFileOpenDialog is Windows Vista and later: mingw-w64 hides it below 0x0600.)
#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "app/FileDialog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include "app/MacFolderPanel.h"
#define REACTION_DIALOG_MAC 1
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <SDL_syswm.h>
#define REACTION_DIALOG_WINDOWS 1
#elif !defined(__EMSCRIPTEN__)
#include <unistd.h>
#define REACTION_DIALOG_UNIX 1
#endif

namespace reaction::dialogs {

namespace fs = std::filesystem;

namespace {

std::string envString(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

#ifdef REACTION_DIALOG_UNIX
bool onPath(const char* program) {
    const std::string path = envString("PATH");
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find(':', start), path.size());
        const std::string dir = path.substr(start, end - start);
        if (!dir.empty() && access((dir + "/" + program).c_str(), X_OK) == 0) return true;
        start = end + 1;
    }
    return false;
}

std::string quote(std::string_view s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

// Runs `cmd` (a shell command line); its first line of output if it exits with 0.
std::optional<std::string> run(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) {
        std::fprintf(stderr, "file dialog: can't run %s\n", cmd.c_str());
        return std::nullopt;
    }
    std::string out;
    char buf[4096];
    while (std::size_t n = std::fread(buf, 1, sizeof buf, p)) out.append(buf, n);
    const int status = pclose(p);
    if (status != 0) return std::nullopt;   // (1: cancelled)
    if (const std::size_t nl = out.find('\n'); nl != std::string::npos) out.resize(nl);
    if (out.empty()) return std::nullopt;
    return out;
}

std::optional<fs::path> unixChoose(Kind k, const Request& r) {
    const bool folder = r.pick == Pick::Folder;
    std::string start = r.start.empty() ? std::string() : r.start.string();
    std::string cmd;
    if (k == Kind::Zenity) {
        cmd = "zenity --file-selection --title=" + quote(r.title);
        if (folder) cmd += " --directory";
        if (!start.empty()) cmd += " --filename=" + quote(start + "/");
        if (!folder && !r.extensions.empty()) {
            std::string patterns;
            for (const auto& e : r.extensions) {
                std::string upper = e;
                for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                patterns += " *" + e + " *" + upper;
            }
            cmd += " --file-filter=" + quote(r.filterName + " |" + patterns);
            cmd += " --file-filter=" + quote("All files | *");
        }
    } else {
        cmd = "kdialog --title " + quote(r.title);
        if (folder) {
            cmd += " --getexistingdirectory " + quote(start.empty() ? "." : start);
        } else {
            std::string patterns;
            for (const auto& e : r.extensions) patterns += (patterns.empty() ? "*" : " *") + e;
            cmd += " --getopenfilename " + quote(start.empty() ? "." : start);
            if (!patterns.empty()) cmd += " " + quote(patterns + "|" + r.filterName);
        }
    }
    cmd += " 2>/dev/null";
    auto out = run(cmd);
    if (!out) return std::nullopt;
    return fs::path(*out);
}
#endif

#ifdef REACTION_DIALOG_WINDOWS
std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::optional<fs::path> windowsChoose(const Request& r, SDL_Window* parent) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::optional<fs::path> out;
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                  reinterpret_cast<void**>(&dlg));
    if (SUCCEEDED(hr)) {
        const bool folder = r.pick == Pick::Folder;
        FILEOPENDIALOGOPTIONS opts = 0;
        dlg->GetOptions(&opts);
        opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
        opts |= folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
        dlg->SetOptions(opts);
        const std::wstring title = wide(r.title);
        dlg->SetTitle(title.c_str());
        std::wstring filterName = wide(r.filterName), spec;
        for (const auto& e : r.extensions) spec += (spec.empty() ? L"*" : L";*") + wide(e);
        if (!folder && !spec.empty()) {
            const COMDLG_FILTERSPEC types[] = {{filterName.c_str(), spec.c_str()}, {L"All files", L"*.*"}};
            dlg->SetFileTypes(2, types);
            dlg->SetFileTypeIndex(1);
        }
        if (!r.start.empty()) {
            IShellItem* where = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(r.start.wstring().c_str(), nullptr, IID_IShellItem,
                                                      reinterpret_cast<void**>(&where)))) {
                dlg->SetFolder(where);
                where->Release();
            }
        }
        HWND owner = nullptr;
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (parent && SDL_GetWindowWMInfo(parent, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
            owner = info.info.win.window;
        hr = dlg->Show(owner);
        if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                    out = fs::path(p);
                    CoTaskMemFree(p);
                }
                item->Release();
            }
        } else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            std::fprintf(stderr, "file dialog: IFileOpenDialog::Show failed (0x%08lx)\n", static_cast<unsigned long>(hr));
        }
        dlg->Release();
    } else {
        std::fprintf(stderr, "file dialog: no IFileOpenDialog (0x%08lx)\n", static_cast<unsigned long>(hr));
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return out;
}
#endif

Kind detect() {
    const std::string force = envString("COVERT_FILE_DIALOG");
    if (force == "builtin") return Kind::None;
#if defined(REACTION_DIALOG_MAC)
    return Kind::MacPanel;
#elif defined(REACTION_DIALOG_WINDOWS)
    return Kind::Windows;
#elif defined(REACTION_DIALOG_UNIX)
    const bool kde = envString("XDG_CURRENT_DESKTOP").find("KDE") != std::string::npos;
    const bool zenity = onPath("zenity"), kdialog = onPath("kdialog");
    if (kdialog && (kde || !zenity)) return Kind::KDialog;
    if (zenity) return Kind::Zenity;
    return Kind::None;
#else
    return Kind::None;
#endif
}

}  // namespace

Kind kind() {
    static const Kind k = detect();
    return k;
}

const char* describe(Kind k) {
    switch (k) {
    case Kind::MacPanel: return "NSOpenPanel";
    case Kind::Windows: return "IFileOpenDialog";
    case Kind::Zenity: return "zenity";
    case Kind::KDialog: return "kdialog";
    default: return "none (the in-game browser)";
    }
}

bool takesFileOrFolder(Kind k) { return k == Kind::MacPanel && envString("COVERT_FILE_DIALOG") != "split"; }

bool takeTestPick(std::optional<fs::path>& out) {
    static const std::string list = envString("COVERT_TEST_PICK");
    static std::size_t pos = 0;
    if (list.empty() || pos > list.size()) return false;
    const std::size_t end = std::min(list.find('|', pos), list.size());
    const std::string item = list.substr(pos, end - pos);
    pos = end + 1;
    out = item.empty() ? std::nullopt : std::optional<fs::path>(fs::path(item));
    std::fprintf(stderr, "file dialog (COVERT_TEST_PICK): %s\n", item.empty() ? "cancelled" : item.c_str());
    return true;
}

std::optional<fs::path> choose(const Request& r, SDL_Window* parent) {
    const Kind k = kind();
    (void)parent;
#if defined(REACTION_DIALOG_MAC)
    if (k == Kind::MacPanel) {
        const std::string message = r.message.empty() ? r.title : r.message;
        if (r.pick == Pick::Folder) return macChooseFolder(r.start, message);
        return macChooseFileOrFolder(r.start, message, r.extensions);
    }
#elif defined(REACTION_DIALOG_WINDOWS)
    if (k == Kind::Windows) return windowsChoose(r, parent);
#elif defined(REACTION_DIALOG_UNIX)
    if (k == Kind::Zenity || k == Kind::KDialog) return unixChoose(k, r);
#endif
    (void)k;
    (void)r;
    return std::nullopt;
}

}  // namespace reaction::dialogs

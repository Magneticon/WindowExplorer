// WindowExplorer: native MDI host for Windows shell folder views.
// The application hosts IShellView in-process; it does not reparent explorer.exe.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <servprov.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <new>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

enum {
    IDC_MDI = 100, IDC_ADDRESS = 101, IDC_GO = 102,
    IDC_BACK = 103, IDC_UP = 104, IDC_STATUS = 105,
    IDC_FORWARD = 106, IDC_CHILD_MENU_FILE = 107,
    IDC_CHILD_MENU_NAVIGATE = 108, IDC_CHILD_MENU_VIEW = 109,
    IDC_CHILD_MENU_WINDOW = 110, IDC_CHILD_MENU_EDIT = 111,
    IDC_CHILD_MENU_FAVORITES = 112, IDC_CHILD_MENU_TOOLS = 113,
    IDC_FOLDERS = 115, IDC_TREE = 116,
    IDC_TOOL_NEW = 117, IDC_TOOL_REFRESH = 118,
    IDC_CHILD_TOOLBAR = 122,
    IDC_CHILD_MENUBAR = 121,
    IDM_NEW = 1001, IDM_CLOSE = 1002, IDM_EXIT = 1003,
    IDM_BACK = 1004, IDM_UP = 1005, IDM_GO = 1006,
    IDM_REFRESH = 1007, IDM_CASCADE = 1008,
    IDM_TILE_H = 1009, IDM_TILE_V = 1010, IDM_ADDRESS = 1011,
    IDM_FORWARD = 1012, IDM_FOLDERS = 1013,
    IDM_VIEW_ICONS = 1014, IDM_VIEW_LIST = 1015,
    IDM_VIEW_DETAILS = 1016,
    IDM_SELECT_ALL = 1018, IDM_FAVORITE_ADD = 1019,
    IDM_SHOW_STATUS = 1020, IDM_SHOW_TOOLBAR = 1021,
    IDM_SHOW_ADDRESS = 1022, IDM_GLOBAL_SETTINGS = 1023,
    IDM_VIEW_THUMBNAILS = 1024, IDM_VIEW_TILES = 1025,
    IDM_EDIT_UNDO = 1030, IDM_EDIT_CUT = 1031,
    IDM_EDIT_COPY = 1032, IDM_EDIT_PASTE = 1033,
    IDM_EDIT_PASTE_LINK = 1034, IDM_EDIT_COPY_TO = 1035,
    IDM_EDIT_MOVE_TO = 1036, IDM_EDIT_INVERT = 1037,
    IDM_EDIT_DELETE = 1038, IDM_EDIT_RENAME = 1039,
    IDM_EDIT_PROPERTIES = 1040, IDM_ABOUT_WINDOWS = 1041,
    WM_OPEN_SHELL_FOLDER = WM_APP + 2,
    IDM_MENU_FILE = 1050, IDM_MENU_EDIT = 1051,
    IDM_MENU_VIEW = 1052, IDM_MENU_FAVORITES = 1053,
    IDM_MENU_TOOLS = 1054,
    IDM_MENU_NAVIGATE = 1056, IDM_MENU_WINDOW = 1057,
    IDM_FAVORITE_FIRST = 4000, IDM_FAVORITE_LAST = 4049,
    IDM_FIRST_CHILD = 30000,
    WM_UPDATE_CHROME = WM_APP + 1
};

static const wchar_t kFrameClass[] = L"WindowExplorer.Frame";
static const wchar_t kChildClass[] = L"WindowExplorer.Folder";
static const wchar_t kMenuStripClass[] = L"WindowExplorer.ChildMenuStrip";
static const wchar_t* const kChildMenus[7] = {
    L"File", L"Edit", L"View", L"Favorites", L"Tools",
    L"Navigate", L"Window"
};
static const int kChildMenuWidths[7] = {
    43, 43, 49, 75, 50, 77, 69
};
static const GUID kTopLevelBrowserService = {
    0x4C96BE40,0x915C,0x11CF,{0x99,0xD3,0x00,0xAA,0x00,0x4A,0xE8,0x37}
};
static const GUID kInPlaceBrowserService = {
    0x1D2AE02B,0x3655,0x46CC,{0xB6,0x3A,0x28,0x59,0x88,0x15,0x3B,0xCA}
};
static HINSTANCE g_instance = NULL;
static HWND g_frame = NULL, g_mdi = NULL;
static HACCEL g_accel = NULL;
static bool g_shuttingDown = false;
static HWND g_menuTracking = NULL;
static std::vector<std::wstring> g_favorites;

// Global MDI defaults are stored per Windows user. The same defaults are
// applied to every existing child and to all children created later.
struct ExplorerDefaults {
    FOLDERVIEWMODE mode;
    bool folders;
    bool toolbar;
    bool address;
    bool status;
    ExplorerDefaults() : mode(FVM_DETAILS), folders(true),
        toolbar(true), address(true), status(true) {}
};
static ExplorerDefaults g_defaults;
static const wchar_t kSettingsKey[] = L"Software\\Magneticon\\WindowExplorer";

static DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback) {
    DWORD value = fallback, type = 0, bytes = sizeof(value);
    if (RegQueryValueExW(key, name, NULL, &type,
        reinterpret_cast<LPBYTE>(&value), &bytes) != ERROR_SUCCESS ||
        type != REG_DWORD || bytes != sizeof(value)) return fallback;
    return value;
}
static void WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
}
static void LoadDefaults() {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0,
            KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    DWORD mode = ReadDword(key, L"ViewMode", FVM_DETAILS);
    if (mode == FVM_ICON || mode == FVM_SMALLICON ||
        mode == FVM_LIST || mode == FVM_DETAILS ||
        mode == FVM_THUMBNAIL || mode == FVM_TILE)
        g_defaults.mode = static_cast<FOLDERVIEWMODE>(mode);
    g_defaults.folders = ReadDword(key, L"Folders", 1) != 0;
    g_defaults.toolbar = ReadDword(key, L"Toolbar", 1) != 0;
    g_defaults.address = ReadDword(key, L"Address", 1) != 0;
    g_defaults.status = ReadDword(key, L"Status", 1) != 0;
    DWORD count = ReadDword(key, L"FavoriteCount", 0);
    if (count > 50) count = 50;
    for (DWORD i = 0; i < count; ++i) {
        wchar_t name[30], path[32768];
        path[0] = 0;
        wsprintfW(name, L"Favorite%lu", static_cast<unsigned long>(i));
        DWORD type = 0, size = sizeof(path);
        if (RegQueryValueExW(key, name, NULL, &type,
                reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS &&
            type == REG_SZ && size >= sizeof(wchar_t) &&
            size <= sizeof(path)) {
            path[(size / sizeof(wchar_t)) - 1] = 0;
            if (path[0] &&
                std::find(g_favorites.begin(), g_favorites.end(), path)
                    == g_favorites.end())
                g_favorites.push_back(path);
        }
    }
    RegCloseKey(key);
}
static void SaveDefaults() {
    HKEY key = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, NULL,
            0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    WriteDword(key, L"ViewMode", static_cast<DWORD>(g_defaults.mode));
    WriteDword(key, L"Folders", g_defaults.folders ? 1 : 0);
    WriteDword(key, L"Toolbar", g_defaults.toolbar ? 1 : 0);
    WriteDword(key, L"Address", g_defaults.address ? 1 : 0);
    WriteDword(key, L"Status", g_defaults.status ? 1 : 0);
    const size_t count = g_favorites.size() < 50 ? g_favorites.size() : 50;
    WriteDword(key, L"FavoriteCount", static_cast<DWORD>(count));
    for (size_t i = 0; i < count; ++i) {
        wchar_t name[30];
        wsprintfW(name, L"Favorite%lu", static_cast<unsigned long>(i));
        const std::wstring& path = g_favorites[i];
        RegSetValueExW(key, name, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(path.c_str()),
            static_cast<DWORD>((path.length() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
}

class FolderBrowser;
static FolderBrowser* ActiveBrowser();
static HRESULT NewFolderWindow(LPCITEMIDLIST location);
static void UpdateChrome();
static void LayoutFrame(HWND frame);
static void ShowFailure(HWND owner, const wchar_t* operation, HRESULT hr);
static void ApplyDefaultsToAllChildren();
static FolderBrowser* BrowserFor(HWND child);
static LRESULT CALLBACK ChildMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

class FolderBrowser : public IShellBrowser, public IServiceProvider,
                      public ICommDlgBrowser {
public:
    explicit FolderBrowser(HWND child)
        : refs_(1), child_(child), viewWindow_(NULL), view_(NULL),
          folder_(NULL), pidl_(NULL), historyIndex_(-1),
          navigating_(false), closed_(false), editingAddress_(false),
          address_(NULL), go_(NULL), status_(NULL), tree_(NULL),
          toolbar_(NULL), menuBar_(NULL), treeRoot_(NULL),
          syncingTree_(false), showFolders_(g_defaults.folders),
          viewMode_(g_defaults.mode) {}

    virtual ~FolderBrowser() {
        Close();
    }

    HWND Child() const { return child_; }
    HWND ViewWindow() const { return viewWindow_; }
    IShellView* View() const { return view_; }
    bool CanBack() const { return historyIndex_ > 0; }
    bool CanForward() const {
        return historyIndex_ >= 0 &&
            historyIndex_ + 1 < static_cast<int>(history_.size());
    }
    bool CanUp() const { return pidl_ && pidl_->mkid.cb != 0; }

    // Defer Shell-originated navigation until the Shell view's own
    // double-click/Enter callback has returned. Destroying a view from
    // inside its callback can reenter Shell32 and leave dangling windows.
    HRESULT QueueOpenFolder(LPCITEMIDLIST destination, bool newWindow) {
        if (!destination || closed_ || g_shuttingDown) return E_INVALIDARG;
        LPITEMIDLIST copy = ILClone(destination);
        if (!copy) return E_OUTOFMEMORY;
        pending_.push_back(copy);
        if (!PostMessageW(child_, WM_OPEN_SHELL_FOLDER,
                newWindow ? 1 : 0, reinterpret_cast<LPARAM>(copy))) {
            pending_.pop_back();
            CoTaskMemFree(copy);
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }

    void HandleOpenFolder(LPITEMIDLIST location, bool newWindow) {
        std::vector<LPITEMIDLIST>::iterator pos =
            std::find(pending_.begin(), pending_.end(), location);
        if (pos == pending_.end()) return;
        pending_.erase(pos);
        if (!closed_ && !g_shuttingDown) {
            HRESULT hr = newWindow ? NewFolderWindow(location) :
                Navigate(location);
            if (FAILED(hr)) ShowFailure(child_, L"Open Shell folder", hr);
        }
        CoTaskMemFree(location);
    }
    LPCITEMIDLIST Location() const { return pidl_; }
    HWND AddressEdit() const { return address_; }

    bool CreateControls() {
        menuBar_ = CreateWindowW(kMenuStripClass, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, child_,
            reinterpret_cast<HMENU>(IDC_CHILD_MENUBAR), g_instance, NULL);
        // Native common-controls toolbar: XP supplies the standard bitmap
        // strips, so there are no external icons or text-only push buttons.
        toolbar_ = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
            CCS_NOPARENTALIGN | CCS_NORESIZE | CCS_NODIVIDER,
            0, 0, 0, 0, child_, reinterpret_cast<HMENU>(IDC_CHILD_TOOLBAR),
            g_instance, NULL);
        if (toolbar_) {
            SendMessageW(toolbar_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
            const int history = static_cast<int>(SendMessageW(toolbar_,
                TB_LOADIMAGES, IDB_HIST_SMALL_COLOR,
                reinterpret_cast<LPARAM>(HINST_COMMCTRL)));
            const int standard = static_cast<int>(SendMessageW(toolbar_,
                TB_LOADIMAGES, IDB_STD_SMALL_COLOR,
                reinterpret_cast<LPARAM>(HINST_COMMCTRL)));
            const int folderViews = static_cast<int>(SendMessageW(toolbar_,
                TB_LOADIMAGES, IDB_VIEW_SMALL_COLOR,
                reinterpret_cast<LPARAM>(HINST_COMMCTRL)));
            const int icons[] = {
                history + HIST_BACK, history + HIST_FORWARD,
                folderViews + VIEW_PARENTFOLDER, history + HIST_VIEWTREE,
                standard + STD_FILENEW, standard + STD_REDOW
            };
            const int commands[] = {
                IDC_BACK, IDC_FORWARD, IDC_UP, IDC_FOLDERS,
                IDC_TOOL_NEW, IDC_TOOL_REFRESH
            };
            TBBUTTON buttons[6];
            ZeroMemory(buttons, sizeof(buttons));
            for (int i = 0; i < 6; ++i) {
                buttons[i].iBitmap = icons[i];
                buttons[i].idCommand = commands[i];
                buttons[i].fsState = TBSTATE_ENABLED;
                buttons[i].fsStyle = TBSTYLE_BUTTON;
            }
            SendMessageW(toolbar_, TB_ADDBUTTONSW, 6,
                reinterpret_cast<LPARAM>(buttons));
            SendMessageW(toolbar_, TB_SETBUTTONSIZE, 0,
                MAKELONG(28, 26));
            SendMessageW(toolbar_, TB_AUTOSIZE, 0, 0);
        }
        tree_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
            TVS_SHOWSELALWAYS, 0, 0, 0, 0, child_,
            reinterpret_cast<HMENU>(IDC_TREE), g_instance, NULL);
        if (tree_) {
            SHFILEINFOW shellInfo;
            ZeroMemory(&shellInfo, sizeof(shellInfo));
            LPITEMIDLIST desktop = NULL;
            if (SUCCEEDED(SHGetSpecialFolderLocation(child_, CSIDL_DESKTOP,
                &desktop))) {
                HIMAGELIST icons = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
                    reinterpret_cast<LPCWSTR>(desktop), 0, &shellInfo,
                    sizeof(shellInfo), SHGFI_PIDL | SHGFI_SYSICONINDEX |
                    SHGFI_SMALLICON));
                if (icons) TreeView_SetImageList(tree_, icons, TVSIL_NORMAL);
                treeRoot_ = AddTreeItem(TVI_ROOT, desktop, L"Desktop");
                if (treeRoot_) TreeView_Expand(tree_, treeRoot_, TVE_EXPAND);
                CoTaskMemFree(desktop);
            }
        }
        address_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, child_, reinterpret_cast<HMENU>(IDC_ADDRESS),
            g_instance, NULL);
        go_ = CreateWindowW(L"BUTTON", L"Go", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, child_, reinterpret_cast<HMENU>(IDC_GO),
            g_instance, NULL);
        status_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE |
            SS_LEFTNOWORDWRAP, 0, 0, 0, 0, child_,
            reinterpret_cast<HMENU>(IDC_STATUS), g_instance, NULL);
        if (!menuBar_ || !toolbar_ || !address_ ||
            !go_ || !status_ || !tree_)
            return false;
        Layout();
        UpdateControls();
        return true;
    }

    int ContentTop() const {
        return 30 + (g_defaults.toolbar ? 27 : 0) +
            (g_defaults.address ? 26 : 0);
    }
    void ViewRect(RECT* rect) const {
        GetClientRect(child_, rect);
        rect->left = showFolders_ ? 222 : 0;
        rect->top = ContentTop();
        if (g_defaults.status) rect->bottom -= 21;
        if (rect->left > rect->right) rect->left = rect->right;
        if (rect->bottom < rect->top) rect->bottom = rect->top;
    }

    void Layout() {
        RECT rc;
        GetClientRect(child_, &rc);
        const int width = rc.right - rc.left;
        const int height = rc.bottom - rc.top;
        if (menuBar_) MoveWindow(menuBar_, 0, 0,
            width > 0 ? width : 0, 28, TRUE);
        const bool showToolbar = g_defaults.toolbar;
        const bool showAddress = g_defaults.address;
        if (toolbar_) {
            ShowWindow(toolbar_, showToolbar ? SW_SHOW : SW_HIDE);
            if (showToolbar) MoveWindow(toolbar_, 0, 30,
                width > 0 ? width : 0, 27, TRUE);
        }
        if (address_) {
            ShowWindow(address_, showAddress ? SW_SHOW : SW_HIDE);
            if (showAddress) MoveWindow(address_, 3,
                30 + (showToolbar ? 27 : 0),
                width > 57 ? width - 57 : 0, 23, TRUE);
        }
        if (go_) {
            ShowWindow(go_, showAddress ? SW_SHOW : SW_HIDE);
            if (showAddress) MoveWindow(go_, width - 49,
                30 + (showToolbar ? 27 : 0), 46, 23, TRUE);
        }
        if (tree_) {
            ShowWindow(tree_, showFolders_ ? SW_SHOW : SW_HIDE);
            if (showFolders_) {
                const int top = ContentTop();
                const int bottom = g_defaults.status ? 21 : 0;
                MoveWindow(tree_, 0, top, 220,
                    height > top + bottom ? height - top - bottom : 0, TRUE);
            }
        }
        if (status_) {
            ShowWindow(status_, g_defaults.status ? SW_SHOW : SW_HIDE);
            if (g_defaults.status) MoveWindow(status_, 4, height - 20,
                width > 8 ? width - 8 : 0, 18, TRUE);
        }
        Resize();
    }

    void UpdateControls() {
        if (toolbar_) {
            SendMessageW(toolbar_, TB_ENABLEBUTTON, IDC_BACK,
                MAKELONG(CanBack(), 0));
            SendMessageW(toolbar_, TB_ENABLEBUTTON, IDC_FORWARD,
                MAKELONG(CanForward(), 0));
            SendMessageW(toolbar_, TB_ENABLEBUTTON, IDC_UP,
                MAKELONG(CanUp(), 0));
        }
        if (address_ && !editingAddress_) {
            std::wstring text;
            Address(text);
            SetWindowTextW(address_, text.c_str());
        }
        if (status_) {
            std::wstring title;
            DisplayName(title);
            SetWindowTextW(status_, title.c_str());
        }
        SyncTree();
    }

    void AddressChanged() {
        if (address_ && GetFocus() == address_) editingAddress_ = true;
    }
    void FocusAddress() {
        if (!g_defaults.address) {
            g_defaults.address = true;
            SaveDefaults();
            ApplyDefaultsToAllChildren();
        }
        if (address_) {
            SetFocus(address_);
            SendMessageW(address_, EM_SETSEL, 0, -1);
            editingAddress_ = true;
        }
    }
    void CancelAddress() {
        editingAddress_ = false;
        UpdateControls();
        if (view_) view_->UIActivate(SVUIA_ACTIVATE_FOCUS);
    }
    void AddressGo() {
        if (!address_) return;
        const int length = GetWindowTextLengthW(address_);
        std::vector<wchar_t> path(static_cast<size_t>(length) + 1);
        GetWindowTextW(address_, &path[0], length + 1);
        HRESULT hr = Go(&path[0]);
        if (FAILED(hr)) ShowFailure(child_, L"Open folder", hr);
        else editingAddress_ = false;
        UpdateControls();
    }

    void OpenMenu(int index);
    void ExpandTree(HTREEITEM node);
    void SyncTree();
    void SelectAll();
    HRESULT EditCommand(UINT command);
    HRESULT InvokeShellVerb(const char* verb, bool background);
    HRESULT TransferSelectedFiles(bool move);
    HRESULT ChangeViewMode(FOLDERVIEWMODE mode) {
        if (!pidl_ || viewMode_ == mode) return S_FALSE;
        const FOLDERVIEWMODE previous = viewMode_;
        viewMode_ = mode;
        // Recreate the native Shell view with the requested FOLDERSETTINGS.
        // The existing history and location are not changed.
        HRESULT hr = Navigate(pidl_, false);
        if (FAILED(hr)) viewMode_ = previous;
        return hr;
    }
    void ToggleFolders() {
        g_defaults.folders = !g_defaults.folders;
        SaveDefaults();
        ApplyDefaultsToAllChildren();
    }
    void ApplyDefaults() {
        if (closed_) return;
        showFolders_ = g_defaults.folders;
        Layout();
        SyncTree();
        if (pidl_ && viewMode_ != g_defaults.mode) {
            HRESULT hr = ChangeViewMode(g_defaults.mode);
            if (FAILED(hr)) ShowFailure(child_, L"Apply default view", hr);
        } else if (!pidl_) {
            viewMode_ = g_defaults.mode;
        }
    }
    bool FoldersShown() const { return showFolders_; }
    FOLDERVIEWMODE ViewMode() const { return viewMode_; }
    void TreeSelectionChanged(HTREEITEM node) {
        if (syncingTree_ || navigating_ || !node) return;
        TVITEMW item;
        ZeroMemory(&item, sizeof(item));
        item.mask = TVIF_PARAM;
        item.hItem = node;
        if (TreeView_GetItem(tree_, &item) && item.lParam) {
            LPCITEMIDLIST location = reinterpret_cast<LPCITEMIDLIST>(item.lParam);
            if (!pidl_ || !ILIsEqual(pidl_, location)) {
                HRESULT hr = Navigate(location);
                if (FAILED(hr)) ShowFailure(child_, L"Open folder from tree", hr);
            }
        }
    }
    void DeleteTreeItem(LPARAM value) {
        if (value) CoTaskMemFree(reinterpret_cast<void*>(value));
    }

    void Close() {
        if (closed_) return;
        closed_ = true;
        if (view_) {
            IShellView* oldView = view_;
            view_ = NULL;
            viewWindow_ = NULL;
            oldView->UIActivate(SVUIA_DEACTIVATE);
            oldView->DestroyViewWindow();
            oldView->Release();
        }
        if (folder_) { folder_->Release(); folder_ = NULL; }
        if (pidl_) { CoTaskMemFree(pidl_); pidl_ = NULL; }
        for (size_t i = 0; i < history_.size(); ++i)
            CoTaskMemFree(history_[i]);
        history_.clear();
        historyIndex_ = -1;
        for (size_t i = 0; i < pending_.size(); ++i)
            CoTaskMemFree(pending_[i]);
        pending_.clear();
        if (tree_) TreeView_DeleteAllItems(tree_);
        treeRoot_ = NULL;
    }

    void Resize() {
        if (viewWindow_ && IsWindow(viewWindow_)) {
            RECT rc;
            ViewRect(&rc);
            MoveWindow(viewWindow_, rc.left, rc.top,
                rc.right - rc.left, rc.bottom - rc.top, TRUE);
        }
    }

    void Activate(bool active) {
        if (view_)
            view_->UIActivate(active ? SVUIA_ACTIVATE_FOCUS : SVUIA_DEACTIVATE);
    }

    void DisplayName(std::wstring& name) const {
        name = L"My Computer";
        if (!pidl_) return;
        SHFILEINFOW info;
        ZeroMemory(&info, sizeof(info));
        if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl_), 0, &info,
                sizeof(info), SHGFI_PIDL | SHGFI_DISPLAYNAME) && info.szDisplayName[0])
            name = info.szDisplayName;
    }

    void Address(std::wstring& address) const {
        address.clear();
        if (!pidl_) return;
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl_, path)) {
            address = path;
        } else {
            // Virtual Shell locations (such as My Computer) have no
            // filesystem path. Show the Shell-provided display name.
            DisplayName(address);
        }
    }

    // The input PIDL must be absolute (relative to the desktop).
    HRESULT Navigate(LPCITEMIDLIST destination, bool recordHistory = true) {
        if (!destination) return E_INVALIDARG;
        if (closed_ || navigating_) return E_UNEXPECTED;
        navigating_ = true;

        LPITEMIDLIST absolute = ILClone(destination);
        if (!absolute) { navigating_ = false; return E_OUTOFMEMORY; }

        IShellFolder* desktop = NULL;
        IShellFolder* nextFolder = NULL;
        IShellView* nextView = NULL;
        HWND nextWindow = NULL;
        HRESULT hr = SHGetDesktopFolder(&desktop);
        if (SUCCEEDED(hr)) {
            if (absolute->mkid.cb == 0) {
                nextFolder = desktop;
                nextFolder->AddRef();
            } else {
                hr = desktop->BindToObject(absolute, NULL, IID_IShellFolder,
                    reinterpret_cast<void**>(&nextFolder));
            }
        }
        if (SUCCEEDED(hr))
            hr = nextFolder->CreateViewObject(child_, IID_IShellView,
                reinterpret_cast<void**>(&nextView));

        if (SUCCEEDED(hr)) {
            FOLDERSETTINGS settings;
            settings.ViewMode = viewMode_;
            settings.fFlags = FWF_AUTOARRANGE;
            RECT rc;
            ViewRect(&rc);
            hr = nextView->CreateViewWindow(view_, &settings, this, &rc, &nextWindow);
            if (SUCCEEDED(hr) && !nextWindow) hr = E_FAIL;
        }

        if (SUCCEEDED(hr)) {
            IShellView* oldView = view_;
            IShellFolder* oldFolder = folder_;
            LPITEMIDLIST oldPidl = pidl_;

            view_ = nextView;
            folder_ = nextFolder;
            pidl_ = absolute;
            viewWindow_ = nextWindow;
            nextView = NULL;
            nextFolder = NULL;
            absolute = NULL;

            // Create the replacement before tearing down the old view.
            if (oldView) {
                oldView->UIActivate(SVUIA_DEACTIVATE);
                oldView->DestroyViewWindow();
                oldView->Release();
            }
            if (oldFolder) oldFolder->Release();
            if (oldPidl) CoTaskMemFree(oldPidl);

            if (recordHistory) {
                while (static_cast<int>(history_.size()) > historyIndex_ + 1) {
                    CoTaskMemFree(history_.back());
                    history_.pop_back();
                }
                LPITEMIDLIST copy = ILClone(pidl_);
                if (copy) {
                    history_.push_back(copy);
                    historyIndex_ = static_cast<int>(history_.size()) - 1;
                }
            }

            std::wstring name;
            DisplayName(name);
            SetWindowTextW(child_, name.c_str());
            Resize();
            SetWindowPos(viewWindow_, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            const bool active = (ActiveBrowser() == this);
            if (active) editingAddress_ = false;
            UpdateControls();
            view_->UIActivate(active ?
                SVUIA_ACTIVATE_FOCUS : SVUIA_ACTIVATE_NOFOCUS);
            if (g_frame) PostMessageW(g_frame, WM_UPDATE_CHROME, 0, 0);
        }

        if (nextView) {
            if (nextWindow) nextView->DestroyViewWindow();
            nextView->Release();
        }
        if (nextFolder) nextFolder->Release();
        if (desktop) desktop->Release();
        if (absolute) CoTaskMemFree(absolute);
        navigating_ = false;
        return hr;
    }

    HRESULT Back() {
        if (!CanBack()) return S_FALSE;
        const int target = historyIndex_ - 1;
        HRESULT hr = Navigate(history_[target], false);
        if (SUCCEEDED(hr)) {
            historyIndex_ = target;
            UpdateControls();
        }
        return hr;
    }

    HRESULT Forward() {
        if (!CanForward()) return S_FALSE;
        const int target = historyIndex_ + 1;
        HRESULT hr = Navigate(history_[target], false);
        if (SUCCEEDED(hr)) {
            historyIndex_ = target;
            UpdateControls();
        }
        return hr;
    }

    HRESULT Up() {
        if (!CanUp()) return S_FALSE;
        LPITEMIDLIST parent = ILClone(pidl_);
        if (!parent) return E_OUTOFMEMORY;
        HRESULT hr = E_FAIL;
        if (ILRemoveLastID(parent)) hr = Navigate(parent);
        CoTaskMemFree(parent);
        return hr;
    }

    HRESULT Go(const wchar_t* path) {
        if (!path || !*path) return E_INVALIDARG;
        // The friendly name displayed for a virtual location is not
        // necessarily a path accepted by ParseDisplayName.
        if (lstrcmpiW(path, L"My Computer") == 0) {
            LPITEMIDLIST drives = NULL;
            HRESULT result = SHGetSpecialFolderLocation(child_,
                CSIDL_DRIVES, &drives);
            if (SUCCEEDED(result)) {
                result = Navigate(drives);
                CoTaskMemFree(drives);
            }
            return result;
        }
        IShellFolder* desktop = NULL;
        HRESULT hr = SHGetDesktopFolder(&desktop);
        if (FAILED(hr)) return hr;
        LPITEMIDLIST location = NULL;
        ULONG eaten = 0;
        // ParseDisplayName accepts file-system paths and Shell namespace paths.
        hr = desktop->ParseDisplayName(child_, NULL,
            const_cast<LPWSTR>(path), &eaten, &location, NULL);
        desktop->Release();
        if (SUCCEEDED(hr)) {
            hr = Navigate(location);
            CoTaskMemFree(location);
        }
        return hr;
    }

    // XP's Shell view may ask its host for the browser via the site
    // service chain rather than call IShellBrowser::BrowseObject directly.
    STDMETHODIMP QueryService(REFGUID service, REFIID riid, void** result) {
        if (!result) return E_POINTER;
        *result = NULL;
        if (IsEqualGUID(service, kTopLevelBrowserService) ||
            IsEqualGUID(service, kInPlaceBrowserService) ||
            IsEqualGUID(service, IID_IShellBrowser))
            return QueryInterface(riid, result);
        return E_NOINTERFACE;
    }

    // XP's native DefView can report its default action to the host.
    // Consume a folder activation and browse in-place; do not intercept
    // documents or executables, which must retain their normal open verb.
    STDMETHODIMP OnDefaultCommand(IShellView* invokingView) {
        if (closed_ || g_shuttingDown || !invokingView ||
            invokingView != view_ || !folder_ || !pidl_)
            return S_FALSE;
        IFolderView* fv = NULL;
        HRESULT hr = invokingView->QueryInterface(IID_IFolderView,
            reinterpret_cast<void**>(&fv));
        if (FAILED(hr)) return S_FALSE;
        int focused = -1;
        LPITEMIDLIST relative = NULL;
        hr = fv->GetFocusedItem(&focused);
        if (SUCCEEDED(hr) && focused >= 0)
            hr = fv->Item(focused, &relative);
        fv->Release();
        if (FAILED(hr) || !relative) return S_FALSE;
        SFGAOF flags = SFGAO_FOLDER | SFGAO_BROWSABLE;
        LPCITEMIDLIST childIds[] = { relative };
        hr = folder_->GetAttributesOf(1, childIds, &flags);
        HRESULT result = S_FALSE;
        if (SUCCEEDED(hr) && (flags & SFGAO_FOLDER)) {
            LPITEMIDLIST absolute = ILCombine(pidl_, relative);
            if (absolute) {
                result = SUCCEEDED(QueueOpenFolder(absolute, false)) ?
                    S_OK : S_FALSE;
                CoTaskMemFree(absolute);
            }
        }
        CoTaskMemFree(relative);
        return result;
    }
    STDMETHODIMP OnStateChange(IShellView*, ULONG) { return S_OK; }
    STDMETHODIMP IncludeObject(IShellView*, LPCITEMIDLIST) { return S_OK; }

    // IUnknown / IOleWindow / IShellBrowser
    STDMETHODIMP QueryInterface(REFIID riid, void** object) {
        if (!object) return E_POINTER;
        *object = NULL;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IOleWindow) ||
            IsEqualIID(riid, IID_IShellBrowser)) {
            *object = static_cast<IShellBrowser*>(this);
        } else if (IsEqualIID(riid, IID_IServiceProvider)) {
            *object = static_cast<IServiceProvider*>(this);
        } else if (IsEqualIID(riid, IID_ICommDlgBrowser)) {
            *object = static_cast<ICommDlgBrowser*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    STDMETHODIMP_(ULONG) Release() {
        ULONG count = static_cast<ULONG>(InterlockedDecrement(&refs_));
        if (!count) delete this;
        return count;
    }
    STDMETHODIMP GetWindow(HWND* window) {
        if (!window) return E_POINTER;
        *window = child_;
        return S_OK;
    }
    STDMETHODIMP ContextSensitiveHelp(BOOL) { return E_NOTIMPL; }

    // The host retains its own application menu and toolbar; the Shell
    // handles item context menus, file operations and view keyboard input.
    STDMETHODIMP InsertMenusSB(HMENU, LPOLEMENUGROUPWIDTHS) { return S_OK; }
    STDMETHODIMP SetMenuSB(HMENU, HOLEMENU, HWND) { return S_OK; }
    STDMETHODIMP RemoveMenusSB(HMENU) { return S_OK; }
    STDMETHODIMP SetStatusTextSB(LPCWSTR message) {
        if (status_)
            SetWindowTextW(status_, message ? message : L"");
        return S_OK;
    }
    STDMETHODIMP EnableModelessSB(BOOL) { return S_OK; }
    STDMETHODIMP TranslateAcceleratorSB(MSG*, WORD) { return S_FALSE; }

    STDMETHODIMP BrowseObject(PCUIDLIST_RELATIVE target, UINT flags) {
        if (closed_ || g_shuttingDown) return E_UNEXPECTED;
        if (flags & SBSP_NAVIGATEBACK) return Back();
        if (flags & SBSP_NAVIGATEFORWARD) return Forward();
        if (flags & SBSP_PARENT) return Up();
        if (!target) return E_INVALIDARG;
        LPITEMIDLIST absolute = NULL;
        if (flags & SBSP_RELATIVE) {
            if (!pidl_) return E_UNEXPECTED;
            absolute = ILCombine(pidl_, target);
        } else {
            absolute = ILClone(target);
        }
        if (!absolute) return E_OUTOFMEMORY;
        HRESULT hr = QueueOpenFolder(absolute,
            (flags & SBSP_NEWBROWSER) != 0);
        CoTaskMemFree(absolute);
        return hr;
    }
    STDMETHODIMP GetViewStateStream(DWORD, IStream** stream) {
        if (!stream) return E_POINTER;
        *stream = NULL;
        // No persisted view state in the first prototype.
        return E_NOTIMPL;
    }
    STDMETHODIMP GetControlWindow(UINT, HWND* window) {
        if (!window) return E_POINTER;
        *window = NULL;
        return E_NOTIMPL;
    }
    STDMETHODIMP SendControlMsg(UINT, UINT, WPARAM, LPARAM, LRESULT* result) {
        if (result) *result = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP QueryActiveShellView(IShellView** view) {
        if (!view) return E_POINTER;
        *view = view_;
        if (!view_) return E_FAIL;
        view_->AddRef();
        return S_OK;
    }
    STDMETHODIMP OnViewWindowActive(IShellView*) {
        // MDI activation is managed by DefMDIChildProc. Do not send
        // WM_MDIACTIVATE from this callback: doing so while UIActivate
        // is running can recursively activate the same Shell view.
        if (g_frame) PostMessageW(g_frame, WM_UPDATE_CHROME, 0, 0);
        return S_OK;
    }
    STDMETHODIMP SetToolbarItems(LPTBBUTTONSB, UINT, UINT) { return S_OK; }

private:
    LONG refs_;
    HWND child_;
    HWND viewWindow_;
    IShellView* view_;
    IShellFolder* folder_;
    LPITEMIDLIST pidl_;
    std::vector<LPITEMIDLIST> history_;
    std::vector<LPITEMIDLIST> pending_;
    int historyIndex_;
    bool navigating_;
    bool closed_;
    bool editingAddress_;
    HWND menuBar_;
    HWND address_, go_, status_, tree_;
    HWND toolbar_;
    HTREEITEM treeRoot_;
    bool syncingTree_, showFolders_;
    FOLDERVIEWMODE viewMode_;

    HTREEITEM AddTreeItem(HTREEITEM parent, LPCITEMIDLIST pidl,
                         const wchar_t* overrideName = NULL) {
        LPITEMIDLIST owned = ILClone(pidl);
        if (!owned) return NULL;
        SHFILEINFOW file;
        ZeroMemory(&file, sizeof(file));
        SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &file,
            sizeof(file), SHGFI_PIDL | SHGFI_DISPLAYNAME |
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
        TVINSERTSTRUCTW insert;
        ZeroMemory(&insert, sizeof(insert));
        insert.hParent = parent;
        insert.hInsertAfter = TVI_SORT;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE |
                           TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
        insert.item.pszText = const_cast<LPWSTR>(overrideName ?
            overrideName : file.szDisplayName);
        insert.item.lParam = reinterpret_cast<LPARAM>(owned);
        insert.item.iImage = insert.item.iSelectedImage = file.iIcon;
        insert.item.cChildren = 1; // Query children lazily on expansion.
        HTREEITEM item = TreeView_InsertItem(tree_, &insert);
        if (!item) CoTaskMemFree(owned);
        return item;
    }
};

static FolderBrowser* BrowserFor(HWND child) {
    return child ? reinterpret_cast<FolderBrowser*>(
        GetWindowLongPtrW(child, GWLP_USERDATA)) : NULL;
}

// Child windows cannot own an ordinary Win32 HMENU menu bar. This painted
// menu strip presents the same menu labels/keyboard navigation, and opens
// real HMENU popup menus owned by the corresponding MDI child.
static int MenuIndexAt(int x) {
    int left = 2;
    for (int i = 0; i < 7; ++i) {
        if (x >= left && x < left + kChildMenuWidths[i]) return i;
        left += kChildMenuWidths[i];
    }
    return -1;
}
static LRESULT CALLBACK ChildMenuProc(HWND hwnd, UINT msg,
                                      WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        RECT bounds;
        GetClientRect(hwnd, &bounds);
        FillRect(dc, &bounds, GetSysColorBrush(COLOR_MENU));
        SetBkMode(dc, TRANSPARENT);
        const int selected = static_cast<int>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA)) - 1;
        int left = 2;
        for (int i = 0; i < 8; ++i) {
            RECT item = { left, 1, left + kChildMenuWidths[i], 27 };
            if (i == selected || (g_menuTracking == hwnd && i == selected)) {
                FillRect(dc, &item, GetSysColorBrush(COLOR_HIGHLIGHT));
                SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            } else {
                SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));
            }
            DrawTextW(dc, kChildMenus[i], -1, &item,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            left += kChildMenuWidths[i];
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int hot = MenuIndexAt(GET_X_LPARAM(lp));
        if (static_cast<int>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) != hot + 1) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, hot + 1);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT track;
        ZeroMemory(&track, sizeof(track));
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        const int index = MenuIndexAt(GET_X_LPARAM(lp));
        FolderBrowser* browser = BrowserFor(GetParent(hwnd));
        if (browser && index >= 0) {
            SetFocus(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, index + 1);
            InvalidateRect(hwnd, NULL, FALSE);
            browser->OpenMenu(index);
        }
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN: {
        const int old = static_cast<int>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA)) - 1;
        int selected = old;
        if (wp == VK_RIGHT) selected = (old + 1 + 7) % 7;
        else if (wp == VK_LEFT) selected = (old + 6 + 7) % 7;
        else if (wp == VK_RETURN || wp == VK_DOWN || wp == VK_SPACE) {
            FolderBrowser* browser = BrowserFor(GetParent(hwnd));
            if (browser) browser->OpenMenu(old >= 0 ? old : 0);
            return 0;
        } else if (wp == VK_ESCAPE) {
            HWND parent = GetParent(hwnd);
            FolderBrowser* browser = BrowserFor(parent);
            if (browser && browser->View())
                browser->View()->UIActivate(SVUIA_ACTIVATE_FOCUS);
            return 0;
        } else return DefWindowProcW(hwnd, msg, wp, lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, selected + 1);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static FolderBrowser* ActiveBrowser() {
    if (!g_mdi || !IsWindow(g_mdi)) return NULL;
    HWND child = reinterpret_cast<HWND>(
        SendMessageW(g_mdi, WM_MDIGETACTIVE, 0, 0));
    return BrowserFor(child);
}

static void ShowFailure(HWND owner, const wchar_t* operation, HRESULT hr) {
    wchar_t text[320];
    wsprintfW(text, L"%s failed (HRESULT 0x%08lX).", operation,
        static_cast<unsigned long>(hr));
    MessageBoxW(owner, text, L"WindowExplorer", MB_OK | MB_ICONEXCLAMATION);
}

static void ApplyDefaultsToAllChildren() {
    if (!g_mdi || !IsWindow(g_mdi)) return;
    // Enumerate direct MDI children, never nested Shell controls.
    for (HWND child = GetWindow(g_mdi, GW_CHILD); child; ) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        wchar_t className[64];
        if (GetClassNameW(child, className, 64) &&
            lstrcmpW(className, kChildClass) == 0) {
            FolderBrowser* browser = BrowserFor(child);
            if (browser) browser->ApplyDefaults();
        }
        child = next;
    }
}

static void UpdateChrome() {
    if (g_shuttingDown) return;
    // Folder controls are now owned by their individual MDI children.
    FolderBrowser* browser = ActiveBrowser();
    if (browser) browser->UpdateControls();
}

void FolderBrowser::OpenMenu(int index) {
    if (index < 0 || index > 6 || !menuBar_ || g_shuttingDown) return;
    HMENU popup = CreatePopupMenu();
    if (!popup) return;
    switch (index) {
    case 0: // File
        AppendMenuW(popup, MF_STRING, IDM_NEW, L"&New window\tCtrl+N");
        AppendMenuW(popup, MF_STRING, IDM_CLOSE, L"&Close window\tCtrl+W");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_EXIT, L"E&xit WindowExplorer");
        break;
    case 1: // Edit
        AppendMenuW(popup, MF_STRING, IDM_EDIT_UNDO, L"&Undo\tCtrl+Z");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_EDIT_CUT, L"Cu&t\tCtrl+X");
        AppendMenuW(popup, MF_STRING, IDM_EDIT_COPY, L"&Copy\tCtrl+C");
        AppendMenuW(popup, MF_STRING, IDM_EDIT_PASTE, L"&Paste\tCtrl+V");
        AppendMenuW(popup, MF_STRING, IDM_EDIT_PASTE_LINK, L"Paste &Shortcut");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_EDIT_COPY_TO, L"Copy To &Folder...");
        AppendMenuW(popup, MF_STRING, IDM_EDIT_MOVE_TO, L"&Move To Folder...");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_SELECT_ALL, L"Select &All\tCtrl+A");
        AppendMenuW(popup, MF_STRING, IDM_EDIT_INVERT, L"&Invert Selection");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_EDIT_DELETE, L"&Delete\tDel");
        AppendMenuW(popup, MF_STRING, IDM_EDIT_RENAME, L"&Rename\tF2");
        AppendMenuW(popup, MF_STRING, IDM_EDIT_PROPERTIES, L"Propert&ies\tAlt+Enter");
        break;
    case 2: // View
        AppendMenuW(popup, MF_STRING | (viewMode_ == FVM_THUMBNAIL ? MF_CHECKED : 0),
            IDM_VIEW_THUMBNAILS, L"&Thumbnails");
        AppendMenuW(popup, MF_STRING | (viewMode_ == FVM_TILE ? MF_CHECKED : 0),
            IDM_VIEW_TILES, L"&Tiles");
        AppendMenuW(popup, MF_STRING | (viewMode_ == FVM_ICON ? MF_CHECKED : 0),
            IDM_VIEW_ICONS, L"Large &Icons");
        AppendMenuW(popup, MF_STRING | (viewMode_ == FVM_LIST ? MF_CHECKED : 0),
            IDM_VIEW_LIST, L"&List");
        AppendMenuW(popup, MF_STRING | (viewMode_ == FVM_DETAILS ? MF_CHECKED : 0),
            IDM_VIEW_DETAILS, L"&Details");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING | (g_defaults.toolbar ? MF_CHECKED : 0),
            IDM_SHOW_TOOLBAR, L"Show &Toolbar");
        AppendMenuW(popup, MF_STRING | (g_defaults.address ? MF_CHECKED : 0),
            IDM_SHOW_ADDRESS, L"Show &Address bar");
        AppendMenuW(popup, MF_STRING | (g_defaults.status ? MF_CHECKED : 0),
            IDM_SHOW_STATUS, L"Show &Status bar");
        AppendMenuW(popup, MF_STRING | (showFolders_ ? MF_CHECKED : 0),
            IDM_FOLDERS, L"Show &Folders pane\tCtrl+F");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_REFRESH, L"&Refresh\tF5");
        break;
    case 3: // Favorites
        AppendMenuW(popup, MF_STRING, IDM_FAVORITE_ADD, L"&Add current folder");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        if (g_favorites.empty())
            AppendMenuW(popup, MF_STRING | MF_GRAYED, 0, L"(No favorites yet)");
        for (size_t i = 0; i < g_favorites.size() &&
                i < IDM_FAVORITE_LAST - IDM_FAVORITE_FIRST + 1; ++i)
            AppendMenuW(popup, MF_STRING, IDM_FAVORITE_FIRST + static_cast<UINT>(i),
                g_favorites[i].c_str());
        break;
    case 4: // Tools
        AppendMenuW(popup, MF_STRING | (showFolders_ ? MF_CHECKED : 0),
            IDM_FOLDERS, L"&Folders pane\tCtrl+F");
        AppendMenuW(popup, MF_STRING, IDM_ADDRESS, L"&Address\tCtrl+L");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_GLOBAL_SETTINGS,
            L"Global display settings...");
        break;
    case 5: // Navigate
        AppendMenuW(popup, MF_STRING | (CanBack() ? 0 : MF_GRAYED),
            IDM_BACK, L"&Back\tAlt+Left");
        AppendMenuW(popup, MF_STRING | (CanForward() ? 0 : MF_GRAYED),
            IDM_FORWARD, L"&Forward\tAlt+Right");
        AppendMenuW(popup, MF_STRING | (CanUp() ? 0 : MF_GRAYED),
            IDM_UP, L"&Up\tAlt+Up");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_ADDRESS, L"&Address\tCtrl+L");
        break;
    case 6: // Window
        AppendMenuW(popup, MF_STRING, IDM_CASCADE, L"&Cascade");
        AppendMenuW(popup, MF_STRING, IDM_TILE_H, L"Tile &horizontally");
        AppendMenuW(popup, MF_STRING, IDM_TILE_V, L"Tile &vertically");
        break;
    }
    RECT anchor;
    GetWindowRect(menuBar_, &anchor);
    int offset = 2;
    for (int i = 0; i < index; ++i) offset += kChildMenuWidths[i];
    anchor.left += offset;
    anchor.bottom = anchor.top + 27;
    g_menuTracking = menuBar_;
    InvalidateRect(menuBar_, NULL, FALSE);
    // Return the selected command rather than allowing a popup to dispatch
    // a WM_COMMAND to the enclosing frame. Post it to this exact child.
    const UINT selected = TrackPopupMenuEx(popup,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        anchor.left, anchor.bottom, child_, NULL);
    DestroyMenu(popup);
    g_menuTracking = NULL;
    if (IsWindow(menuBar_)) InvalidateRect(menuBar_, NULL, FALSE);
    if (selected && IsWindow(child_) && !g_shuttingDown)
        PostMessageW(child_, WM_COMMAND, MAKEWPARAM(selected, 0), 0);
}


/* Store absolute Shell PIDLs in the native TreeView; each node owns a clone.
 * Only enumerate children of expanded nodes to keep My Computer responsive
 * even when a drive or network location is slow to enumerate.
 */
void FolderBrowser::ExpandTree(HTREEITEM node) {
    if (!tree_ || !node || TreeView_GetChild(tree_, node)) return;
    TVITEMW item;
    ZeroMemory(&item, sizeof(item));
    item.hItem = node;
    item.mask = TVIF_PARAM;
    if (!TreeView_GetItem(tree_, &item) || !item.lParam) return;
    LPCITEMIDLIST parent = reinterpret_cast<LPCITEMIDLIST>(item.lParam);
    IShellFolder* desktop = NULL;
    IShellFolder* folder = NULL;
    if (FAILED(SHGetDesktopFolder(&desktop))) return;
    HRESULT hr = S_OK;
    if (parent->mkid.cb == 0) {
        folder = desktop;
        folder->AddRef();
    } else {
        hr = desktop->BindToObject(parent, NULL, IID_IShellFolder,
            reinterpret_cast<void**>(&folder));
    }
    if (SUCCEEDED(hr) && folder) {
        IEnumIDList* enumerator = NULL;
        hr = folder->EnumObjects(child_,
            SHCONTF_FOLDERS | SHCONTF_INCLUDEHIDDEN, &enumerator);
        if (SUCCEEDED(hr) && enumerator) {
            LPITEMIDLIST relative = NULL;
            ULONG fetched = 0;
            while (enumerator->Next(1, &relative, &fetched) == S_OK) {
                LPITEMIDLIST absolute = ILCombine(parent, relative);
                if (absolute) {
                    AddTreeItem(node, absolute);
                    CoTaskMemFree(absolute);
                }
                CoTaskMemFree(relative);
                relative = NULL;
            }
            enumerator->Release();
        }
        folder->Release();
    }
    desktop->Release();
}

void FolderBrowser::SyncTree() {
    if (!tree_ || !treeRoot_ || !pidl_ || syncingTree_ ||
        !showFolders_ || closed_) return;
    syncingTree_ = true;
    HTREEITEM cursor = treeRoot_;
    // The Desktop PIDL is the root of the Shell namespace. Work downward
    // selecting the nearest ancestor of the current location at each level.
    for (int depth = 0; depth < 128 && cursor; ++depth) {
        TVITEMW item;
        ZeroMemory(&item, sizeof(item));
        item.mask = TVIF_PARAM;
        item.hItem = cursor;
        if (!TreeView_GetItem(tree_, &item) || !item.lParam) break;
        LPCITEMIDLIST ancestor = reinterpret_cast<LPCITEMIDLIST>(item.lParam);
        if (ILIsEqual(ancestor, pidl_)) break;
        ExpandTree(cursor);
        TreeView_Expand(tree_, cursor, TVE_EXPAND);
        HTREEITEM next = NULL;
        for (HTREEITEM candidate = TreeView_GetChild(tree_, cursor); candidate;
            candidate = TreeView_GetNextSibling(tree_, candidate)) {
            TVITEMW childItem;
            ZeroMemory(&childItem, sizeof(childItem));
            childItem.mask = TVIF_PARAM;
            childItem.hItem = candidate;
            if (TreeView_GetItem(tree_, &childItem) && childItem.lParam) {
                LPCITEMIDLIST childPidl =
                    reinterpret_cast<LPCITEMIDLIST>(childItem.lParam);
                if (ILIsEqual(childPidl, pidl_) ||
                    ILIsParent(childPidl, pidl_, FALSE)) {
                    next = candidate;
                    break;
                }
            }
        }
        if (!next) break;
        cursor = next;
    }
    if (cursor) TreeView_SelectItem(tree_, cursor);
    syncingTree_ = false;
}

// The Shell view retains ownership of item selection and all clipboard
// formats. Invoke actual Shell context-menu commands for selected objects
// or the folder background rather than implementing a second clipboard.
HRESULT FolderBrowser::InvokeShellVerb(const char* verb, bool background) {
    if (!view_ || !verb) return E_FAIL;
    IContextMenu* context = NULL;
    HRESULT hr = view_->GetItemObject(background ? SVGIO_BACKGROUND :
        SVGIO_SELECTION, IID_IContextMenu,
        reinterpret_cast<void**>(&context));
    if (FAILED(hr)) return hr;
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        context->Release();
        return E_OUTOFMEMORY;
    }
    hr = context->QueryContextMenu(menu, 0, 1, 0x6FFF, CMF_NORMAL);
    if (SUCCEEDED(hr)) {
        CMINVOKECOMMANDINFO info;
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        info.hwnd = child_;
        info.lpVerb = verb;
        info.nShow = SW_SHOWNORMAL;
        hr = context->InvokeCommand(&info);
    }
    DestroyMenu(menu);
    context->Release();
    return hr;
}

HRESULT FolderBrowser::TransferSelectedFiles(bool move) {
    if (!view_) return E_FAIL;
    IDataObject* selected = NULL;
    HRESULT hr = view_->GetItemObject(SVGIO_SELECTION,
        IID_IDataObject, reinterpret_cast<void**>(&selected));
    if (FAILED(hr)) return hr;
    FORMATETC format;
    ZeroMemory(&format, sizeof(format));
    format.cfFormat = CF_HDROP;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    STGMEDIUM medium;
    ZeroMemory(&medium, sizeof(medium));
    hr = selected->GetData(&format, &medium);
    selected->Release();
    if (FAILED(hr)) return hr; // Virtual Shell folders may not expose paths.

    HDROP files = reinterpret_cast<HDROP>(medium.hGlobal);
    const UINT count = DragQueryFileW(files, 0xFFFFFFFF, NULL, 0);
    std::vector<wchar_t> paths;
    for (UINT i = 0; i < count; ++i) {
        UINT length = DragQueryFileW(files, i, NULL, 0);
        if (!length || length > 32760) continue;
        size_t offset = paths.size();
        paths.resize(offset + length + 1);
        if (DragQueryFileW(files, i, &paths[offset], length + 1) != length)
            paths.resize(offset);
    }
    ReleaseStgMedium(&medium);
    if (paths.empty()) return S_FALSE;
    paths.push_back(0); // SHFileOperation requires a double-NUL list.

    BROWSEINFOW browse;
    ZeroMemory(&browse, sizeof(browse));
    browse.hwndOwner = child_;
    browse.lpszTitle = move ? L"Move selected files to folder:" :
        L"Copy selected files to folder:";
    browse.ulFlags = BIF_RETURNONLYFSDIRS;
    LPITEMIDLIST destination = SHBrowseForFolderW(&browse);
    if (!destination) return S_FALSE; // The user cancelled.
    wchar_t directory[MAX_PATH];
    const BOOL valid = SHGetPathFromIDListW(destination, directory);
    CoTaskMemFree(destination);
    if (!valid) return E_FAIL;
    std::vector<wchar_t> target(directory,
        directory + lstrlenW(directory) + 1);
    target.push_back(0);
    SHFILEOPSTRUCTW operation;
    ZeroMemory(&operation, sizeof(operation));
    operation.hwnd = child_;
    operation.wFunc = move ? FO_MOVE : FO_COPY;
    operation.pFrom = &paths[0];
    operation.pTo = &target[0];
    operation.fFlags = FOF_ALLOWUNDO;
    const int error = SHFileOperationW(&operation);
    if (error || operation.fAnyOperationsAborted)
        return error ? HRESULT_FROM_WIN32(static_cast<DWORD>(error)) : S_FALSE;
    view_->Refresh();
    return S_OK;
}

HRESULT FolderBrowser::EditCommand(UINT command) {
    if (!view_ || closed_) return E_FAIL;
    if (command == IDM_EDIT_COPY_TO || command == IDM_EDIT_MOVE_TO)
        return TransferSelectedFiles(command == IDM_EDIT_MOVE_TO);
    if (command == IDM_SELECT_ALL) { SelectAll(); return S_OK; }

    const char* verb = NULL;
    bool background = false;
    UINT shellId = 0;
    switch (command) {
    case IDM_EDIT_UNDO:
        verb = "undo"; background = true; shellId = 0x701B; break;
    case IDM_EDIT_CUT:
        verb = "cut"; shellId = 0x7018; break;
    case IDM_EDIT_COPY:
        verb = "copy"; shellId = 0x7019; break;
    case IDM_EDIT_PASTE:
        verb = "paste"; background = true; shellId = 0x701A; break;
    case IDM_EDIT_PASTE_LINK:
        verb = "pastelink"; background = true; shellId = 0x701C; break;
    case IDM_EDIT_INVERT:
        shellId = 0x7022; break;
    case IDM_EDIT_DELETE:
        verb = "delete"; shellId = 0x7011; break;
    case IDM_EDIT_RENAME:
        verb = "rename"; shellId = 0x7050; break;
    case IDM_EDIT_PROPERTIES:
        verb = "properties"; shellId = 0x7013; break;
    default: return E_INVALIDARG;
    }

    // Prefer the documented IContextMenu verb interface. The legacy
    // command IDs are a best-effort fallback for XP's SHELLDLL_DefView;
    // they are not a public cross-version Shell API.
    HRESULT hr = verb ? InvokeShellVerb(verb, background) : E_NOTIMPL;
    if (FAILED(hr) && viewWindow_ && IsWindow(viewWindow_) && shellId) {
        SendMessageW(viewWindow_, WM_COMMAND, MAKEWPARAM(shellId, 0), 0);
        hr = S_OK; // WM_COMMAND does not report whether the Shell handled it.
    }
    return hr;
}

// On XP the Shell's SysListView32 can be nested several levels below the
// IShellView HWND (including SHELLDLL_DefView). The prior one-level lookup
// missed it, which made Edit > Select All appear to do nothing while the
// Shell's own Ctrl+A keyboard handler continued to work.
static HWND FindShellListView(HWND parent) {
    if (!parent || !IsWindow(parent)) return NULL;
    wchar_t className[64];
    if (GetClassNameW(parent, className, 64) &&
        lstrcmpiW(className, WC_LISTVIEWW) == 0)
        return parent;
    for (HWND child = GetWindow(parent, GW_CHILD); child;
         child = GetWindow(child, GW_HWNDNEXT)) {
        HWND match = FindShellListView(child);
        if (match) return match;
    }
    return NULL;
}

void FolderBrowser::SelectAll() {
    if (!viewWindow_ || !IsWindow(viewWindow_)) return;
    HWND list = FindShellListView(viewWindow_);
    if (list) {
        SetFocus(list);
        ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
    }
}

static HRESULT NewFolderWindow(LPCITEMIDLIST location) {
    if (!g_mdi || g_shuttingDown) return E_FAIL;
    MDICREATESTRUCTW create;
    ZeroMemory(&create, sizeof(create));
    create.szClass = kChildClass;
    create.szTitle = L"Folder";
    create.hOwner = g_instance;
    create.x = CW_USEDEFAULT;
    create.y = CW_USEDEFAULT;
    create.cx = CW_USEDEFAULT;
    create.cy = CW_USEDEFAULT;
    create.style = WS_VISIBLE | WS_OVERLAPPEDWINDOW;
    HWND child = reinterpret_cast<HWND>(SendMessageW(g_mdi, WM_MDICREATE,
        0, reinterpret_cast<LPARAM>(&create)));
    if (!child) return E_FAIL;

    FolderBrowser* browser = BrowserFor(child);
    HRESULT hr = browser ? S_OK : E_OUTOFMEMORY;
    LPITEMIDLIST drives = NULL;
    LPCITEMIDLIST destination = location;
    if (SUCCEEDED(hr) && !destination) {
        hr = SHGetSpecialFolderLocation(child, CSIDL_DRIVES, &drives);
        destination = drives;
    }
    if (SUCCEEDED(hr)) hr = browser->Navigate(destination);
    if (drives) CoTaskMemFree(drives);
    if (FAILED(hr)) {
        SendMessageW(g_mdi, WM_MDIDESTROY,
            reinterpret_cast<WPARAM>(child), 0);
    } else {
        SendMessageW(g_mdi, WM_MDIACTIVATE,
            reinterpret_cast<WPARAM>(child), 0);
        browser->Activate(true);
        UpdateChrome();
    }
    return hr;
}

static LRESULT CALLBACK ChildProc(HWND hwnd, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    FolderBrowser* browser = BrowserFor(hwnd);
    switch (message) {
    case WM_CREATE:
        browser = new(std::nothrow) FolderBrowser(hwnd);
        if (!browser) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(browser));
        if (!browser->CreateControls()) return -1;
        return 0;
    case WM_SIZE:
        if (browser && wParam != SIZE_MINIMIZED) browser->Layout();
        break;
    case WM_OPEN_SHELL_FOLDER:
        if (browser)
            browser->HandleOpenFolder(
                reinterpret_cast<LPITEMIDLIST>(lParam), wParam != 0);
        return 0;
    case WM_NOTIFY:
        if (browser && lParam) {
            NMHDR* notification = reinterpret_cast<NMHDR*>(lParam);
            if (g_shuttingDown && notification->code != TVN_DELETEITEMW)
                return 0;
            if (notification->code == TTN_GETDISPINFOW) {
                NMTTDISPINFOW* tip =
                    reinterpret_cast<NMTTDISPINFOW*>(lParam);
                switch (notification->idFrom) {
                case IDC_BACK: tip->lpszText = const_cast<LPWSTR>(L"Back"); break;
                case IDC_FORWARD: tip->lpszText = const_cast<LPWSTR>(L"Forward"); break;
                case IDC_UP: tip->lpszText = const_cast<LPWSTR>(L"Up one level"); break;
                case IDC_FOLDERS: tip->lpszText = const_cast<LPWSTR>(L"Folders pane"); break;
                case IDC_TOOL_NEW: tip->lpszText = const_cast<LPWSTR>(L"New MDI window"); break;
                case IDC_TOOL_REFRESH: tip->lpszText = const_cast<LPWSTR>(L"Refresh"); break;
                default: break;
                }
                return 0;
            }
            if (notification->idFrom == IDC_TREE) {
                NMTREEVIEWW* change = reinterpret_cast<NMTREEVIEWW*>(lParam);
                if (notification->code == TVN_ITEMEXPANDINGW &&
                    change->action == TVE_EXPAND) {
                    browser->ExpandTree(change->itemNew.hItem);
                    return 0;
                }
                if (notification->code == TVN_SELCHANGEDW) {
                    browser->TreeSelectionChanged(change->itemNew.hItem);
                    return 0;
                }
                if (notification->code == TVN_DELETEITEMW) {
                    browser->DeleteTreeItem(change->itemOld.lParam);
                    return 0;
                }
            }
        }
        break;
    case WM_SETFOCUS:
        if (!g_shuttingDown && browser && browser->View())
            browser->Activate(true);
        return 0;
    case WM_MDIACTIVATE:
        if (browser && !g_shuttingDown) {
            const bool active = (reinterpret_cast<HWND>(lParam) == hwnd);
            browser->Activate(active);
            if (active) browser->UpdateControls();
        }
        if (!g_shuttingDown && g_frame)
            PostMessageW(g_frame, WM_UPDATE_CHROME, 0, 0);
        break;
    case WM_COMMAND: {
        if (!browser || g_shuttingDown) return 0;
        const int command = LOWORD(wParam);
        if (command == IDC_ADDRESS && HIWORD(wParam) == EN_CHANGE) {
            browser->AddressChanged();
            return 0;
        }
        // A native toolbar emits WM_COMMAND with HIWORD(wParam) == 0,
        // not BN_CLICKED; handle these IDs independently of notify code.
        if (command == IDC_FOLDERS) {
            browser->ToggleFolders();
            return 0;
        }
        if (command == IDC_TOOL_NEW) {
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_NEW, 0), 0);
            return 0;
        }
        if (command == IDC_TOOL_REFRESH) {
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDM_REFRESH, 0), 0);
            return 0;
        }
        HRESULT hr = S_OK;
        if (command >= IDM_FAVORITE_FIRST &&
            command <= IDM_FAVORITE_LAST) {
            size_t index = static_cast<size_t>(command - IDM_FAVORITE_FIRST);
            if (index < g_favorites.size()) {
                hr = browser->Go(g_favorites[index].c_str());
                if (FAILED(hr)) ShowFailure(hwnd, L"Open favorite", hr);
            }
            return 0;
        }
        switch (command) {
        case IDM_MENU_FILE: browser->OpenMenu(0); return 0;
        case IDM_MENU_EDIT: browser->OpenMenu(1); return 0;
        case IDM_MENU_VIEW: browser->OpenMenu(2); return 0;
        case IDM_MENU_FAVORITES: browser->OpenMenu(3); return 0;
        case IDM_MENU_TOOLS: browser->OpenMenu(4); return 0;
        case IDM_MENU_NAVIGATE: browser->OpenMenu(5); return 0;
        case IDM_MENU_WINDOW: browser->OpenMenu(6); return 0;
        case IDC_GO:
        case IDM_GO:
            browser->AddressGo(); return 0;
        case IDM_ADDRESS:
            browser->FocusAddress(); return 0;
        case IDC_BACK:
        case IDM_BACK:
            hr = browser->Back();
            if (FAILED(hr)) ShowFailure(hwnd, L"Back", hr);
            return 0;
        case IDC_FORWARD:
        case IDM_FORWARD:
            hr = browser->Forward();
            if (FAILED(hr)) ShowFailure(hwnd, L"Forward", hr);
            return 0;
        case IDC_UP:
        case IDM_UP:
            hr = browser->Up();
            if (FAILED(hr)) ShowFailure(hwnd, L"Parent folder", hr);
            return 0;
        case IDM_NEW:
            hr = NewFolderWindow(browser->Location());
            if (FAILED(hr)) ShowFailure(hwnd, L"New window", hr);
            return 0;
        case IDM_CLOSE:
            SendMessageW(g_mdi, WM_MDIDESTROY,
                reinterpret_cast<WPARAM>(hwnd), 0);
            return 0;
        case IDM_REFRESH:
            if (browser->View()) browser->View()->Refresh();
            return 0;
        case IDM_VIEW_ICONS:
        case IDM_VIEW_LIST:
        case IDM_VIEW_DETAILS:
        case IDM_VIEW_THUMBNAILS:
        case IDM_VIEW_TILES: {
            FOLDERVIEWMODE mode = FVM_DETAILS;
            if (command == IDM_VIEW_ICONS) mode = FVM_ICON;
            else if (command == IDM_VIEW_LIST) mode = FVM_LIST;
            else if (command == IDM_VIEW_THUMBNAILS) mode = FVM_THUMBNAIL;
            else if (command == IDM_VIEW_TILES) mode = FVM_TILE;
            hr = browser->ChangeViewMode(mode);
            if (FAILED(hr)) ShowFailure(hwnd, L"Change folder view", hr);
            else {
                g_defaults.mode = mode;
                SaveDefaults();
                ApplyDefaultsToAllChildren();
            }
            return 0;
        }
        case IDM_FOLDERS:
            browser->ToggleFolders();
            return 0;
        case IDM_SHOW_STATUS:
            g_defaults.status = !g_defaults.status;
            SaveDefaults();
            ApplyDefaultsToAllChildren();
            return 0;
        case IDM_SHOW_TOOLBAR:
            g_defaults.toolbar = !g_defaults.toolbar;
            SaveDefaults();
            ApplyDefaultsToAllChildren();
            return 0;
        case IDM_SHOW_ADDRESS:
            g_defaults.address = !g_defaults.address;
            SaveDefaults();
            ApplyDefaultsToAllChildren();
            return 0;
        case IDM_GLOBAL_SETTINGS:
            MessageBoxW(hwnd,
                L"Display settings are shared by all WindowExplorer MDI windows "
                L"and saved for the next application launch.\n\n"
                L"Use View to select Thumbnails, Tiles, Icons, List or Details, and toggle the "
                L"toolbar, address bar, status bar or folder tree.\n\n"
                L"Settings are stored per Windows user in "
                L"HKCU\\Software\\Magneticon\\WindowExplorer.\n\n",
                L"Global display settings", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_EDIT_UNDO:
        case IDM_EDIT_CUT:
        case IDM_EDIT_COPY:
        case IDM_EDIT_PASTE:
        case IDM_EDIT_PASTE_LINK:
        case IDM_EDIT_COPY_TO:
        case IDM_EDIT_MOVE_TO:
        case IDM_SELECT_ALL:
        case IDM_EDIT_INVERT:
        case IDM_EDIT_DELETE:
        case IDM_EDIT_RENAME:
        case IDM_EDIT_PROPERTIES:
            hr = browser->EditCommand(command);
            if (FAILED(hr))
                ShowFailure(hwnd, L"Shell Edit command", hr);
            return 0;
        case IDM_FAVORITE_ADD: {
            // Filesystem favorites are also persisted for the current Windows user.
            wchar_t realPath[MAX_PATH];
            if (browser->Location() &&
                SHGetPathFromIDListW(browser->Location(), realPath)) {
                if (std::find(g_favorites.begin(), g_favorites.end(), realPath)
                    == g_favorites.end()) {
                    g_favorites.push_back(realPath);
                    SaveDefaults();
                }
            } else {
                MessageBoxW(hwnd, L"Favorites currently support filesystem folders.",
                    L"WindowExplorer", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }

        case IDM_CASCADE:
            SendMessageW(g_mdi, WM_MDICASCADE, 0, 0);
            return 0;
        case IDM_TILE_H:
            SendMessageW(g_mdi, WM_MDITILE, MDITILE_HORIZONTAL, 0);
            return 0;
        case IDM_TILE_V:
            SendMessageW(g_mdi, WM_MDITILE, MDITILE_VERTICAL, 0);
            return 0;
        case IDM_EXIT:
            PostMessageW(g_frame, WM_CLOSE, 0, 0);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_DESTROY:
        if (browser) browser->Close();
        break;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (browser) browser->Release();
        break;
    }
    return DefMDIChildProcW(hwnd, message, wParam, lParam);
}

// A custom modal About dialog: XP-inspired Windows banner + our little tale
// instead of licensing, memory and OS-version details. No external bitmap
// or modern UI framework is needed, so it also runs under XP x64.
static INT_PTR CALLBACK AboutWindowsProc(HWND dialog, UINT message,
                                         WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG: {
        RECT client;
        GetClientRect(dialog, &client);
        HDC metricsDc = GetDC(dialog);
        const int dpi = metricsDc ? GetDeviceCaps(metricsDc, LOGPIXELSY) : 96;
        if (metricsDc) ReleaseDC(dialog, metricsDc);
        const int margin = MulDiv(16, dpi, 96);
        const int buttonW = MulDiv(78, dpi, 96);
        const int buttonH = MulDiv(27, dpi, 96);
        HWND ok = CreateWindowW(L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            client.right - margin - buttonW,
            client.bottom - margin - buttonH, buttonW, buttonH,
            dialog, reinterpret_cast<HMENU>(IDOK), g_instance, NULL);
        if (ok) SetFocus(ok);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(dialog, &paint);
        RECT client;
        GetClientRect(dialog, &client);
        const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
        const int bannerH = MulDiv(100, dpi, 96);
        RECT banner = { 0, 0, client.right, bannerH };
        HBRUSH bannerBrush = CreateSolidBrush(RGB(79, 124, 207));
        FillRect(dc, &banner, bannerBrush);
        DeleteObject(bannerBrush);
        // Four coloured panes evoke the original Windows logo without
        // depending on a version-specific shell32.dll bitmap resource.
        const int x = MulDiv(23, dpi, 96);
        const int y = MulDiv(22, dpi, 96);
        POINT panes[4][4] = {
            { {x+0,y+5}, {x+30,y+1}, {x+29,y+28}, {x+0,y+31} },
            { {x+35,y+1}, {x+68,y+0}, {x+68,y+29}, {x+34,y+28} },
            { {x+0,y+36}, {x+29,y+33}, {x+29,y+60}, {x+0,y+64} },
            { {x+34,y+33}, {x+68,y+34}, {x+68,y+64}, {x+34,y+60} }
        };
        COLORREF colors[4] = {
            RGB(234, 80, 44), RGB(110, 178, 42),
            RGB(49, 141, 224), RGB(241, 198, 48)
        };
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        for (int i = 0; i < 4; ++i) {
            HBRUSH brush = CreateSolidBrush(colors[i]);
            HGDIOBJ oldBrush = SelectObject(dc, brush);
            Polygon(dc, panes[i], 4);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
        }
        SelectObject(dc, oldPen);
        SetBkMode(dc, TRANSPARENT);
        HFONT titleFont = CreateFontW(-MulDiv(33, dpi, 96), 0, 0, 0,
            FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        HGDIOBJ oldFont = SelectObject(dc, titleFont);
        SetTextColor(dc, RGB(255,255,255));
        RECT heading = { MulDiv(112,dpi,96), MulDiv(23,dpi,96),
                         client.right - 10, bannerH };
        DrawTextW(dc, L"Windows", -1, &heading, DT_SINGLELINE | DT_LEFT);
        SelectObject(dc, oldFont);
        DeleteObject(titleFont);

        RECT body = client;
        body.left += MulDiv(22, dpi, 96);
        body.right -= MulDiv(20, dpi, 96);
        body.top = bannerH + MulDiv(18, dpi, 96);
        body.bottom -= MulDiv(49, dpi, 96);
        HFONT storyFont = CreateFontW(-MulDiv(17, dpi, 96), 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        oldFont = SelectObject(dc, storyFont);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        const wchar_t tale[] =
            L"Once upon a time, there was a window. It was lonely, "
            L"so it opened its shutters and invited the world in. "
            L"Soon it had more friends than it could count.\r\n\r\n"
            L"First, there were 3 of them. Then, there were 95 of them. "
            L"Some were NT, and some were a little ME. "
            L"Five years later, 2000 windows were running around!\r\n\r\n"
            L"One cheerful window was called XP. It loved bright "
            L"colors, green hills, and keeping old friends close. "
            L"Then came Vista. She was an arrogant window. Nobody liked her."
            L"Over time, only 7 old friends left. And the times were never the same again.\r\n\r\n"
            L"Unfortunately, nowadays, only 11 windows are left. "
            L"However, The others have not truly vanished, though. "
            L"Some are still hidding away on the old desks, tucked inside little "
            L"windows of their own, quietly waiting for someone "
            L"to open them again.";
        DrawTextW(dc, tale, -1, &body, DT_LEFT | DT_TOP | DT_WORDBREAK);
        SelectObject(dc, oldFont);
        DeleteObject(storyFont);
        EndPaint(dialog, &paint);
        return TRUE;
    }
    }
    return FALSE;
}

static void ShowAboutWindows(HWND owner) {
    struct AboutTemplate {
        DLGTEMPLATE dlg;
        WORD menu;
        WORD windowClass;
        WCHAR title[16];
    };
    AboutTemplate t;
    ZeroMemory(&t, sizeof(t));
    t.dlg.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    t.dlg.cdit = 0;
    t.dlg.cx = 310;
    t.dlg.cy = 275; // Extra space for the expanded tale at XP font sizes.
    lstrcpyW(t.title, L"About Windows");
    // A modal dialog owned by the MDI frame keeps this story independent
    // of whichever folder window currently has focus.
    DialogBoxIndirectParamW(g_instance, &t.dlg, owner,
        AboutWindowsProc, 0);
}

static HMENU MakeMenu() {
    // A minimal application-level menu is retained for Exit and the MDI
    // window list. The folder-specific command menus live in each child.
    HMENU menu = CreateMenu();
    HMENU application = CreatePopupMenu();
    HMENU windows = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    AppendMenuW(application, MF_STRING, IDM_NEW, L"&New folder window\tCtrl+N");
    AppendMenuW(application, MF_SEPARATOR, 0, NULL);
    AppendMenuW(application, MF_STRING, IDM_EXIT, L"E&xit");
    AppendMenuW(windows, MF_STRING, IDM_CASCADE, L"&Cascade");
    AppendMenuW(windows, MF_STRING, IDM_TILE_H, L"Tile &horizontally");
    AppendMenuW(windows, MF_STRING, IDM_TILE_V, L"Tile &vertically");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(application),
        L"&Application");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(windows),
        L"&Windows");
    AppendMenuW(help, MF_STRING, IDM_ABOUT_WINDOWS, L"&About Windows...");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");
    return menu;
}

static void LayoutFrame(HWND frame) {
    if (!g_mdi) return;
    RECT rc;
    GetClientRect(frame, &rc);
    MoveWindow(g_mdi, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
}

static LRESULT CALLBACK FrameProc(HWND hwnd, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        SetMenu(hwnd, MakeMenu());
        CLIENTCREATESTRUCT create;
        create.hWindowMenu = GetSubMenu(GetMenu(hwnd), 1);
        create.idFirstChild = IDM_FIRST_CHILD;
        g_mdi = CreateWindowExW(WS_EX_CLIENTEDGE, L"MDICLIENT", NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
            WS_HSCROLL | WS_VSCROLL | MDIS_ALLCHILDSTYLES,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_MDI),
            g_instance, &create);
        if (!g_mdi) return -1;
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) LayoutFrame(hwnd);
        return 0;
    case WM_UPDATE_CHROME:
        if (!g_shuttingDown) UpdateChrome();
        return 0;
    case WM_COMMAND: {
        const int command = LOWORD(wParam);
        if (command == IDM_ABOUT_WINDOWS) {
            if (!g_shuttingDown) ShowAboutWindows(hwnd);
            return 0;
        }
        if (command == IDM_EXIT) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if ((command >= IDM_NEW && command <= IDM_EDIT_PROPERTIES) ||
            (command >= IDM_MENU_FILE && command <= IDM_MENU_WINDOW)) {
            FolderBrowser* browser = ActiveBrowser();
            if (browser)
                return SendMessageW(browser->Child(), WM_COMMAND,
                    MAKEWPARAM(command, 0), 0);
            if (command == IDM_NEW) {
                HRESULT hr = NewFolderWindow(NULL);
                if (FAILED(hr)) ShowFailure(hwnd, L"New folder window", hr);
            }
            return 0;
        }
        // Let the MDI frame handle its automatic child-window menu IDs.
        break;
    }
    case WM_SETFOCUS:
        if (g_mdi && !g_shuttingDown) SetFocus(g_mdi);
        return 0;
    case WM_CLOSE: {
        if (g_shuttingDown) return 0;
        g_shuttingDown = true;
        // Explicitly close the native Shell views before their MDI client and
        // OLE apartment go away. Leaving active views during parent teardown
        // can keep Shell extensions and window references alive after Exit.
        if (g_mdi && IsWindow(g_mdi)) {
            for (;;) {
                HWND victim = NULL;
                for (HWND child = GetWindow(g_mdi, GW_CHILD);
                     child; child = GetWindow(child, GW_HWNDNEXT)) {
                    wchar_t klass[64];
                    if (GetClassNameW(child, klass, 64) &&
                        lstrcmpW(klass, kChildClass) == 0) {
                        victim = child;
                        break;
                    }
                }
                if (!victim) break;
                SendMessageW(g_mdi, WM_MDIDESTROY,
                    reinterpret_cast<WPARAM>(victim), 0);
                // Avoid an endless loop if an extension prevented destruction.
                if (IsWindow(victim)) {
                    DestroyWindow(victim);
                    if (IsWindow(victim)) break;
                }
            }
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        g_mdi = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefFrameProcW(hwnd, g_mdi, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    g_instance = instance;
    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr)) {
        ShowFailure(NULL, L"COM initialization", hr);
        return 1;
    }
    INITCOMMONCONTROLSEX common;
    common.dwSize = sizeof(common);
    common.dwICC = ICC_BAR_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&common);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    LoadDefaults();
    wc.lpfnWndProc = FrameProc;
    wc.lpszClassName = kFrameClass;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_APPWORKSPACE + 1);
    if (!RegisterClassExW(&wc)) {
        OleUninitialize();
        return 1;
    }
    wc.lpfnWndProc = ChildMenuProc;
    wc.lpszClassName = kMenuStripClass;
    wc.hbrBackground = GetSysColorBrush(COLOR_MENU);
    if (!RegisterClassExW(&wc)) {
        OleUninitialize();
        return 1;
    }
    wc.lpfnWndProc = ChildProc;
    wc.lpszClassName = kChildClass;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc)) {
        OleUninitialize();
        return 1;
    }

    ACCEL keys[] = {
        { FVIRTKEY | FCONTROL, 'N', IDM_NEW },
        { FVIRTKEY | FCONTROL, 'W', IDM_CLOSE },
        { FVIRTKEY | FCONTROL, 'L', IDM_ADDRESS },
        { FVIRTKEY | FCONTROL, 'F', IDM_FOLDERS },
        { FVIRTKEY | FCONTROL, 'A', IDM_SELECT_ALL },
        { FVIRTKEY | FALT, 'F', IDM_MENU_FILE },
        { FVIRTKEY | FALT, 'E', IDM_MENU_EDIT },
        { FVIRTKEY | FALT, 'V', IDM_MENU_VIEW },
        { FVIRTKEY | FALT, 'O', IDM_MENU_FAVORITES },
        { FVIRTKEY | FALT, 'T', IDM_MENU_TOOLS },
        { FVIRTKEY | FALT, 'N', IDM_MENU_NAVIGATE },
        { FVIRTKEY | FALT, 'W', IDM_MENU_WINDOW },
        { FVIRTKEY | FCONTROL, 'Z', IDM_EDIT_UNDO },
        { FVIRTKEY | FCONTROL, 'X', IDM_EDIT_CUT },
        { FVIRTKEY | FCONTROL, 'C', IDM_EDIT_COPY },
        { FVIRTKEY | FCONTROL, 'V', IDM_EDIT_PASTE },
        { FVIRTKEY, VK_F2, IDM_EDIT_RENAME },
        { FVIRTKEY, VK_DELETE, IDM_EDIT_DELETE },
        { FVIRTKEY | FALT, VK_RETURN, IDM_EDIT_PROPERTIES },
        { FVIRTKEY | FALT, VK_LEFT, IDM_BACK },
        { FVIRTKEY | FALT, VK_RIGHT, IDM_FORWARD },
        { FVIRTKEY | FALT, VK_UP, IDM_UP },
        { FVIRTKEY, VK_F5, IDM_REFRESH }
    };
    g_accel = CreateAcceleratorTableW(keys, sizeof(keys) / sizeof(keys[0]));
    g_frame = CreateWindowExW(0, kFrameClass,
        L"WindowExplorer",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 700, NULL, NULL, instance, NULL);
    if (!g_frame) {
        if (g_accel) DestroyAcceleratorTable(g_accel);
        OleUninitialize();
        return 1;
    }
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    hr = NewFolderWindow(NULL);
    if (FAILED(hr)) ShowFailure(g_frame, L"Open My Computer", hr);

    MSG msg;
    int code = 0;
    BOOL got;
    while ((got = GetMessageW(&msg, NULL, 0, 0)) > 0) {
        FolderBrowser* browser = ActiveBrowser();
        bool shellHandled = false;
        if (browser && browser->View() && browser->ViewWindow()) {
            HWND focus = GetFocus();
            if (focus == browser->ViewWindow() ||
                IsChild(browser->ViewWindow(), focus))
                shellHandled = (browser->View()->TranslateAccelerator(&msg) == S_OK);
        }
        if (shellHandled) continue;
        // Enter navigates only the address bar of the currently active
        // folder; an inactive MDI document retains its own edit text.
        if (browser && browser->AddressEdit() &&
            GetFocus() == browser->AddressEdit() &&
            msg.hwnd == browser->AddressEdit() &&
            msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            browser->AddressGo();
            continue;
        }
        if (browser && browser->AddressEdit() &&
            GetFocus() == browser->AddressEdit() &&
            msg.hwnd == browser->AddressEdit() &&
            msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            browser->CancelAddress();
            continue;
        }
        // The editable address bar keeps its own standard edit shortcuts.
        // Do not turn Ctrl+A/C/X/V/Z or Delete into file operations there.
        bool addressKey = false;
        if (browser && browser->AddressEdit() &&
            GetFocus() == browser->AddressEdit()) {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
                if (ctrl && msg.wParam == 'A') {
                    SendMessageW(browser->AddressEdit(), EM_SETSEL, 0, -1);
                    continue;
                }
                if ((ctrl && (msg.wParam == 'X' || msg.wParam == 'C' ||
                    msg.wParam == 'V' || msg.wParam == 'Z')) ||
                    msg.wParam == VK_DELETE)
                    addressKey = true;
            }
        }
        if (!addressKey && g_accel &&
            TranslateAcceleratorW(g_frame, g_accel, &msg)) continue;
        if (g_mdi && TranslateMDISysAccel(g_mdi, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (got == 0) code = static_cast<int>(msg.wParam);
    else code = 1;
    if (g_accel) DestroyAcceleratorTable(g_accel);
    g_frame = NULL;
    OleUninitialize();
    return code;
}

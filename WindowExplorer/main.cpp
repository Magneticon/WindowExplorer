// WindowExplorer: native MDI host for Windows shell folder views.
// The application hosts IShellView in-process; it does not reparent explorer.exe.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <vector>
#include <string>
#include <new>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

enum {
    IDC_MDI = 100, IDC_ADDRESS = 101, IDC_GO = 102,
    IDC_BACK = 103, IDC_UP = 104, IDC_STATUS = 105,
    IDC_FORWARD = 106, IDC_CHILD_MENU_FILE = 107,
    IDC_CHILD_MENU_NAVIGATE = 108, IDC_CHILD_MENU_VIEW = 109,
    IDC_CHILD_MENU_WINDOW = 110,
    IDM_NEW = 1001, IDM_CLOSE = 1002, IDM_EXIT = 1003,
    IDM_BACK = 1004, IDM_UP = 1005, IDM_GO = 1006,
    IDM_REFRESH = 1007, IDM_CASCADE = 1008,
    IDM_TILE_H = 1009, IDM_TILE_V = 1010, IDM_ADDRESS = 1011,
    IDM_FORWARD = 1012,
    IDM_FIRST_CHILD = 30000,
    WM_UPDATE_CHROME = WM_APP + 1
};

static const wchar_t kFrameClass[] = L"WindowExplorer.Frame";
static const wchar_t kChildClass[] = L"WindowExplorer.Folder";
static HINSTANCE g_instance = NULL;
static HWND g_frame = NULL, g_mdi = NULL;
static HACCEL g_accel = NULL;

class FolderBrowser;
static FolderBrowser* ActiveBrowser();
static HRESULT NewFolderWindow(LPCITEMIDLIST location);
static void UpdateChrome();
static void LayoutFrame(HWND frame);
static void ShowFailure(HWND owner, const wchar_t* operation, HRESULT hr);

class FolderBrowser : public IShellBrowser {
public:
    explicit FolderBrowser(HWND child)
        : refs_(1), child_(child), viewWindow_(NULL), view_(NULL),
          folder_(NULL), pidl_(NULL), historyIndex_(-1),
          navigating_(false), closed_(false), editingAddress_(false),
          address_(NULL), back_(NULL), forward_(NULL), up_(NULL),
          go_(NULL), status_(NULL) {
        for (int i = 0; i < 4; ++i) menus_[i] = NULL;
    }

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
    LPCITEMIDLIST Location() const { return pidl_; }
    HWND AddressEdit() const { return address_; }

    bool CreateControls() {
        const wchar_t* captions[] = { L"&File", L"&Navigate", L"&View", L"&Window" };
        const int ids[] = { IDC_CHILD_MENU_FILE, IDC_CHILD_MENU_NAVIGATE,
                            IDC_CHILD_MENU_VIEW, IDC_CHILD_MENU_WINDOW };
        for (int i = 0; i < 4; ++i) {
            menus_[i] = CreateWindowW(L"BUTTON", captions[i],
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 0, 0, child_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),
                g_instance, NULL);
        }
        back_ = CreateWindowW(L"BUTTON", L"Back", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, child_, reinterpret_cast<HMENU>(IDC_BACK),
            g_instance, NULL);
        forward_ = CreateWindowW(L"BUTTON", L"Forward", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, child_, reinterpret_cast<HMENU>(IDC_FORWARD),
            g_instance, NULL);
        up_ = CreateWindowW(L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, child_, reinterpret_cast<HMENU>(IDC_UP),
            g_instance, NULL);
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
        for (int i = 0; i < 4; ++i) if (!menus_[i]) return false;
        if (!back_ || !forward_ || !up_ || !address_ || !go_ || !status_)
            return false;
        Layout();
        UpdateControls();
        return true;
    }

    void ViewRect(RECT* rect) const {
        GetClientRect(child_, rect);
        rect->left = 0;
        rect->top = 58;
        rect->bottom -= 21;
        if (rect->right < rect->left) rect->right = rect->left;
        if (rect->bottom < rect->top) rect->bottom = rect->top;
    }

    void Layout() {
        RECT rc;
        GetClientRect(child_, &rc);
        const int width = rc.right - rc.left;
        const int height = rc.bottom - rc.top;
        const int menuWidths[] = { 54, 85, 55, 75 };
        int x = 3;
        for (int i = 0; i < 4; ++i) {
            if (menus_[i]) MoveWindow(menus_[i], x, 2, menuWidths[i], 24, TRUE);
            x += menuWidths[i] + 2;
        }
        if (back_) MoveWindow(back_, 3, 30, 51, 24, TRUE);
        if (forward_) MoveWindow(forward_, 56, 30, 65, 24, TRUE);
        if (up_) MoveWindow(up_, 123, 30, 38, 24, TRUE);
        int addressWidth = width - 214;
        if (addressWidth < 20) addressWidth = 20;
        if (address_) MoveWindow(address_, 165, 30, addressWidth, 24, TRUE);
        if (go_) MoveWindow(go_, width - 45, 30, 42, 24, TRUE);
        if (status_) MoveWindow(status_, 4, height - 20,
            width > 8 ? width - 8 : 0, 18, TRUE);
        Resize();
    }

    void UpdateControls() {
        if (back_) EnableWindow(back_, CanBack());
        if (forward_) EnableWindow(forward_, CanForward());
        if (up_) EnableWindow(up_, CanUp());
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
    }

    void AddressChanged() {
        if (address_ && GetFocus() == address_) editingAddress_ = true;
    }
    void FocusAddress() {
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
            settings.ViewMode = FVM_DETAILS;
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

    // IUnknown / IOleWindow / IShellBrowser
    STDMETHODIMP QueryInterface(REFIID riid, void** object) {
        if (!object) return E_POINTER;
        *object = NULL;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IOleWindow) ||
            IsEqualIID(riid, IID_IShellBrowser)) {
            *object = static_cast<IShellBrowser*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
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
        if (closed_ || navigating_) return E_UNEXPECTED;
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
        HRESULT hr;
        if (flags & SBSP_NEWBROWSER)
            hr = NewFolderWindow(absolute);
        else
            hr = Navigate(absolute);
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
    int historyIndex_;
    bool navigating_;
    bool closed_;
    bool editingAddress_;
    HWND menus_[4];
    HWND address_, back_, forward_, up_, go_, status_;
};

static FolderBrowser* BrowserFor(HWND child) {
    return child ? reinterpret_cast<FolderBrowser*>(
        GetWindowLongPtrW(child, GWLP_USERDATA)) : NULL;
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

static void UpdateChrome() {
    // Folder controls are now owned by their individual MDI children.
    FolderBrowser* browser = ActiveBrowser();
    if (browser) browser->UpdateControls();
}

void FolderBrowser::OpenMenu(int index) {
    if (index < 0 || index > 3 || !menus_[index]) return;
    HMENU popup = CreatePopupMenu();
    if (!popup) return;
    switch (index) {
    case 0: // File
        AppendMenuW(popup, MF_STRING, IDM_NEW, L"New folder &window\tCtrl+N");
        AppendMenuW(popup, MF_STRING, IDM_CLOSE, L"&Close folder\tCtrl+W");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_EXIT, L"E&xit WindowExplorer");
        break;
    case 1: // Navigate
        AppendMenuW(popup, MF_STRING | (CanBack() ? 0 : MF_GRAYED),
            IDM_BACK, L"&Back\tAlt+Left");
        AppendMenuW(popup, MF_STRING | (CanForward() ? 0 : MF_GRAYED),
            IDM_FORWARD, L"&Forward\tAlt+Right");
        AppendMenuW(popup, MF_STRING | (CanUp() ? 0 : MF_GRAYED),
            IDM_UP, L"&Up\tAlt+Up");
        AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
        AppendMenuW(popup, MF_STRING, IDM_ADDRESS, L"&Address\tCtrl+L");
        break;
    case 2: // View
        AppendMenuW(popup, MF_STRING, IDM_REFRESH, L"&Refresh\tF5");
        break;
    case 3: // Window
        AppendMenuW(popup, MF_STRING, IDM_CASCADE, L"&Cascade");
        AppendMenuW(popup, MF_STRING, IDM_TILE_H, L"Tile &horizontally");
        AppendMenuW(popup, MF_STRING, IDM_TILE_V, L"Tile &vertically");
        break;
    }
    RECT anchor;
    GetWindowRect(menus_[index], &anchor);
    // Return the selected command rather than allowing a popup to dispatch
    // a WM_COMMAND to the enclosing frame. Post it to this exact child.
    const UINT selected = TrackPopupMenuEx(popup,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        anchor.left, anchor.bottom, child_, NULL);
    DestroyMenu(popup);
    if (selected) PostMessageW(child_, WM_COMMAND, MAKEWPARAM(selected, 0), 0);
}

static HRESULT NewFolderWindow(LPCITEMIDLIST location) {
    if (!g_mdi) return E_FAIL;
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
    case WM_SETFOCUS:
        if (browser && browser->View()) browser->Activate(true);
        return 0;
    case WM_MDIACTIVATE:
        if (browser) {
            const bool active = (reinterpret_cast<HWND>(lParam) == hwnd);
            browser->Activate(active);
            if (active) browser->UpdateControls();
        }
        if (g_frame) PostMessageW(g_frame, WM_UPDATE_CHROME, 0, 0);
        break;
    case WM_COMMAND: {
        if (!browser) break;
        const int command = LOWORD(wParam);
        if (command == IDC_ADDRESS && HIWORD(wParam) == EN_CHANGE) {
            browser->AddressChanged();
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (command) {
            case IDC_CHILD_MENU_FILE: browser->OpenMenu(0); return 0;
            case IDC_CHILD_MENU_NAVIGATE: browser->OpenMenu(1); return 0;
            case IDC_CHILD_MENU_VIEW: browser->OpenMenu(2); return 0;
            case IDC_CHILD_MENU_WINDOW: browser->OpenMenu(3); return 0;
            default: break;
            }
        }
        HRESULT hr = S_OK;
        switch (command) {
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
            if (FAILED(hr)) ShowFailure(hwnd, L"New folder window", hr);
            return 0;
        case IDM_CLOSE:
            SendMessageW(g_mdi, WM_MDIDESTROY,
                reinterpret_cast<WPARAM>(hwnd), 0);
            return 0;
        case IDM_REFRESH:
            if (browser->View()) browser->View()->Refresh();
            return 0;
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
            DestroyWindow(g_frame);
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

static HMENU MakeMenu() {
    // A minimal application-level menu is retained for Exit and the MDI
    // window list. The folder-specific command menus live in each child.
    HMENU menu = CreateMenu();
    HMENU application = CreatePopupMenu();
    HMENU windows = CreatePopupMenu();
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
        UpdateChrome();
        return 0;
    case WM_COMMAND: {
        const int command = LOWORD(wParam);
        if (command == IDM_EXIT) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (command >= IDM_NEW && command <= IDM_FORWARD) {
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
        if (g_mdi) SetFocus(g_mdi);
        return 0;
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
    common.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&common);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = FrameProc;
    wc.lpszClassName = kFrameClass;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_APPWORKSPACE + 1);
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
        if (g_accel && TranslateAcceleratorW(g_frame, g_accel, &msg)) continue;
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

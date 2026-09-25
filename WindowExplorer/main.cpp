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
    IDM_NEW = 1001, IDM_CLOSE = 1002, IDM_EXIT = 1003,
    IDM_BACK = 1004, IDM_UP = 1005, IDM_GO = 1006,
    IDM_REFRESH = 1007, IDM_CASCADE = 1008,
    IDM_TILE_H = 1009, IDM_TILE_V = 1010, IDM_ADDRESS = 1011,
    IDM_FIRST_CHILD = 30000,
    WM_UPDATE_CHROME = WM_APP + 1
};

static const wchar_t kFrameClass[] = L"WindowExplorer.Frame";
static const wchar_t kChildClass[] = L"WindowExplorer.Folder";
static HINSTANCE g_instance = NULL;
static HWND g_frame = NULL, g_mdi = NULL, g_address = NULL;
static HWND g_back = NULL, g_up = NULL, g_status = NULL;
static HACCEL g_accel = NULL;
static bool g_addressEditing = false;

class FolderBrowser;
static FolderBrowser* ActiveBrowser();
static HRESULT NewFolderWindow(LPCITEMIDLIST location);
static void UpdateChrome();
static void ShowFailure(HWND owner, const wchar_t* operation, HRESULT hr);

class FolderBrowser : public IShellBrowser {
public:
    explicit FolderBrowser(HWND child)
        : refs_(1), child_(child), viewWindow_(NULL), view_(NULL),
          folder_(NULL), pidl_(NULL), historyIndex_(-1),
          navigating_(false), closed_(false) {}

    virtual ~FolderBrowser() {
        Close();
    }

    HWND Child() const { return child_; }
    HWND ViewWindow() const { return viewWindow_; }
    IShellView* View() const { return view_; }
    bool CanBack() const { return historyIndex_ > 0; }
    bool CanUp() const { return pidl_ && pidl_->mkid.cb != 0; }
    LPCITEMIDLIST Location() const { return pidl_; }

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
            GetClientRect(child_, &rc);
            MoveWindow(viewWindow_, 0, 0, rc.right, rc.bottom, TRUE);
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
        if (SHGetPathFromIDListW(pidl_, path)) address = path;
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
            GetClientRect(child_, &rc);
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
            view_->UIActivate(active ?
                SVUIA_ACTIVATE_FOCUS : SVUIA_ACTIVATE_NOFOCUS);
            if (active) g_addressEditing = false;
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
            PostMessageW(g_frame, WM_UPDATE_CHROME, 0, 0);
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
        if (ActiveBrowser() == this && g_status)
            SendMessageW(g_status, SB_SETTEXTW, 0,
                reinterpret_cast<LPARAM>(message ? message : L""));
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
    FolderBrowser* browser = ActiveBrowser();
    EnableWindow(g_back, browser && browser->CanBack());
    EnableWindow(g_up, browser && browser->CanUp());

    if (browser && !g_addressEditing) {
        std::wstring address;
        browser->Address(address);
        SetWindowTextW(g_address, address.c_str());
    }
    if (browser && g_status) {
        std::wstring title;
        browser->DisplayName(title);
        SendMessageW(g_status, SB_SETTEXTW, 0,
            reinterpret_cast<LPARAM>(title.c_str()));
    } else if (g_status) {
        SendMessageW(g_status, SB_SETTEXTW, 0,
            reinterpret_cast<LPARAM>(L"No folder open"));
    }
    if (!browser && !g_addressEditing && g_address)
        SetWindowTextW(g_address, L"");
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
        return 0;
    case WM_SIZE:
        if (browser && wParam != SIZE_MINIMIZED) browser->Resize();
        break;
    case WM_SETFOCUS:
        if (browser && browser->View()) browser->Activate(true);
        return 0;
    case WM_MDIACTIVATE:
        if (browser) {
            const bool active = (reinterpret_cast<HWND>(lParam) == hwnd);
            browser->Activate(active);
            if (active) g_addressEditing = false;
        }
        if (g_frame) PostMessageW(g_frame, WM_UPDATE_CHROME, 0, 0);
        break;
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
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU navigate = CreatePopupMenu();
    HMENU window = CreatePopupMenu();

    AppendMenuW(file, MF_STRING, IDM_NEW, L"&New folder window\tCtrl+N");
    AppendMenuW(file, MF_STRING, IDM_CLOSE, L"&Close folder\tCtrl+W");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    AppendMenuW(navigate, MF_STRING, IDM_BACK, L"&Back\tAlt+Left");
    AppendMenuW(navigate, MF_STRING, IDM_UP, L"&Up\tAlt+Up");
    AppendMenuW(navigate, MF_STRING, IDM_ADDRESS, L"&Address\tCtrl+L");
    AppendMenuW(navigate, MF_STRING, IDM_REFRESH, L"&Refresh\tF5");
    AppendMenuW(window, MF_STRING, IDM_CASCADE, L"&Cascade");
    AppendMenuW(window, MF_STRING, IDM_TILE_H, L"Tile &horizontally");
    AppendMenuW(window, MF_STRING, IDM_TILE_V, L"Tile &vertically");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(navigate), L"&Navigate");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(window), L"&Window");
    return menu;
}

static void LayoutFrame(HWND frame) {
    if (!g_mdi) return;
    RECT r;
    GetClientRect(frame, &r);
    const int width = r.right - r.left;
    const int height = r.bottom - r.top;
    const int top = 34;
    const int bottom = 23;
    MoveWindow(g_back, 5, 5, 54, 24, TRUE);
    MoveWindow(g_up, 63, 5, 44, 24, TRUE);
    int addressWidth = width - 173;
    if (addressWidth < 30) addressWidth = 30;
    MoveWindow(g_address, 112, 5, addressWidth, 24, TRUE);
    MoveWindow(GetDlgItem(frame, IDC_GO), width - 56, 5, 51, 24, TRUE);
    MoveWindow(g_status, 0, height - bottom, width, bottom, TRUE);
    MoveWindow(g_mdi, 0, top, width,
        height > top + bottom ? height - top - bottom : 0, TRUE);
}

static void GoFromAddress() {
    FolderBrowser* browser = ActiveBrowser();
    if (!browser) return;
    int size = GetWindowTextLengthW(g_address);
    std::vector<wchar_t> text(static_cast<size_t>(size) + 1);
    GetWindowTextW(g_address, &text[0], size + 1);
    g_addressEditing = false;
    HRESULT hr = browser->Go(&text[0]);
    if (FAILED(hr)) ShowFailure(g_frame, L"Open folder", hr);
    UpdateChrome();
}

static LRESULT CALLBACK FrameProc(HWND hwnd, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        SetMenu(hwnd, MakeMenu());
        CLIENTCREATESTRUCT create;
        create.hWindowMenu = GetSubMenu(GetMenu(hwnd), 2);
        create.idFirstChild = IDM_FIRST_CHILD;
        g_mdi = CreateWindowExW(WS_EX_CLIENTEDGE, L"MDICLIENT", NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
            WS_HSCROLL | WS_VSCROLL | MDIS_ALLCHILDSTYLES,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_MDI),
            g_instance, &create);
        g_back = CreateWindowW(L"BUTTON", L"Back", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_BACK),
            g_instance, NULL);
        g_up = CreateWindowW(L"BUTTON", L"Up", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_UP),
            g_instance, NULL);
        g_address = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_ADDRESS),
            g_instance, NULL);
        CreateWindowW(L"BUTTON", L"Go", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_GO),
            g_instance, NULL);
        g_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_STATUS),
            g_instance, NULL);
        if (!g_mdi || !g_back || !g_up || !g_address || !g_status ||
            !GetDlgItem(hwnd, IDC_GO)) return -1;
        EnableWindow(g_back, FALSE);
        EnableWindow(g_up, FALSE);
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
        if (command == IDC_ADDRESS && HIWORD(wParam) == EN_CHANGE) {
            if (GetFocus() == g_address) g_addressEditing = true;
            return 0;
        }
        FolderBrowser* browser = ActiveBrowser();
        HRESULT hr = S_OK;
        switch (command) {
        case IDC_GO:
        case IDM_GO: GoFromAddress(); return 0;
        case IDM_ADDRESS:
            if (g_address) {
                SetFocus(g_address);
                SendMessageW(g_address, EM_SETSEL, 0, -1);
            }
            return 0;
        case IDC_BACK:
        case IDM_BACK:
            if (browser) hr = browser->Back();
            if (FAILED(hr)) ShowFailure(hwnd, L"Back", hr);
            return 0;
        case IDC_UP:
        case IDM_UP:
            if (browser) hr = browser->Up();
            if (FAILED(hr)) ShowFailure(hwnd, L"Parent folder", hr);
            return 0;
        case IDM_NEW:
            hr = NewFolderWindow(browser ? browser->Location() : NULL);
            if (FAILED(hr)) ShowFailure(hwnd, L"New folder window", hr);
            return 0;
        case IDM_CLOSE:
            if (browser) SendMessageW(g_mdi, WM_MDIDESTROY,
                reinterpret_cast<WPARAM>(browser->Child()), 0);
            return 0;
        case IDM_REFRESH:
            if (browser && browser->View()) browser->View()->Refresh();
            return 0;
        case IDM_CASCADE:
            SendMessageW(g_mdi, WM_MDICASCADE, 0, 0); return 0;
        case IDM_TILE_H:
            SendMessageW(g_mdi, WM_MDITILE, MDITILE_HORIZONTAL, 0); return 0;
        case IDM_TILE_V:
            SendMessageW(g_mdi, WM_MDITILE, MDITILE_VERTICAL, 0); return 0;
        case IDM_EXIT: DestroyWindow(hwnd); return 0;
        default: break;
        }
        break;
    }
    case WM_SETFOCUS:
        if (g_mdi) SetFocus(g_mdi);
        return 0;
    case WM_DESTROY:
        g_mdi = NULL;
        g_address = NULL;
        g_back = NULL;
        g_up = NULL;
        g_status = NULL;
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
        { FVIRTKEY | FALT, VK_UP, IDM_UP },
        { FVIRTKEY, VK_F5, IDM_REFRESH }
    };
    g_accel = CreateAcceleratorTableW(keys, sizeof(keys) / sizeof(keys[0]));
    g_frame = CreateWindowExW(0, kFrameClass, L"WindowExplorer",
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
        // Give the address edit its normal typing behavior. Enter navigates.
        if (g_address && GetFocus() == g_address &&
            msg.hwnd == g_address && msg.message == WM_KEYDOWN &&
            msg.wParam == VK_RETURN) {
            GoFromAddress();
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

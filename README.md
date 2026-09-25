# WindowExplorer

Native Win32 **MDI host for Windows Shell folder views**, intended first for Windows XP
x86/x64. Developed in Visual Studio 2022 using the XP-capable v141_xp toolset.
The solution also defines W10/v143 builds for a subsequent Windows 10 compatibility
check.

## About Windows and Select All

The **main MDI frame** now has an ordinary Win32 **Help > About Windows...**
menu item, separate from the folder-specific Help > About WindowExplorer.
It opens a modal, XP-inspired About dialog with a four-colour Windows-style
logo/banner and a short fictional tale instead of operating-system version,
licensing and memory information. The dialog is hand-drawn using Win32 GDI;
no XP system resources, bitmaps, or additional project dependencies are needed.

**Edit > Select All** now searches recursively for the native Shell
`SysListView32` under each folder's `IShellView` before selecting its
items. The earlier implementation only searched one level deep, which
missed the actual file list on XP even though Ctrl+A worked.

This revision requires a clean XP GUI test; it has been committed but
has not been compiled or tested by the assistant. Close all surviving
WindowExplorer processes before launching the updated executable.

## Native Shell view, child menus and toolbar

WindowExplorer hosts the OS's real `IShellView` per MDI child. The XP task
pane, native file list, contextual file commands and file icons come from
the Windows Shell. The application does not reparent `explorer.exe`.

**Menu presentation:** Standard Win32 MDI child windows cannot own an
ordinary HMENU menu bar. The per-child File / Edit / View / Favorites /
Tools / Help / Navigate / Window row is therefore now a flat, painted
menu strip that opens **actual Win32 popup menus**; it replaces the old
push-button menu row. Mouse interaction, Left/Right/Down/Enter on the
focused strip and Alt+F/E/V/O/T/H/N/W open its menus. The top-level frame
still owns the normal Application / Windows menu and MDI window list.

Each child toolbar now contains only Back, Forward, Up, Folders,
New Window and Refresh. The redundant Views/Favorites toolbar buttons
have been removed; their commands remain in the per-child menu strip.

### Edit menu

Edit exposes Undo, Cut, Copy, Paste, Paste Shortcut, Copy To Folder,
Move To Folder, Select All, Invert Selection, Delete, Rename and Properties.
Commands use the active child’s native `IShellView` selection and
`IContextMenu` canonical verbs where available, with a best-effort
fallback to older XP Shell-view WM_COMMAND identifiers where the
corresponding verb is not exposed. These private Shell identifiers are
not a documented, cross-version API; particular commands may need
additional testing on XP and W10.

Copy To Folder and Move To Folder obtain the Shell selection as
`CF_HDROP`, prompt for a filesystem destination using the XP folder
picker, then invoke `SHFileOperationW`. They currently support
filesystem items, not arbitrary virtual Shell objects. File operations
are real operations on disk: use disposable test files until verified.

The address field retains its ordinary text-edit shortcuts; selecting
Ctrl+A/C/X/V/Z inside it will not execute file operations.

### Shutdown lifecycle

Closing the application first marks the frame as shutting down and
destroys every open MDI child and its Shell view before destroying the
main frame and uninitializing OLE. The code suppresses Shell activation,
tree navigation and toolbar updates during this teardown. This targets
the reported XP process-left-running problem; the new exit sequence
still requires direct testing on XP before we can treat it as fixed.

### Persistent global display defaults

View mode and visibility of the tree, toolbar, address and status are
global to this application: changing them in any child updates all
children and persists across application restarts. Favorites persist
per Windows user as well. These preferences are stored under
`HKEY_CURRENT_USER\\Software\\Magneticon\\WindowExplorer`; XP and W10
have distinct user registries.

XP native grouping selected within the Shell view is *not* yet
broadcast or persisted; `IFolderView2` grouping is unavailable on XP.
WindowExplorer's global view defaults govern only the commands that
WindowExplorer itself implements.

## Building (VS2022)

Open the checked-in **WindowExplorer.sln** (not SLNX) in Visual Studio 2022. It
contains a native C++ GUI EXE. The external build workflow selects the custom
MSBuild property AtaTargetOS; the project defaults it to WXP if unspecified.

| Target | Toolset | Output directory |
| --- | --- | --- |
| XP x64 | v141_xp | bin\Release\WXP\x64\ |
| XP x86 | v141_xp | bin\Release\WXP\x86\ |
| Windows 10 x64 | v143 | bin\Release\W10\x64\ |
| Windows 10 x86 | v143 | bin\Release\W10\x86\ |

The x86 solution platform maps to native Win32. Intermediate directories are
separated by OS, architecture, and configuration. Windows 10 builds are a
separate target, not a claim that runtime behavior has been validated on W10.
No .NET Framework, CUDA, helper build BAT, or externally installed Explorer
process is required by the application.

## Manual GUI acceptance checks

Close **all** running WindowExplorer processes on XP before launching
the rebuilt executable. W10 `E:\\GPT\\CODEX\\CODEX\\GIT\\WindowExplorer`
and XP `Y:\\GPT\\CODEX\\CODEX\\GIT\\WindowExplorer` represent the
**same shared directory** under different drive letters, not separate
checkouts.

1. Check that the child menu row looks like a flat menu rather than
   push buttons; open each popup by mouse, then via Alt+F/E/V.
2. Verify the child toolbar has Back, Forward, Up, Folders, New and
   Refresh, but no Views or Favorites buttons.
3. Select disposable files in the native Shell view; test each Edit
   command, including clipboard cut/copy/paste, Paste Shortcut,
   Undo, Select All, Invert Selection, Delete, Rename and Properties.
4. Test Copy To Folder / Move To Folder using **only expendable files**
   and check cancellation, file conflicts, and destination selection.
5. Open two MDI children and verify an Edit command affects the child
   whose menu was used. Test address-bar Ctrl+A/C/X/V/Z separately.
6. Test persistence of global display defaults after restart.
7. Close the application with two child windows open. Confirm in XP
   Task Manager that `WindowExplorer.exe` disappears from Processes.
   Repeat by closing via the main title-bar X and Application > Exit.
   If it still remains, report whether any modal Shell dialog was open
   and capture a dump/stack before terminating the stuck instance.
8. Repeat on W10 and report compatibility differences.

This remains a GUI manual-test project without an unattended AI_RUN
script. The repository does not track generated `bin/` executables.

## Technical notes and scope

- OLE is initialized in a single-threaded apartment on the GUI thread.
- Each child owns an IShellBrowser implementation and an independent folder PIDL,
  Shell view, and navigation history. Local paths are parsed by the Desktop
  IShellFolder; My Computer is obtained with CSIDL_DRIVES.
- Supported Shell namespace extensions must match the bitness of the host process.
  x64 WindowExplorer does not load in-process x86-only Shell extensions.
- The folder view, not a reparented standalone Explorer frame, is the integration
  boundary. Native shell extensions execute in the WindowExplorer process, so
  an unstable extension may crash the application.
- This initial version uses basic application menus instead of merging the Shell
  view's complete command menu; some Explorer commands, preview panes, or
  version-specific features may require additional browser interfaces.

## Running the latest executable

The main window is titled **WindowExplorer**. Its main menu reads
**Application / Windows**, while every MDI child has **File / Navigate /
View / Window** and independent Back / Forward / Up / address / Go controls.

Before manually testing a rebuilt EXE, **exit every running WindowExplorer
instance on the target system**. A previously launched process continues to
execute its old program image even if a newer executable replaces the file
on disk. The initial apparent XP/Windows 10 interface discrepancy was caused
by surviving old processes on XP, not by a separate XP interface.

Build diagnostics still identify the project, source and output paths without
printing a temporary version string in the title bar. XP x64 Release runs from
`bin\Release\WXP\x64\WindowExplorer.exe`; the repository's `bin/` directory
is ignored by Git.

## Current development status

The earlier Shell-view, child navigation and tree versions were
demonstrated on XP x64. This **menu/edit/shutdown update is committed
but has not yet been compiled or runtime-tested on XP or W10**.
Some Edit operations rely on XP Shell behavior and require verification.

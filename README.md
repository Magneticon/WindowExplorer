# WindowExplorer

Native Win32 **MDI host for Windows Shell folder views**, intended first for Windows XP
x86/x64. Developed in Visual Studio 2022 using the XP-capable v141_xp toolset.
The solution also defines W10/v143 builds for a subsequent Windows 10 compatibility
check.

## Shell integration and MDI layout

The application hosts the operating system's actual `IShellView` inside
each MDI child; no existing `explorer.exe` process is reparented or captured.
XP's task pane, native file-list rendering, file icons, and item context
menus are provided by the Shell view itself.

Each MDI child now also has a native Win32 Shell-namespace folder tree,
using `IShellFolder::EnumObjects`, absolute PIDLs, and the Shell's shared
system image list. Expanding folders loads their children on demand;
clicking a tree folder opens it in the corresponding MDI child's view.
The tree follows supported folder navigation and can be toggled with
**Folders** or **Ctrl+F** without affecting other MDI children.

Per-child File / Edit / View / Favorites / Tools / Help / Navigate / Window
popup menus supplement Back / Forward / Up / Folders toolbar buttons,
an address field, Go button and a status line. View offers Icons, List,
Details and Refresh. Edit offers Select All. Favorites supports adding
filesystem folders and opening them within the same application session.
Each MDI child maintains independent history, folder location and tree.

This is an **XP-style first pass**, not a byte-for-byte clone of Explorer's
menu bar or icon toolbar. In particular, it does not yet merge all of the
original Explorer File/Edit/Tools commands, persistent Favorites, native
Explorer search UI, or the complete Explorer bands. The Shell folder view
remains the source of file selection, native item context menus and
operating-system-dependent folder presentation. The tree and Shell task
pane may both be present, since XP itself renders the task pane inside
the Shell view.

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

## First manual GUI test (WXP x64, then W10)

Close **all** running instances of WindowExplorer on the target machine
before launching the rebuilt executable. A surviving process continues
displaying the old interface even after the EXE on disk is replaced.

1. Open a new child and verify the left-hand Desktop/My Computer shell
   folder tree appears alongside the native folder view and XP task pane.
2. Expand Desktop, My Computer, drives and nested directories in the tree.
   Click a tree node and verify only that child's folder view navigates.
3. Navigate through the native view or address field; confirm the tree
   selects the nearest matching folder and Back/Forward history still works.
4. Hide/show the Folders pane (Ctrl+F). Check resize and maximize of child
   windows and the view/status/toolbar layout.
5. Test child File, Edit (Select All), View (Icons/List/Details/Refresh),
   Favorites, Tools, Help and Window menus. Favorites are session-only.
6. Open a second MDI child, browse elsewhere, switch children and verify
   that each tree, address and navigation history remains independent.
7. Check native folder context menus and test copy/paste/drag/drop only with
   disposable files. Close both children and exit without a hanging process.
8. Repeat on Windows 10 and record any Shell-view or tree differences.

This is a human-operated GUI program, so an unattended AI_RUN script is not
required. A successful build is not proof of XP runtime behavior. The
repository deliberately does not track generated `bin/` executables.

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

The prior MDI Shell-view/navigation build has been observed working on XP x64
and W10. The **new folder-tree and expanded-menu revision has not yet been
built or tested on those systems**. Report compiler diagnostics and XP GUI
screenshots before treating this milestone as verified.

# WindowExplorer

Native Win32 **MDI host for Windows Shell folder views**, intended first for Windows XP
x86/x64. Developed in Visual Studio 2022 using the XP-capable v141_xp toolset.
The solution also defines W10/v143 builds for a subsequent Windows 10 compatibility
check.

## Explorer MDI layout and persistent global display defaults

The application hosts the OS's native `IShellView` in every MDI child; it
does not capture or reparent an `explorer.exe` window. XP's classic task pane,
native item view and item context menus belong to the Shell. The folder tree,
address bar, toolbar and application menus are WindowExplorer controls.

The new toolbar in each child contains **Back, Forward, Up, Folders,
New (window), Refresh, Views and Favorites**. The child View menu offers
**Thumbnails, Tiles, Large Icons, List and Details**, plus visibility toggles
for the toolbar, address bar, status bar and folder tree. The view-mode
and pane settings are **global to WindowExplorer**: changing them in one
child updates all existing children, supplies defaults for new children,
and survives application restarts. Ctrl+L re-enables and focuses the
address bar if it was hidden.

Preferences and filesystem Favorites are stored in
`HKEY_CURRENT_USER\\Software\\Magneticon\\WindowExplorer` using
Windows XP-compatible registry APIs. Each Windows user account (and
each separate XP/W10 installation) has its own preferences. The program
does not modify the normal Windows Explorer's system-wide folder settings.

**Important XP limitation:** Selecting "Arrange Icons by" / "Show in Groups"
in the native Shell view is not yet captured or broadcast to other MDI
children. The documented `IFolderView2::SetGroupBy` interface is available
only on Vista and later. WindowExplorer does **not** claim to persist or
synchronize XP's native grouping in this revision. The global display
defaults apply to commands chosen from WindowExplorer's View menu; external
changes made directly inside the embedded Shell view are not monitored.

Every child retains its own folder location, Back/Forward history and
Shell-tree selection. The status, toolbar and address visibility are
global defaults, not folder-specific preferences.

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

Before launching the rebuilt executable, close all existing WindowExplorer
processes on the target machine (including any old, hanging XP instances).
The build logs and binaries on the two machines are reached through
`E:\\GPT\\CODEX\\CODEX\\GIT\\WindowExplorer` (W10) and
`Y:\\GPT\\CODEX\\CODEX\\GIT\\WindowExplorer` (XP); these are
different drive letters for the **same shared directory**, not distinct
checkouts. GUI operation requires manual verification on XP.

1. On XP x64, open two MDI folder windows. Change View > Details to Tiles or
   Thumbnails in the first child. Verify the second existing child changes,
   and a subsequently created child starts with the same mode.
2. Toggle the folders pane, toolbar, address bar, and status bar in View.
   Verify that all open windows update without hiding the embedded file view.
   With address hidden, Ctrl+L should restore and focus it.
3. Test New, Refresh, Views, Favorites and the existing navigation controls
   on the per-child toolbar. Verify the expanded toolbar remains inside each
   MDI child and does not cover the XP task pane or file list.
4. Add a filesystem folder to Favorites. Exit **all** WindowExplorer
   processes, restart and confirm that Favorites and display defaults
   return. Check the other MDI child retains independent navigation history.
5. Use the native Shell's Show in Groups command: note that this is **not**
   synchronized or persisted by WindowExplorer yet; inspect for regressions.
6. Repeat on W10, then report any XP-specific compilation, startup,
   view-mode or UI issues with screenshots/logs.

This GUI build has no unattended `AI_RUN` script. A successful VS2022 build
does not prove that all XP Shell integration paths work correctly.

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

Previous MDI Shell-view and folder-tree builds were shown operating on XP x64.
This global-defaults and expanded-toolbar revision has been committed but
**has not yet been built or exercised on XP or W10**. Grouping support is a
separate, pending XP Shell integration task.

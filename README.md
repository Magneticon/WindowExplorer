# WindowExplorer

Native Win32 **MDI host for Windows Shell folder views**, intended first for Windows XP
x86/x64. Developed in Visual Studio 2022 using the XP-capable v141_xp toolset.
The solution also defines W10/v143 builds for a subsequent Windows 10 compatibility
check.

## What this first milestone actually embeds

Each MDI child instantiates the operating system's own IShellFolder/IShellView
implementation. The file-list UI belongs to the Windows Shell and normally brings
the Shell's native file icons, selection, file context menus, and drag/drop behavior.
This is not a new file list pretending to be Explorer, and it does **not** use
SetParent to hijack an explorer.exe window across processes.

Explorer's entire outer frame (navigation tree, command bars, address bar,
and application menu) is **not** embedded. Windows XP's familiar task pane
is drawn by the native Shell view itself, which you may see inside each child.
WindowExplorer supplies an independent File/Navigate/View/Window popup-menu
strip, Back/Forward/Up buttons, address field, Go button and status area in
**every MDI child**. The top-level frame retains only an Application menu and
MDI Windows list.

The child menus expose the commands currently implemented by WindowExplorer;
they do not clone Explorer's complete File/Edit/View/Favorites/Tools/Help menus,
Shell extension menu merging, navigation tree, or original toolbar icons.
Native per-file context menus still come from the Shell. Additional Shell
browser services would be needed for closer Explorer UI parity.

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

## First manual GUI test

Launch bin\Release\WXP\x64\WindowExplorer.exe on Windows XP x64 after the
XP build succeeds (or choose the corresponding OS/architecture output above).

1. Verify a My Computer Shell view appears *inside* the initial MDI child.
2. Double-click a drive and then a folder: the active child should browse in
   place, with an updated caption and address.
3. Right-click a file and check that the standard Shell context menu appears.
   Check selection, copy/paste and drag/drop with test files, not valuable files.
4. In each MDI child, verify the File/Navigate/View/Window popup strips,
   Back, Forward, Up, address, Go, and status are **inside that child**.
5. Enter an existing directory in child A's address field and press Enter;
   browse to a second folder, then test Back and Forward (Alt+Left/Right)
   and Up (Alt+Up). Check View > Refresh (F5).
6. Open another folder window (Ctrl+N), navigate child B somewhere different,
   and verify that B's address and history are independent of A's. Switch
   windows and verify each child's popup menus control the child clicked.
7. Use Window > Cascade and Tile, resize children, and confirm that neither
   the native task pane nor the file list covers child navigation controls.
8. Close one child, then close the application and check for crashes.
9. Repeat on W10 only after verifying the XP target; record Shell differences.

This is a human-operated GUI project; no unattended AI_RUN script is supplied.
The former AI_RUN_WXP.bat was a Hello World/DBGRun placeholder rather than a
real test and was removed in accordance with the provided VS2022 project guide.
A successful build does not establish that the Shell view actually works on XP.

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

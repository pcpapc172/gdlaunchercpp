# GDLauncher (C++ / Qt6)

A native C++/Qt6 port of [gdlauncher](https://github.com/pcpapc172/gdlauncher) (the
Electron-based Geometry Dash instance launcher). Same feature set, a fraction of the
footprint: the built executable is under 1 MB (vs. an Electron app's ~150-300 MB
Chromium/Node bundle), and RAM usage drops from Chromium-per-window levels to a
normal native Qt app.

## What was ported

Every function from the original `main.js`/`editor.js`/`renderer.js` has a direct
C++ counterpart:

| Original (Electron/Node) | Port |
|---|---|
| `main.js` instance CRUD, trash, managed-file sync | `InstanceManager` |
| `main.js` version scan/download/extract (`easydl` + `decompress`) | `VersionManager` (`QNetworkAccessManager` + vendored `miniz`) |
| `main.js` `launchGame`/restart-detection/Wine handling | `GameLauncher` |
| `main.js` named-pipe Geode log server (`net.createServer`) | `LogPipeServer` (`QLocalServer`, cross-platform) |
| `main.js` self-updater (GitHub releases, Fedora rpm-ostree/dnf) | `UpdateChecker` |
| `editor.js` `GDParser` (custom plist-ish XML parser/builder) | `gd/GDParser` (byte-for-byte logic port, incl. insertion-order dict) |
| `editor.js` `GDCrypto` (XOR + url-safe base64 + gzip) | `gd/GDCrypto` (zlib gzip streams) |
| `editor.js` session functions (levels, song, description, import/export, XML) | `gd/SaveEditor` |
| `index.html` + `renderer.js` UI | `MainWindow` + `ui/*Dialog` (QtWidgets) |
| `console.html` | `ConsoleWindow` |

## Notable differences from the Electron version

- The Monaco-based raw XML editor is a plain `QPlainTextEdit` here (no syntax
  highlighting) to avoid bundling a JS editor engine — keeps the "lower space"
  goal intact.
- Zip extraction uses the vendored single-purpose `miniz` library instead of
  shelling out or bundling a Node zip package.
- Downloads use Qt's networking stack with a single connection instead of
  `easydl`'s multi-connection segmented downloader; still resumable-friendly
  and much simpler to maintain.
- The first-run onboarding tour and changelog dialog were simplified to a
  single "what's new" message box; the core first-run save-data import flow
  (the part that actually matters) is fully ported.

## Building

Requires Qt 6 (Core, Widgets, Network) and zlib development headers.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/gdlauncher
```

On Debian/Ubuntu: `apt install qt6-base-dev libqt6svg6 zlib1g-dev build-essential cmake`.

## Launching and updates

The launcher tracks its own game process family, so another running copy with
the same executable name does not prevent save sync. Save copying and sync-back
run in a worker thread, with the current filename, file count, and percentage
shown live in the main window.

On Linux, **Launch executable** runs a chosen Windows executable and **Open
Explorer** opens Windows Explorer, using the Wine or GE-Proton backend selected
in Settings and its corresponding prefix. These tools do not swap instance saves.
Close the tool before launching an instance.

**NONG Downloader** browses SongFileHub's catalogue, searches by song/level name,
ID, or creator, and downloads songs as `[ID].mp3` or `[ID].ogg`. Downloads show
progress and continue if the menu is closed. The folder defaults to
`%LOCALAPPDATA%/GeometryDash`; enter `%LOCALAPPDATA%/custom` or use **Browse** to
choose another folder. On Linux, `%LOCALAPPDATA%` refers to the selected Wine or
GE-Proton prefix. The selected folder is remembered.

When a song already exists, **Replace** renames it to `[ID].mp3.original` or
`[ID].ogg.original` before installing the completed download. Further replacements
keep that first backup, including when switching audio formats. **Restore
original** restores the backup for that song and folder. Failed or cancelled
downloads leave the current song intact. Both download menus display fetch and
download errors and offer a refresh/retry action. Window fades and progress-bar
animations have been removed.

Updates automatically detect whether this executable belongs to a Debian/RPM
package, a Windows NSIS/MSI installation, or a portable copy. Settings shows the
detected type. Its **Source code** button opens this repository, using a GitHub
SVG icon with contrasting colors in light and dark themes. The icon is from
[Simple Icons](https://github.com/simple-icons/simple-icons) (CC0).

To run the Linux regression checks:

```sh
cmake -S . -B build -DGDLAUNCHER_BUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## GE-Proton on Linux

In Settings, enable **Download GE-Proton and launch with UMULauncher**. On the
next launch, GDLauncher downloads the latest official GE-Proton release for your
CPU and the portable UMU launcher. Download and unpacking progress appears in the
main window's progress bar; later launches reuse the installed runtime.

GE-Proton and UMU are stored under `~/.config/gdlauncher/Runtimes`, with a separate
prefix in `~/.config/gdlauncher/ProtonPrefix`. Instance saves sync into that prefix
and back after the game exits. Geode-compatible instances also receive the
`WINEDLLOVERRIDES=xinput1_4=n,b` override. Disabling the option returns to Wine.

This option requires Python 3.10 or newer and tar, plus working graphics/Vulkan drivers.
[UMU](https://github.com/Open-Wine-Components/umu-launcher) runs Proton outside
Steam and automatically prepares the Steam Linux Runtime on first launch, which
can take additional time. That preparation uses an indeterminate progress bar.
Performance improvements depend on the game and system.

## Layout

```
src/
  MainWindow.*        # main window (instance table + actions)
  Settings.*           # launcher_settings.json load/save, data dirs
  InstanceManager.*     # instance CRUD, managed-file transfer/trash
  VersionManager.*      # local/remote version listing, download+extract
  GameLauncher.*         # launch/monitor/sync/Wine handling
  UpdateChecker.*        # GitHub release self-update
  LogPipeServer.*        # Geode advanced-logging named pipe
  ConsoleWindow.*         # standalone log window
  gd/
    GDParser.*            # GD's plist-like XML parse/build
    GDCrypto.*             # GD's save-file XOR/base64/gzip scheme
    SaveEditor.*            # save-editor session logic
  ui/
    InstanceDialog.*        # create/edit instance modal
    SettingsDialog.*         # settings modal
    DownloadsDialog.*         # version manager modal
    SaveEditorDialog.*         # save editor modal
third_party/miniz/           # vendored zip read/write (public domain)
```

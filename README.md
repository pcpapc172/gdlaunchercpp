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

On Debian/Ubuntu: `apt install qt6-base-dev zlib1g-dev build-essential cmake`.

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

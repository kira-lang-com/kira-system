# Kira System

The operating system, as Kira packages.

Everything here answers a question the C standard library cannot: what the
platform's own file dialog returns, what is on the clipboard, where the user's
documents live. Each namespace is its own package so a program links only the
system surface it actually uses — a build that never opens a dialog does not
carry a COM apartment or an AppKit framework on its link line.

| Package | Module | What it is |
|---|---|---|
| [`FileSystem/`](FileSystem) | `FileSystem` | The platform's file/folder chooser. |

```kira
import FileSystem

let folder = FileSystem.pickFolder()
if folder.count > 0 {
    // the user chose it; "" means they cancelled
}
```

## Why a library and not a widget

A file chooser drawn by an application is a chooser that knows nothing: not the
user's sidebar, not their tags or recents, not the network volumes their file
manager mounted, not the keyboard shortcuts they type without thinking. Every
platform already ships the real one, and it is the only one the user has
learned. Kira applications ask for it.

## Layout

A package per system namespace, each with its own native shim under
`NativeLibs/` and its Kira surface under `app/`. Package name, module root and
dependency name agree, which is what lets a consumer write
`FileSystem.pickFolder()` after depending on it:

```kira
Dependency { name: "FileSystem", path: "../kira-system/FileSystem" }
```

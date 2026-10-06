# Lectern on Windows

## Supported systems

| OS | Support |
|---|---|
| **Windows 11** (64-bit) | Supported |
| **Windows 10** version **1903+** (64-bit) | Supported |
| Windows 8.1 and older | Not supported (Qt 6.8 requirement) |
| 32-bit Windows | Not supported |

One **64-bit** build (`Lectern.exe` + Qt DLLs) covers all supported Windows 10/11 versions.

## Build on your PC

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with **Desktop development with C++** and **Windows 10/11 SDK**.
2. Install [Qt 6.8+](https://www.qt.io/download) (MSVC 2022 64-bit).
3. Clone [vcpkg](https://github.com/microsoft/vcpkg), run `bootstrap-vcpkg.bat`, set `VCPKG_ROOT`.
4. Set `Qt6_DIR` to Qt’s `lib/cmake/Qt6` folder.

From the repo root in **PowerShell**:

```powershell
$env:VCPKG_ROOT = "C:\vcpkg"
$env:Qt6_DIR = "C:\Qt\6.8.2\msvc2022_64\lib\cmake\Qt6"
.\scripts\build-windows.ps1
```

Output:

- **`dist\Lectern\Lectern.exe`** — portable folder (copy to any supported PC)
- **`build\win\package\Lectern-0.1.0-win64.zip`** — same contents, ready to share

Or with CMake presets (on Windows):

```powershell
cmake --preset win-release
cmake --build --preset win-release --target lectern
cmake --install build/win-release --prefix dist/Lectern --component app
cmake --build build/win-release --target package
```

## CI build

Pushes to `main` run [.github/workflows/windows-app.yml](../.github/workflows/windows-app.yml).
Download the **Lectern-windows-x64** artifact from GitHub Actions.

## Permissions

Windows does not show macOS-style permission dialogs. Enable **Microphone** and **Camera** under  
**Settings → Privacy & security** if recording fails.

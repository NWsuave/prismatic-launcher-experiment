# Windows x64 portable package

The package contains `PrismaticLauncher/bin/prismatic-launch.exe`, release Qt DLLs,
the `platforms/qwindows.dll` plugin, LibArchive and its transitive DLL dependencies,
and application-local MSVC redistributable DLLs. Extract the complete ZIP before
running the executable. End users need neither Visual Studio nor Qt installed.
Profiles and preferences use the launcher's per-user Qt standard data location;
the ZIP is portable application code, while user data remains outside its directory.

## Native build and package

Use Windows 10/11 x64, CMake 3.28+, Ninja, PowerShell 7, an MSVC 2022 x64 developer
shell, Qt 6.8.3 MSVC x64, and LibArchive built using the repository's pinned vcpkg
registry. Use matching release SDK libraries. The workflow in
`.github/workflows/package-windows.yml` provisions these SDKs separately: Qt's
official binary distribution and vcpkg's `libarchive:x64-windows`. Its vcpkg checkout
matches `vcpkg-configuration.json`; building Qt again through vcpkg is unnecessary.

After installing the prerequisites, run from the repository root:

```powershell
$qtSdk = 'C:/Qt/6.8.3/msvc2022_64'
$archiveSdk = "$PWD/build/windows-dependencies/x64-windows"
$env:PATH = "$qtSdk/bin;$archiveSdk/bin;$env:PATH"
cmake -S . -B build/windows-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DCMAKE_PREFIX_PATH=$qtSdk;$archiveSdk"
./scripts/package-windows.ps1 -BuildDir build/windows-release -QtBinDir "$qtSdk/bin" -RuntimeDirectories "$archiveSdk/bin"
```

The equivalent explicit CMake target is `cmake --build build/windows-release
--target package-windows`. The script accepts an already configured build directory
inside this worktree's `build/` directory. It always rebuilds and runs CTest before
staging, and fails if no tests exist or any test fails. Do not invoke it concurrently
against the same build tree.

Artifacts are `build/artifacts/windows/PrismaticLauncher-<version>-windows-x64.zip`
and the adjacent `.zip.sha256`. The staging tree remains at
`build/windows-release/windows-package-stage`. The ZIP uses sorted entries and fixed
timestamps (2000-01-01 by default, or `SOURCE_DATE_EPOCH`) for reproducible archive
metadata. Binary reproducibility additionally requires the same compiler, SDKs,
source tree and build flags. A `runtime-manifest.json` records staged file hashes.

## Deployment and verification

`windeployqt --release --no-compiler-runtime` collects the Qt runtime and plugins.
The script explicitly copies x64 CRT DLLs from the licensed
`VCToolsRedistDir/x64/Microsoft.VC*.CRT` directory. It never redistributes arbitrary
CRT copies from Windows or the compiler's tool directory. Distribution remains
subject to the Visual Studio redistributable license. Application-local CRT
deployment requires shipping runtime security updates with application updates;
Microsoft recommends central redistributable deployment for automatic servicing.

CMake scans the executable and every deployed DLL with `dumpbin`, copies additional
non-system dependencies, rejects conflicting DLL contents and unresolved imports,
then verifies that all non-system dependencies resolve inside the package. All
executables and DLLs must have x64 PE headers. The packaged executable's `--help`
and `--version` are run with SDK entries removed from `PATH` and Qt plugin path
overrides removed. These checks load Qt's real Windows platform plugin; they do not
launch Stardew Valley, SMAPI or mod code.

The workflow uploads build artifacts only and has read-only repository permissions.
It never creates or publishes a release. A clean Windows VM without Qt/Visual Studio
remains necessary for a full interactive distribution acceptance test. No Windows
build or artifact was produced in this Linux development environment.

The package copies available Qt license directories and vcpkg dependency copyright
files. Before distributing externally, verify that your chosen Qt license and all
dependency notice/source obligations are satisfied; SDK distributions differ in
which notices they include. Code signing is optional for development ZIPs. Public
distribution should use an appropriate Authenticode certificate and timestamp.

Implementation references: [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html),
[CMake runtime dependency scanning](https://cmake.org/cmake/help/latest/command/file.html#get-runtime-dependencies),
and [Microsoft runtime redistribution](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170).

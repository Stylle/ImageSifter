# Windows build and release

The supported publication target is Windows x64. Use Qt6.8.3 `win64_mingw`, MinGW13.1, CMake and Ninja. Install Qt base, svg and imageformats. Both static sifter_core and a separate LGPL LibRaw dynamic library are built; there is no Docker workflow.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$env:QT_ROOT_DIR
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./scripts/package.ps1 -QtRoot $env:QT_ROOT_DIR -BuildDir build -DistDir dist
```

The package script refuses to clear an existing output folder. It deploys Qt plugins and the compiler runtime, adds licenses, verifies the packaged executable with a sanitized PATH, creates portable ZIP/installer and SHA256SUMS. It requires Inno Setup6+; pass `-ISCCPath` if necessary. The application `--check-runtime` flag exits after checking mandatory image plugins and SQLite without opening/creating a project.

Before release, run the UI tests on the native Windows platform as well as offscreen, inspect screenshots at desktop and narrow widths, and verify installer install/uninstall in an isolated directory. Test data must be synthetic, never user photos. The `SIFTER_QA_OUTPUT` environment variable enables local test screenshots; do not commit those containing local paths. Tests cover persistence/isolation, locks, migration, scanning exclusions, paired moves, format separation, single-format cleanup, collision refusal, copied transport, organization undo, project relocation and interrupted-move recovery. RAW uses a generated Bayer DNG; other brands rely on the pinned LibRaw implementation and are not all physically tested.

Keep CMake project version, installer version, package names, main About text and README synchronized. Publish only the tested commit under a version tag and GitHub Release. Include portable ZIP, installer, checksum manifest and matching third-party source ZIP. The latter includes Qtbase6.8.3, qtsvg6.8.3, qtimageformats6.8.3 and LibRaw0.22.3 archives. Verify upstream hashes and retain their notices. Do not include build folders, live catalogs, original user photos, settings, logs, `.pro.user` or local credentials.

Bundled dependencies are replaceable dynamic libraries. To rebuild LibRaw, use the archive/hash and target in `cmake/LibRaw.cmake`. To rebuild Qt, use the corresponding source archives and matching MinGW toolchain; replace compatible Qt DLLs and plugin subdirectories together. See THIRD-PARTY-NOTICES for licenses and source locations.

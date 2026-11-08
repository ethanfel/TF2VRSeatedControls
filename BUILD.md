# Building

Requires CMake 3.24+, C++17, and a Windows x64 toolchain. MinHook is vendored; CMake fetches OpenXR SDK headers at `release-1.1.63` unless `OPENXR_INCLUDE_DIR` is supplied. Original game/mod binaries are not included.

## Windows

From a Visual Studio developer shell:

```sh
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Release --target dist_package
```

## Linux cross-build

With MinGW-w64 and OpenXR development headers installed:

```sh
cmake -S . -B build-win -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw.cmake \
  -DOPENXR_INCLUDE_DIR=/usr/include/openxr \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
cmake --build build-win --target dist_package
```

The package ZIP is written into the build directory. Development builds have been compiled and checked with MinGW; the Visual Studio build path has not yet been verified.

Portable control checks only:

```sh
cmake -S . -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Windows test executables

Run these on Windows or in a separate test Wine prefix:

- `control_tests.exe`: tap/hold, crouch, geometry, and interaction checks.
- `tests/Titanfall2VRLauncher.exe`: simulated OpenXR input routing. This is a test executable, not the game launcher.
- `bootstrap_tests.exe <bootstrap_host.dll> <TF2VRSeatedControls.dll>`: loads the actual bootstrap DLL; registry operations are redirected to temporary keys. Use Release mode to exercise compiler optimizations.
- `native_probe.exe <path/to/Titanfall2VR.dll>`: checks native interaction and reload routines against a supported original DLL on synthetic inputs. It maps the DLL without running its initializer. Keep the original filename.

Native adapters require exact hashes for stable 1.0.25 or multiplayer beta 1.1.32. Unsupported builds are left unchanged; inspect and test any new profile before adding it.

Automated checks do not replace a headset playtest. Verify short/long Y presses, menus, dialogue, both grip modes, grenade throws, reloads, death/checkpoints, and entering/exiting Titans before calling a release stable.

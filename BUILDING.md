# Building Guide

This guide will help you build the project on your local machine. The process will require you to provide a decompressed ROM of the US version of the game.

These steps cover: decompressing the ROM, running the recompiler and finally building the project.

## 1. Clone the BanjoRecomp Repository
This project makes use of submodules so you will need to clone the repository with the `--recurse-submodules` flag.

```bash
git clone --branch android-port --recurse-submodules https://github.com/douglasjv/BanjoRecomp.git
cd BanjoRecomp

# if you forgot to clone with --recurse-submodules
# git submodule sync --recursive
# git submodule update --init --recursive
```

The published Android fork pins the matching submodule commits in `.gitmodules`, so a recursive clone of the `android-port` branch is enough to get the correct BanjoRecomp, RecompFrontend, N64ModernRuntime, and RT64 revisions for building.

## 2. Install Dependencies

### Linux
For Linux the instructions for Ubuntu are provided, but you can find the equivalent packages for your preferred distro.

```bash
# For Ubuntu, simply run:
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev lld llvm clang
```

### Windows
You will need to install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/).
In the setup process you'll need to select the following options and tools for installation:
- Desktop development with C++
- C++ Clang Compiler for Windows
- C++ CMake tools for Windows

The other tool necessary will be `make` which can be installe via [Chocolatey](https://chocolatey.org/):
```bash
choco install make
```

### Android
For Android developer builds you will also need:

- Android Studio or the Android SDK command line tools with API 34, Build Tools, and an NDK that supports CMake-based arm64-v8a builds.
- Either an Android SDK location exported through `ANDROID_HOME`/`ANDROID_SDK_ROOT` or an `android/local.properties` file with `sdk.dir=/absolute/path/to/sdk`.
- Either an SDL 2.30.3 source tree that includes `android-project/`, or an SDL 2.30.3 source archive. You can point the build at them with `BANJO_ANDROID_SDL_SOURCE_DIR` / `banjoAndroidSdl2Dir` or `BANJO_ANDROID_SDL_ARCHIVE` / `banjoAndroidSdl2Archive`. If you provide neither, Gradle will try to download the SDL source release automatically.

## 3. Decompressing the target ROM
You will need to decompress the NTSC-U 1.0 N64 Banjo-Kazooie ROM (sha1: d6133ace5afaa0882cf214cf88daba39e266c078) before running the recompiler.

The most straightforward way to do this is to set up the [Banjo-Kazooie decompilation](https://gitlab.com/banjo.decomp/banjo-kazooie), which will decompress the ROM when building. Alternatively, you can run the [bk_rom_compressor tool](https://github.com/MittenzHugg/bk_rom_compressor) directly, which is what the decompilation uses to decompress the ROM.

Regardless of which method you use, copy the decompressed ROM to the root of the BanjoRecomp repository with this filename:
- `banjo.us.v10.decompressed.z64`

## 4. Generating the C code

Now that you have the required files, you must build [N64Recomp](https://github.com/N64Recomp/N64Recomp) and run it to generate the C code to be compiled. The building instructions can be found [here](https://github.com/N64Recomp/N64Recomp?tab=readme-ov-file#building). That will build the executables: `N64Recomp` and `RSPRecomp` which you should copy to the root of the BanjoRecomp repository.

After that, go back to the repository root, and run the following commands:
```bash
./N64Recomp banjo.us.rev0.toml
./RSPRecomp n_aspMain.us.rev0.toml
```

## 5. Building the Project

Finally, you can build the project! :rocket:

On Windows, you can open the repository folder with Visual Studio, and you'll be able to `[build / run / debug]` the project from there.

If you prefer the command line or you're on a Unix platform you can build the project using CMake:

```bash
cmake -S . -B build-cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -G Ninja -DCMAKE_BUILD_TYPE=Release # or Debug if you want to debug
cmake --build build-cmake --target BanjoRecompiled -j$(nproc) --config Release # or Debug
```

### Android debug APK

The Android project lives under `android/` and wraps the root CMake build as an SDL-based shared library.

1. Point Gradle at your Android SDK if it is not already configured globally:

```bash
export ANDROID_HOME=/absolute/path/to/android-sdk
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/<installed-version>
```

   Or create `android/local.properties` with:

```properties
sdk.dir=/absolute/path/to/android-sdk
```

   On macOS, if neither environment variable nor `android/local.properties` is set, the Gradle project also checks common SDK install locations such as `~/Library/Android/sdk`, `~/Library/Android/Sdk`, `~/Android/Sdk`, `/opt/homebrew/share/android-commandlinetools`, `/usr/local/share/android-commandlinetools`, `/opt/homebrew/share/android-sdk`, and `/usr/local/share/android-sdk`, and writes `android/local.properties` automatically when it finds one.

2. Optionally point the build at a local SDL checkout or source archive to avoid the automatic download step:

```bash
export BANJO_ANDROID_SDL_SOURCE_DIR=/absolute/path/to/SDL2-2.30.3
# or
export BANJO_ANDROID_SDL_ARCHIVE=/absolute/path/to/SDL2-2.30.3.zip
```

3. Build the debug APK:

```bash
gradle -p android :app:assembleDebug
```

The resulting APK will be at:

```text
android/app/build/outputs/apk/debug/BanjoRecomp-android-v1.0.1-debug.apk
```

4. To build a properly signed release APK, configure a release keystore in `android/local.properties` (do not commit these values) or provide the equivalent environment variables:

```properties
banjo.release.storeFile=/absolute/path/to/your-release.keystore
banjo.release.storePassword=your-store-password
banjo.release.keyAlias=your-key-alias
banjo.release.keyPassword=your-key-password
```

Then build the release variant:

```bash
gradle -p android :app:assembleRelease
```

With release signing configured, the signed APK will be written to:

```text
android/app/build/outputs/apk/release/BanjoRecomp-android-v1.0.1-release.apk
```

Without a release keystore, the Android release build still succeeds but produces an unsigned artifact named `BanjoRecomp-android-v1.0.1-release-unsigned.apk`.

The Android build packages the runtime assets and controller database automatically, but it does not bundle the game ROM. On first launch, use the launcher to pick your own supported ROM through Android's document picker. Mods and texture packs can be imported from the Mods menu through the same picker. During gameplay the Android build also enables an on-screen touch overlay with movement, face buttons, C-buttons, Start, and a Menu shortcut for opening the recomp configuration UI; paired controllers continue to work alongside it.

If you need to sideload files manually instead, the Android build prefers the app-specific external files directory when available, which is typically under:

```text
Android/data/io.github.banjorecomp.android/files/
```

Mods and texture packs should be copied into that directory's `mods/` subfolder, and the ROM should be stored as `bk.n64.us.1.0.z64`. Once the ROM has been imported there successfully, normal APK updates should not require selecting it again unless the app data is removed.

## 6. Success

Voilà! You should now have a `BanjoRecompiled` executable in the build directory! If you used Visual Studio this will be `out/build/x64-[Configuration]` and if you used the provided CMake commands then this will be `build-cmake`. You will need to run the executable out of the root folder of this project or copy the assets folder to the build folder to run it.

> [!IMPORTANT]
> In the game itself, you should be using a standard ROM, not the decompressed one.

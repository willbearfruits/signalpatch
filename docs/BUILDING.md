# Building and running

Needs CMake 3.22 or newer and a C++20 compiler. JUCE 8.0.13, GLFW, NanoVG
and NeuralAmpModelerCore are fetched when they are not found locally.

Options:

| Option | Default | Effect |
| --- | --- | --- |
| `SIGNALPATCH_ENABLE_NAM` | ON | OFF builds without NeuralAmpModelerCore; the neural modules pass audio through |
| `SIGNALPATCH_NAM_CORE_DIR` | unset | use a local NeuralAmpModelerCore checkout instead of fetching |
| `SIGNALPATCH_ALLOW_JUCE_FETCH` | ON | OFF requires an installed JUCE |
| `SIGNALPATCH_CPU_BASELINE` | `x86-64-v3` | instruction set to build for. AVX2 makes neural captures about a quarter cheaper; set it to empty for CPUs older than 2013 |
| `SIGNALPATCH_ENABLE_ASIO` | OFF | Windows ASIO; needs Steinberg's SDK and its licence |
| `SIGNALPATCH_BUILD_JUCE_UI` | OFF | also build the retired JUCE rack (`signalpatch_juce`) |

## Linux

```sh
# Debian / Ubuntu
sudo apt install ninja-build libasound2-dev libjack-jackd2-dev libglfw3-dev \
  libgl-dev libcurl4-openssl-dev libfreetype6-dev libfontconfig1-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev
# Arch
sudo pacman -S ninja glfw curl alsa-lib jack2 freetype2 fontconfig

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run it directly for ALSA or JACK. On PipeWire go through `pw-jack`:

```sh
PIPEWIRE_QUANTUM=128/48000 pw-jack ./build/signalpatch_artefacts/RelWithDebInfo/SignalPatch
```

`PIPEWIRE_LATENCY` is only a hint: `pw-jack` keeps the client at whatever
quantum the graph had when it connected, which is 1024 while a browser is
playing. `PIPEWIRE_QUANTUM` forces it. Keep the interface on the Pro Audio
profile.

`packaging/linux/run-from-build.sh` wraps that line. To get an app-menu
entry that runs the build tree:

```sh
sed "s|^Exec=.*|Exec=$PWD/packaging/linux/run-from-build.sh %f|" \
  packaging/linux/io.github.willbearfruits.SignalPatch.desktop \
  > ~/.local/share/applications/io.github.willbearfruits.SignalPatch.desktop
cp packaging/linux/io.github.willbearfruits.SignalPatch.svg ~/.local/share/icons/hicolor/scalable/apps/
```

### Realtime priority

If the header says **NO RT**, the audio thread runs under the normal
scheduler and a busy desktop will cause dropouts. On Arch:

```sh
sudo pacman -S realtime-privileges
sudo gpasswd -a $USER realtime     # then log in again
```

Do not use rtkit with PipeWire 1.6.8. PipeWire ends up setting
`RLIMIT_RTTIME` to 0 and the kernel kills the app as soon as its audio
thread goes realtime. `PIPEWIRE_DEBUG="mod.rt:5"` shows it happening.

## Windows

From a Visual Studio 2022 developer shell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\signalpatch_artefacts\Release\SignalPatch.exe
```

The release zips are the CI build plus its `fonts/` folder. WASAPI is the
default backend.

## Flatpak

```sh
flatpak install flathub org.flatpak.Builder org.freedesktop.Sdk//24.08
flatpak run org.flatpak.Builder --user --install --force-clean \
    build-flatpak packaging/flatpak/io.github.willbearfruits.SignalPatch.yml
```

The manifest has not been rebuilt since the app moved to GLFW.

## Command line

```
SignalPatch [options] [patch.signalpatch | bundle.zip]
  --board        start on the Board
  --kiosk        fullscreen without decorations
  --unmute       fade in at start instead of starting muted
  --touch        touch mode (--no-touch turns it off)
  --scale=1.25   UI scale; also SIGNALPATCH_SCALE, and Ctrl +/- in the app
```

Touch mode turns itself on for a small high-DPI screen: larger targets, tap
an output (anywhere on the right edge of the plate at socket height) and
then tap the module it should go into, hold for the menu a right click
opens, double tap to fit. A module's menu can also send its output to any
other module.

## Tests

`signalpatch_tests` is one binary with every case. Useful variables:

- `SIGNALPATCH_SOAK_BLOCKS=1687500` runs the allocation-trap graph for 30
  minutes of audio.
- `SIGNALPATCH_BENCH_NAM_DIR=<folder>` benchmarks `.nam` models against the
  64-sample deadline, then a two-amp rig on one core and on several.
- `SIGNALPATCH_THREADS=N` in the app forces the number of audio threads.

Sanitizer builds live in `build-asan/` (`-fsanitize=address,undefined`) and
`build-tsan/` (`-fsanitize=thread -DSIGNALPATCH_BUILD_V2=OFF`).

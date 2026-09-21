# Build and first run

The project uses C++17, CMake 3.21 or newer, and vcpkg manifest mode. All targets,
including the CLI, currently build against Qt, Boost, OpenSSL, PortAudio and PoDoFo.
Mock mode removes the need for service credentials and audio hardware at runtime;
it does not remove these build dependencies. The first build downloads and compiles
dependencies and can take considerably longer than later builds.

## 1. Install platform prerequisites

### macOS

Install Apple's Command Line Tools if they are missing:

```sh
xcode-select --install
```

With [Homebrew](https://brew.sh/) installed:

```sh
brew install cmake ninja pkg-config autoconf automake libtool
```

The development machine uses Apple Silicon. Intel macOS is not yet verified.

### Ubuntu 24.04

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build g++ git curl zip unzip tar \
  pkg-config autoconf autoconf-archive automake libtool bison flex gperf \
  python3 python3-jinja2 libltdl-dev libasound2-dev libjack-jackd2-dev \
  '^libxcb.*-dev' libx11-xcb-dev libglu1-mesa-dev libxrender-dev libxi-dev \
  libxkbcommon-dev libxkbcommon-x11-dev libegl1-mesa-dev
```

Qt needs the X11/XCB/OpenGL development packages even though automated UI tests
use an offscreen platform. PortAudio also needs the platform audio development
packages. These packages are build prerequisites; Mock does not open audio devices.

## 2. Clone the project and prepare vcpkg

```sh
git clone https://github.com/WANJU-boop/ai_mock_interview.git
cd ai_mock_interview
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
export VCPKG_ROOT="$HOME/vcpkg"
git -C "$VCPKG_ROOT" checkout c01dc6ea7353758f2ed70df0bc29c97dfc650b3a
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
```

If vcpkg is already installed, skip its clone command and set `VCPKG_ROOT` to that
installation. Checking out the baseline changes the existing vcpkg checkout; use
a separate installation if you need to keep another project's version.
The commit above matches `builtin-baseline` in `vcpkg.json` and the CI workflow.

`export` only affects the current terminal. Add `export VCPKG_ROOT="$HOME/vcpkg"`
to your shell profile if you want it available in new terminals. Do not put machine
specific absolute paths into the project's CMake files.

## 3. Configure, build and test

Run from the project directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

CMake invokes vcpkg to install the manifest dependencies. There is no separate
manual `vcpkg install <library>` step. Two parallel jobs keep memory usage modest;
you can increase the number on a machine with sufficient memory.

## 4. Complete an interview without an API key

### Desktop: automatic Mock walkthrough

macOS:

```sh
./build/AI_mock_interview_qt.app/Contents/MacOS/AI_mock_interview_qt config.example.json
```

Ubuntu, in a graphical desktop session:

```sh
./build/AI_mock_interview_qt config.example.json
```

Click **开始面试** (Start interview). The default Qt Mock mode automatically plays
three scripted candidate answers, scores them, and writes a JSON report under
`reports/`. This is a deterministic demonstration, not online AI or live speech
recognition. Neither a microphone nor an API key is needed. The UI is currently
in Chinese. Click **查看报告** (View report) to read the answers, scores and feedback
inside the app, or **打开目录** (Open folder) to locate the JSON file.

### CLI: type your own answers

```sh
./build/AI_mock_interview config.example.json
```

Answer each question in the terminal. A short answer may trigger a follow-up;
answer that prompt too. The default scorer uses fixed Mock rules. A successful
session prints the report path and saves question, answer, follow-up, score and
feedback fields in the JSON file.

### Automatic check, including headless machines

```sh
./build/AI_mock_interview_realtime_demo config.example.json
```

Expected result: three scored answers, a saved report path, and the completion
message `Realtime mock 面试完成。`. This is the same entry point used by CI's
application smoke check. Network access is required to install build dependencies,
but not to run this Mock interview.

## Troubleshooting

- **CMake cannot find Qt/Boost/etc.** Check `VCPKG_ROOT` and use the explicit
  toolchain argument above. For a cache configured with another toolchain or
  generator, use a new build directory (for example `-B build/first-run`).
- **Qt cannot load the xcb plugin on Ubuntu.** Install all platform prerequisites
  above. A real desktop is needed to show the window; `QT_QPA_PLATFORM=offscreen`
  is intended for tests, not an interactive session.
- **No report appears.** Check the session's error message and the working
  directory. `reports/` is relative to the directory from which you launch the
  program. Ensure `report.save_json` is true and the directory is writable.
- **Double-clicking the macOS app cannot locate configuration.** Launch using the
  explicit command above or select `config.example.json` in the window.
- **Build is slow.** vcpkg compiles large native dependencies, sometimes for both
  Debug and Release. CI caches compiled dependencies after a successful run.

## Optional real services

Copy `config.example.json` to the Git-ignored `config.local.json`. Configure
`llm.provider=http`, the model and HTTPS base URL for your compatible LLM service.
Set the environment variable named by `llm.api_key_env` in the terminal that
launches the program. The CLI can then exercise real question generation and
scoring without a realtime speech service.

Realtime voice additionally needs `realtime.provider=volc`, valid credentials,
and `realtime.dialog.input_mod=keep_alive` for the Qt flow. macOS will request
microphone permission on the first real voice run. These services are optional
and may incur provider charges. Keep TLS certificate verification enabled.

Unit tests and CI never use `config.local.json`, real credentials, microphones,
or paid services. Service availability is separate from Mock and CI results.

References: [Microsoft's vcpkg setup guide](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
and the pinned vcpkg Qt platform prerequisites.

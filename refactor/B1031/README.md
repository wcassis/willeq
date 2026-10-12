# B1031 — macOS dev-platform probe: headless build error census on the M4 mini (issue #351)

**Class B. Card 1 of prove-then-fix (`docs/local_model_cards.md`): PROVE only.
No source, CMake or test change in this unit; card 2 is the fix.**

Scope (Will, 2026-10-12): the DEV PLATFORM — the headless client and the test
suite on a Mac. No visual gate, no budgets, no goldens. A windowed renderer
(ANGLE) is a later item cut from these findings.

Evidence lives outside the checkout in `/mnt/projects/fable/review/B1031/`
(index: that directory's `README.md`). Every number below is quoted from a
file there and names the command that produced it.

## Step 0 — host inventory

`uname -a; sw_vers; sysctl -n hw.memsize hw.ncpu machdep.cpu.brand_string;
xcodebuild -version; /usr/bin/clang --version | head -1` over ssh as `willeq`
(`review/B1031/host_inventory.txt`):

| item | value |
|---|---|
| host | `m4mini.goldfarm.local` (Darwin 24.6.0, xnu-11417.140.69.708.3~1, arm64) |
| OS | macOS 15.7.4 (24G517) |
| CPU | Apple M4, `hw.ncpu` 10 |
| memory | `hw.memsize` 17179869184 (16 GiB) |
| Xcode | 26.2 (17C52) |
| compiler | Apple clang 17.0.0 (clang-1700.6.3.2), `/usr/bin/clang` |

The mini also runs Ollama; the build ran under `nice -n 10` at `-j 8`.

## Step 1 — dependencies

**Finding 0 (environment, not code): `brew install` cannot run as `willeq`.**
`/opt/homebrew/bin/brew install cmake ninja fmt glm jsoncpp zlib` exited 1
with `Error: /opt/homebrew/Cellar is not writable` (`review/B1031/brew_install.log`):
the Homebrew prefix is owned by `will` (`drwxrwxr-x will admin`) and `willeq` is
not in `admin`. Fixing that needs Will (run the install as `will`, or
`chown`), so this probe installed **the exact versions Homebrew would have
installed** (`brew info --json=v2`, read-only) into a user prefix
`~/willeq-deps` with `review/B1031/deps.sh` (log `review/B1031/deps.log`,
exit 0):

| formula | version | how |
|---|---|---|
| cmake | 4.4.4 | Kitware `cmake-4.4.4-macos-universal.tar.gz` |
| ninja | 1.13.2 | `ninja-mac.zip` release binary |
| fmt | 12.2.0 | source, shared, `~/willeq-deps` |
| glm | 1.0.3 | headers only |
| jsoncpp | 1.9.8 | source, shared, `~/willeq-deps` |
| zlib | 1.2.12 | the macOS SDK's `libz.tbd` (Homebrew's `zlib` is keg-only, so CMake finds the SDK copy either way) |
| openssl@3 | 3.6.1 | already in Homebrew (`brew list --versions openssl@3`) |

Every configure/build below adds `-DCMAKE_PREFIX_PATH=$HOME/willeq-deps` and
puts `~/willeq-deps/bin` first on `PATH`; nothing else differs from the issue's
recipe. There is no `pkg-config` on the mini, so `jsoncpp` was found by the
`FIND_PATH`/`FIND_LIBRARY` fallback in `CMakeLists.txt`.

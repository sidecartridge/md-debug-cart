# AGENTS.md — md-debug-cart Playbook

Welcome to the `md-debug-cart` workspace. This is the quick primer so any agent can get productive fast.

## 1. Environment setup (do this before touching the repo)
- **Host tooling**
  - ARM GNU Toolchain 14.2 at `/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin` (export `PICO_TOOLCHAIN_PATH` to this path).
  - Raspberry Pi Debug Probe / Picoprobe wired to the Multi-device header (TX, RX and both GND pins **must** be connected).
  - Git + GNU Make + VS Code with the C/C++ Extension Pack, CMake Tools and Cortex-Debug.
- **SDK environment variables** (add them to your shell profile):
  ```bash
  export PICO_SDK_PATH=$REPO_ROOT/pico-sdk
  ```
- **Optional debugger helpers**
  ```bash
  export ARM_GDB_PATH=/path/to/arm-none-eabi/bin
  export PICO_OPENOCD_PATH=/path/to/openocd/tcl
  ```
- **Workspace root:** `/Users/diego/mister_wkspc/md-debug-cart`

## 2. Common Commands
```bash
# Build firmware (example board + UUID)
cd /Users/diego/mister_wkspc/md-debug-cart
PICO_TOOLCHAIN_PATH=/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin \
  ./build.sh pico_w release 123e4567-e89b-12d3-a456-426614174000

# Build the RP firmware only
cd /Users/diego/mister_wkspc/md-debug-cart/rp
PICO_TOOLCHAIN_PATH=/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/bin \
  ./build.sh pico_w release
```

## 3. Build Notes & Gotchas
- `CHARACTER_GAP_MS` constant lives in `rp/src/include/blink.h`. Keep it defined (700 ms) or the RP build fails.
- The repo no longer uses `pico-extras`, `fatfs-sdk`, or the old `target/` Atari build path.
- `./build.sh` copies `version.txt` into `rp/`, builds the RP firmware, writes the MD5 to `dist/rp.uf2.md5sum`, and generates `dist/<APP_UUID>-<VERSION>.uf2` plus `dist/<APP_UUID>.json`.
- `rp/build.sh` pins `pico-sdk` to tag `2.2.0` before building.
- The RP firmware now handles SELECT-button debounce and short/long-press detection inside `rp/src/emul.c` on the same core; there is no `select.c`, `select.h`, or `pico_multicore` dependency anymore.

## 4. Troubleshooting
| Symptom | Fix |
| --- | --- |
| `arm-none-eabi-gcc not found` | Ensure `PICO_TOOLCHAIN_PATH` points to the Arm GNU toolchain bin dir |
| Build stops with missing `CHARACTER_GAP_MS` | Re-add `#define CHARACTER_GAP_MS 700` to `rp/src/include/blink.h` |
| Final steps fail copying UF2 | Upstream compile failed—scroll back for the first error before the copy step |
| CMake cannot find the Pico SDK | Ensure `PICO_SDK_PATH` points to `$REPO_ROOT/pico-sdk`, or let `rp/src/CMakeLists.txt` resolve the bundled SDK automatically |

## 5. Editing Guardrails
- Agents are **not allowed** to modify code inside these directories under any circumstances:
  - `/pico-sdk`

Keep this file updated as the process evolves so every agent starts with the latest tribal knowledge.

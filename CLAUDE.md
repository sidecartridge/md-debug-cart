# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

See also: `README.md` (what the app does, the `0xFBxxxx` debug-print protocol, EmuTOS usage, installation via Booster), `AGENTS.md` (overlapping playbook + troubleshooting table), `desc/README.md` (`app.json` field reference).

## What this repo is

**Debug Cart**, a Sidecartridge Multi-device microfirmware app for Atari ST / STE / MegaST(E). The app turns the Multi-device into a debug-output device: the Atari "prints" a character by *reading* from the ROM3 cartridge window at `0xFB0000 + (char << 1)`, the RP2040 captures the character off address lines A8–A1, and forwards it to the host over USB serial (CDC). Nothing is written back to the Atari — the cartridge is a one-way sink.

This is a **migration of Christian Zietz's [atari-debug-cart](https://github.com/czietz/atari-debug-cart)** onto the Multi-device microfirmware layout, derived from `md-microfirmware-template`. Public build/usage docs: <https://docs.sidecartridge.com/sidecartridge-multidevice/programming/>.

**RP2040 only.** Unlike other apps built from the same template, there is no m68k target — no `target/`, no `stcmd`/vasm build, no embedded `target_firmware.h`, no shared 64 KB cartridge data region, and no networking, SD card, display or terminal code. `pico-sdk` is the only submodule (`pico-extras` and `fatfs-sdk` are gone).

## Build

Top-level build is driven by `build.sh` in the repo root:

```bash
# <board_type>   = pico | pico_w | sidecartos_16mb   (CMake rejects anything else)
# <build_type>   = debug | release                   (always compiled as MinSizeRel — see below)
# <app_uuid_key> = UUID4 identifying this app, must match desc/app.json
./build.sh pico_w release 123e4567-e89b-12d3-a456-426614174000
```

Required host environment:
- ARM GNU Toolchain 14.2 — export `PICO_TOOLCHAIN_PATH` to its `arm-none-eabi/bin` dir.
- `PICO_SDK_PATH` is optional; `rp/src/CMakeLists.txt` falls back to the bundled `pico-sdk/` at the repo root.

Build flow (orchestrated by `build.sh`):
1. Copies `version.txt` into `rp/`, wipes and recreates `dist/`.
2. Runs `rp/build.sh`, which `git submodule update --init --recursive pico-sdk`, **pins pico-sdk to tag `2.2.0`**, wipes `rp/build/`, configures CMake and `make -j4`, then copies the UF2 into `rp/dist/`.
3. Computes MD5 into `dist/rp.uf2.md5sum`, renames the UF2 to `dist/<APP_UUID>-<VERSION>.uf2`, and substitutes UUID / MD5 / version into `dist/<APP_UUID>.json` from the `desc/app.json` template.

### Iterating on RP-side changes
`rp/build.sh` can be run directly to skip the dist/MD5/JSON packaging (it still re-pins the SDK and wipes `rp/build/`, so a configure+compile from scratch every time):

```bash
cd rp && ./build.sh pico_w release
```

For a fast edit-compile loop on C changes only, `make -j4` inside the existing `rp/build/` reuses the CMake cache and skips the submodule pinning entirely.

### Build gotchas
- **CMake always builds with `-DCMAKE_BUILD_TYPE=MinSizeRel`** regardless of the `<build_type>` argument. A full `Release` previously caused breakage (memory/over-optimization). The legacy line is left commented in `rp/build.sh`. `<build_type>` only controls the `DEBUG_MODE` macro and the dist filename.
- **`<build_type>` case matters for the filename but not for the debug flag.** `rp/build.sh` lowercases the argument to decide `DEBUG_MODE` (`release` → `0`), but both scripts compare it verbatim (`= "release"`) to pick the artifact name. So `Release` — what both CI workflows pass — yields a *release* build named `rp-pico_w-Release.uf2` instead of `rp-pico_w.uf2`. Consistent between the two scripts, but don't be surprised by the suffix.
- **Capture output is `stdout`; debug traces are `stderr`.** `pico_enable_stdio_usb` is on in every build, but `pico_enable_stdio_uart` and the `DPRINTF`/`DPRINTFRAW` macros are compiled out when `_DEBUG == 0`. Never route captured Atari bytes through `DPRINTF` — they would vanish in a release build. Conversely, `DPRINTF` in the capture hot path costs real time and can drop bytes.
- `blink.h` still defines `CHARACTER_GAP_MS` (700) and `AGENTS.md` claims removing it breaks the build. Nothing references it any more — it is a leftover from the template's Morse-code status LED. Harmless either way; don't treat the AGENTS.md line as current.
- Both CI workflows still install the `atarist-toolkit-docker` image (`stcmd`). That step is dead weight inherited from the template — this repo has no m68k target.

### CI / release
- `.github/workflows/build.yml` builds `pico_w Release` on PR and on `workflow_dispatch`.
- `.github/workflows/release.yml` triggers on `v*` tags: builds, attaches the UF2 + JSON to the GitHub Release with the top section of `CHANGELOG.md` as release notes, and uploads them to `s3://atarist.sidecartridge.com/`.
- `make tag` tags HEAD with the contents of `version.txt` and pushes the tag (which triggers the release workflow).
- `secrets.sh` (gitignored, untracked) exports `AWS_ACCESS_KEY_ID` / `AWS_SECRET_ACCESS_KEY` for manual uploads. Never commit it, echo it, or copy its values into any other file.

### Tests
There is no test suite. "Verification" is: build succeeds, UF2 boots on hardware, and characters printed by the Atari (e.g. EmuTOS built with `CARTRIDGE_DEBUG_PRINT=1`) appear on the USB serial port.

## Architecture

### Debug-print protocol
The Atari emits a byte by performing a *read* in the ROM3 window:

```c
#define CARTRIDGE_ROM3 0xFB0000ul
(void)(*((volatile short*)(CARTRIDGE_ROM3 + ('A' << 1))));
```

The character rides on address lines **A8–A1** (hence the `<< 1`), and the falling edge of `ROM3` marks the access. Nothing is driven back onto the data bus, so no cartridge ROM image, header or handshake exists on the Atari side — any code that can do a `move.w` to `$FBxxxx` is a valid producer.

### RP2040 side (`rp/src/`)
- `main.c` — sets clock/voltage, calls `gconfig_init` (global config) then `aconfig_init` (per-app config), and hands off to `emul_start()`. If either fails it jumps to the **Booster** app via `reset_jump_to_booster()` to bootstrap. **Don't add features to `main.c`** — put them in `emul.c` or a new module.
- `emul.c` — the whole application. Despite the template-inherited name it emulates nothing; it is the capture pipeline plus the main loop. This is where feature work goes.
- `gconfig.c` / `aconfig.c` — global vs per-app configuration stored in dedicated flash sectors, on top of `settings/` (a key-value store). `aconfig.c` still carries the template's placeholder entries (`ACONFIG_PARAM_FOLDER`, `ACONFIG_PARAM_MODE`); the app does not act on them.
- `blink.c` — LED on/off (CYW43 GPIO on `pico_w`, `PICO_DEFAULT_LED_PIN` otherwise). Used as a capture-activity indicator.
- `reset.c` / `include/reset.h` — `reset_device()` (watchdog reboot to the start of flash) and the inline `reset_jump_to_booster()` (VTOR + SP + `bx` into `BOOSTER_APP_FLASH`).

### Capture pipeline (`emul.c`)
Three stages, all set up once in `emul_start()` and then free-running:

1. **`debugcart_init_hardware`** — drives `READ_SIGNAL_GPIO_BASE` (GPIO 27) permanently low to enable the external address-bus buffer, and puts `ROM3_GPIO` (GPIO 26) under PIO control with a pull-up.
2. **`debugcart_init_pio`** — assembles a 3-instruction program at runtime with `pio_encode_*` (wait ROM3 high with a 4-cycle delay → wait ROM3 low → `in pins, 8`) and loads it on **`pio1`** (`pio0` is left free for the ROM-emulator programs other apps run). Input pins start at `READ_ADDR_GPIO_BASE + 1` because GPIO 6 is A0 and the payload starts at A1. Right-shift + autopush at 8 bits, RX FIFO joined.
3. **`debugcart_init_dma`** — one DMA channel, paced by the PIO RX DREQ, byte-sized, reading the **MSB byte** of the FIFO word (`&rxf[sm] + 3`, where a right-shifted 8-bit autopush lands) into `debugBuffer` with a write-side ring wrap. Transfer count is `0xFFFFFFFF`, i.e. effectively endless.

`debugBuffer` is 32 KiB (`DEBUG_RING_BITS = 15`) and **must stay `__attribute__((aligned(DEBUG_RING_SIZE)))`** — the RP2040 ring wrap requires the buffer to be naturally aligned to its size, and 15 bits is the hardware maximum. Changing the size means changing `DEBUG_RING_BITS` alone; both the size and the alignment derive from it.

The main loop then, on every iteration:
- polls the SELECT button (debounced in software, same core — there is no `select.c` and no `pico_multicore` dependency): short press → `reset_jump_to_booster()`, press ≥ 10 s → `reset_device()`;
- derives the DMA write cursor from `DEBUG_DMA_SIZE - dma_hw->ch[ch].transfer_count`, modulo the ring size, and `fwrite`s everything between it and the read cursor to `stdout` (handling the one wrap case as two writes);
- pulses the LED for 20 ms whenever bytes moved.

There is no back-pressure: if the host does not drain USB CDC fast enough the DMA keeps writing and old bytes are overwritten. Any new work added to the loop must stay short for the same reason.

### GPIO map (`rp/src/include/constants.h`)

| Constant | GPIO | Purpose |
| --- | --- | --- |
| `SELECT_GPIO` | 5 | SELECT button (active high, pulled down) |
| `READ_ADDR_GPIO_BASE` | 6 | A0 of the address bus; 16 lines, capture starts at +1 (A1) |
| `ROM3_GPIO` | 26 | ROM3 chip select — the access strobe |
| `READ_SIGNAL_GPIO_BASE` | 27 | READ, held low to enable the address-bus buffer |

Core 0 overclocks to 225 MHz (`RP2040_CLOCK_FREQ_KHZ`) at `VREG_VOLTAGE_1_10` — the PIO capture depends on it.

### Memory layout (`rp/src/memmap_rp.ld`)
The RP2040's flash and RAM are sliced into named regions, and code is responsible for not stomping on them:

| Region | Origin | Length | Purpose |
| --- | --- | --- | --- |
| `FLASH` | `0x10000000` | 1024 K | App code |
| `BOOSTER_APP_FLASH` | `0x10120000` | 768 K | Reserved for the Booster app (do not write from this app) |
| `CONFIG_FLASH` | `0x101E0000` | 120 K | 30 sectors of per-app config |
| `GLOBAL_LOOKUP_FLASH` | `0x101FE000` | 4 K | UUID → config-sector lookup |
| `GLOBAL_CONFIG_FLASH` | `0x101FF000` | 4 K | Global config |
| `RAM` | `0x20000000` | 128 K | Normal RAM (the 32 KiB capture ring lives here) |
| `ROM_IN_RAM` | `0x20020000` | 128 K | Inherited ROM mirror region; unused by this app |
| `SCRATCH_X` / `SCRATCH_Y` | `0x20040000` / `0x20041000` | 4 K each | Core 1 / Core 0 stacks |

The build assumes Core 0 owns flash writes (`PICO_FLASH_ASSUME_CORE0_SAFE=1`). `ROM_BANKS` and `ROM_SIZE_BYTES` in `constants.h` are likewise leftovers, referenced only by a startup `DPRINTF`.

### App identity
`CURRENT_APP_UUID_KEY` (set from the `APP_UUID_KEY` env var at CMake time, defaulting to `44444444-4444-4444-8444-444444444444`) is the app's UUID4. It must match the `uuid` field in `desc/app.json` and is used as the key into `GLOBAL_LOOKUP_FLASH` to find this app's config sector. Mismatch → app jumps to Booster.

## Editing guardrails

- **Never modify `pico-sdk/`** — it is a git submodule pinned to tag `2.2.0`, and `rp/build.sh` re-pins it on every run, so local edits are silently discarded.
- Don't touch `main.c` for feature work — start in `emul.c`.
- Don't add code to the capture main loop that can block or that prints in release builds; dropped bytes are the failure mode and they are invisible from the RP side.
- Match the existing C style (clang-format config in `.clang-format`, clang-tidy in `.clang-tidy` — both wired up via CMake when the binaries are on `PATH`). Note the existing naming mix: `snake_case` for functions, `lowerCamelCase` for locals and statics, `SCREAMING_SNAKE_CASE` for macros.
- `rp/build/` and `dist/` are build output and gitignored — never hand-edit anything under them.

---

## Working style

These behavioral guidelines bias toward caution over speed. For trivial tasks, use judgment.

### 1. Think before coding

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them — don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

### 2. Simplicity first

Minimum code that solves the problem. Nothing speculative.
- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

### 3. Surgical changes

Touch only what you must. Clean up only your own mess.
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it — don't delete it.
- When your changes orphan an import/variable/function, remove it. Don't remove pre-existing dead code unless asked.

The test: every changed line should trace directly to the user's request.

### 4. Goal-driven execution

Define success criteria. Loop until verified.
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan with a verification check per step.

### 5. No AI attribution

Never add AI-tool attribution to commits, PR descriptions, code comments,
docs, or any other artifact. This means **no**:
- "Generated with Claude Code", "Co-authored by Claude", "Made with ChatGPT",
  or any similar phrasing.
- `Co-Authored-By: Claude …`, `Co-Authored-By: ChatGPT …`, or any other
  AI co-author trailer.
- "AI-assisted", "written with the help of an LLM", etc., as comments or
  changelog entries.

Write the message as the human author. Do not mention AI tools used to
produce the work.

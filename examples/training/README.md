# Ponce practice crackmes (Linux x86_64)

Build with `make` in this directory; no Ponce or IDA SDK needed to build these programs. Outputs are in `build/`. The source is intentionally readable so you can compare what IDA and Ponce report with the C code. Use a disposable debugger session for each attempt.

```sh
cd examples/training
make
./build/01_taint_flow AAAA
./build/02_symbolic_xor AAAA
printf 'AAAA' > build/input.bin
./build/03_file_snapshot build/input.bin
```

In IDA, load one `build/` executable, select **Local Linux debugger**, and set its command-line arguments under **Debugger → Process options** (`AAAA` for exercises 1–2, absolute path to `input.bin` for exercise 3). Put a breakpoint at `training_pause` before starting execution. All binaries include debug symbols and are built without PIE or optimization, making that function easy to locate. At its entry on Linux x86_64, **RDI holds the pointer to four input bytes**. Pause there, then select the **address in RDI** (not the register itself) for **Ctrl+Shift+M → memory address = RDI value, size = 4**. You can navigate to that address in IDA's Hex View and select four bytes instead. If the prompt is missing or the action is disabled, check that the process is paused under the debugger and that Ponce loaded successfully.

## 1. Trace controlled bytes: `01_taint_flow`

Set **Edit → Ponce → Show Config** (`Ctrl+Shift+P`) to **Taint Engine**. Use argument `AAAA`. At `training_pause`, taint four bytes at the address in RDI. Continue (`F9`). Inspect Ponce comments/colours in `check_input`: the input affects the comparisons even though the program says `Try again`. Try again with `RUNE` to see the success path. Taint mode identifies where input flows; it does not solve for the winning input.

## 2. Solve a branch: `02_symbolic_xor`

Set **Symbolic Engine**, restart debuggee, and use argument `AAAA`. At `training_pause`, symbolize four bytes at the address in RDI. Use **Run until symbolic condition** (`Ctrl+Shift+F9`), or continue (`F9`) and inspect branches inside `check_input`. Right-click a symbolic branch in the disassembly for **Solve formula** (output appears in IDA's Output window) or **Negate & Inject** (writes a solution into debuggee memory). Each comparison checks one byte, so you may need to solve successive branches. To verify your own answer, run the executable normally with a different four-character argument.

## 3. File input and snapshot: `03_file_snapshot`

Create `build/input.bin` with `printf 'AAAA' > build/input.bin` and pass its **absolute path** as argument. Set **Symbolic Engine**. Pause at `training_pause` and symbolize four bytes at the address in RDI. You can solve this simple program with **Negate & Inject** alone: it changes the current branch and writes solved bytes into debuggee memory. To practice snapshots, first create one (`Ctrl+Shift+C`) at `training_pause` **before** continuing. Stop at a symbolic branch in `check_input`; right-click it and choose **Negate, Inject & Restore snapshot**. This replays execution from the saved point with the solved bytes. Snapshots matter more when later checks depend on calculations already performed using the old bytes. `Ctrl+Shift+D` deletes the snapshot. Neither action rewrites your original `build/input.bin`. Independently verify successful file contents by rerunning with a new file.

### Troubleshooting

- No Ponce menu or `libz3.so.4` load error: fix plugin/library loading first; these crackmes need the native plugin running in IDA.
- No enabled taint/symbolize action: start or attach the debugger and pause **before** `check_input` executes. Mark the four bytes pointed to by RDI, not RDI itself.
- No symbolic branch: make sure **Symbolic Engine** was chosen before symbolizing, and that tracing is enabled (`Ctrl+Shift+E` toggles it). Restart the process if comparisons already ran.
- Ponce slows dramatically when single-stepping libraries: start tracing at `training_pause` after input is available. Ponce only analyzes paths the debugger actually executes.
- If snapshot restore faults, compare IDA Output's `Snapshot saved SP/RDI` and `Snapshot restoring SP/RDI` lines with the debugger's RSP/RDI at the fault. Use the matching `libz3.so.4` from `build-ida93/`, not a symlink to a newer Z3 release.

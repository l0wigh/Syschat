# Pitfalls

- **ASan does not work** on this toolchain (zig cc, musl): link fails. Use `./build.sh debug` with gdb/valgrind instead.
- **UBSan panics**: `./build.sh ubsan` exits 134 on the first undefined behaviour instead of warning. That is the sanitizer stopping the process, not a syschat crash. `nm | grep ubsan` proves nothing here; trigger real UB to check.
- **Debug build** (`syschat_dbg`) disables the segfault catcher (`-DSYSCHAT_NO_CATCHER`) so a crash exits 134/139 and cores are written.
- **`SYSCHAT_DEBUG=<file>`** appends every raw incoming line, including decrypted TLS traffic, in clear. Never commit or leave that file.
- **Raw mode before connect**: a fatal error during connect must restore the terminal (`syschat_terminal_restore`), or the user's shell is left with echo off.
- **Write drops** are reported in red in the active buffer (no silent `-1`). Keep that behaviour when touching `tls_send`/`net_send`.
- **Incoming lines are capped at 511 bytes**; `BF_SIZE` is 512. Wrapping is only exercised on narrow terminals (see `tests/wrap_unit.c`).

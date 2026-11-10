# Pitfalls

- **ASan does not work** on this toolchain (zig cc, musl): link fails. Use `./build.sh debug` with gdb/valgrind instead.
- **UBSan panics**: `./build.sh ubsan` exits 134 on the first undefined behaviour instead of warning. That is the sanitizer stopping the process, not a syschat crash. `nm | grep ubsan` proves nothing here; trigger real UB to check.
- **Debug build** (`syschat_dbg`) disables the segfault catcher (`-DSYSCHAT_NO_CATCHER`) so a crash exits 134/139 and cores are written.
- **`SYSCHAT_DEBUG=<file>`** appends every raw incoming line, including decrypted TLS traffic, in clear. Never commit or leave that file.
- **Raw mode before connect**: a fatal error during connect must restore the terminal (`syschat_terminal_restore`), or the user's shell is left with echo off.
- **Write drops** are reported in red in the active buffer (no silent `-1`). Keep that behaviour when touching `tls_send`/`net_send`.
- **Incoming lines are capped at 511 bytes**; `BF_SIZE` is 512. Wrapping is only exercised on narrow terminals (see `tests/wrap_unit.c`).
- **Ctrl-Tab does not exist as a distinct key in most terminals**: a plain xterm sends the same `0x09` as Tab, so the client cannot tell them apart. Alt-Tab (`ESC` `Tab`) and Alt-Shift-Tab (`ESC ESC [ Z`) are the portable server-switch gestures; Ctrl-Tab only arrives separately from terminals using the kitty keyboard protocol (`ESC [ 9 ; <mod> u`) or xterm `modifyOtherKeys` (`ESC [ 27 ; <mod> ; 9 ~`). Both forms are handled in `syschat_handle_input`.
- **`syschat_handle_input` is a byte state machine**: any terminal byte that *ends* a sequence must reset `escape` to 0 before the next key. Forgetting it swallows the next sequence (Alt-Tab followed by Alt-Shift-Tab was the real bug) and leaks `[Z` into the prompt line.
- **The help list must fit one 24-row frame**: `tests/cli_args.py` asserts `Commands list` is still visible in the last frame after `/?`. A new line in `commands_handle_cmd_list` must be paid for by merging or dropping another, or the header scrolls off.

# AGENT.md

Syschat: IRC client in C (C11) built on libc and raw syscalls (poll, sockets, termios). TLS is the only third-party code: mbedTLS, vendored in `vendor/mbedtls`, built with the same compiler. Version 0.4.2-RC1.

## Branches (read first)
- Work on `ai`. Open PRs against `ai`. Never merge to `master` (prod) unless the user explicitly says so.
- Do not merge PRs yourself. The user merges.
- Branch names: `<type>/<slug>`. Commits and PR titles: Conventional Commits (`feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`, `ci`, `revert`).
- Do not commit binaries (`syschat`, `syschat_dbg`, `syschat_ubsan`, `tests/wrap_unit`, `build/`, `objs/`); they are gitignored.

## Build
- `./build.sh` (release, `-Werror`), `./build.sh debug` (`syschat_dbg`), `./build.sh ubsan`, `./build.sh clean`.
- `make` is equivalent but this machine has no make; `build.sh` is the verified path.
- First build compiles mbedTLS (~30 s), then it is cached in `build/mbedtls/`.
- Compiler flags must stay `-std=c11 -Wall -Wextra -Werror`. Third-party code is built without `-Werror`.

## Test
- Full offline suite: `tests/run_all.sh` (builds release + debug first).
- Single test: `./build.sh && python3 tests/<name>.py [binary]`. Tests are functional: mock IRC server + pty, or local TLS server. No unit framework except `tests/wrap_unit.c` and `tests/prompt_wrap_unit.c` (they include `screen.c` and fake the terminal).
- Real network test: `SYSCHAT_NET_TESTS=1 python3 tests/network_smoke.py`. Opt-in only. Never run it by default; it connects to a public network.
- Baseline on `ai`: all offline tests must pass before a PR. Report the actual output, not a summary.

## Layout
- `srcs/syschat.c` — entry point, argv/env parsing, exit codes.
- `srcs/syschat.h` — shared `t_syschat` state, limits (`BF_SIZE`, `SYSCHAT_MAX_BUFFERS`, ...).
- `srcs/utils/network.c` — socket connect, TLS-or-plain choice and fallback.
- `srcs/utils/tls.c` — mbedTLS glue (`tls_connect/send/recv/close`).
- `srcs/utils/server.c` — parse and dispatch server lines (PING, JOIN, PRIVMSG, numerics...).
- `srcs/utils/commands.c` — `/` commands and help list (`commands_handle_cmd_list`).
- `srcs/utils/buffers.c` — per-channel/per-PM buffers, unread flag, history.
- `srcs/utils/screen.c` — drawing, tab bar, line wrap, ANSI.
- `srcs/utils/error.c` — `error_print`, `error_exit`, `error_tls_exit`.
- `srcs/segfault_catcher/` — SIGSEGV handler; disabled by `-DSYSCHAT_NO_CATCHER` (debug builds).
- Detailed notes: `docs/agents/tls.md` (TLS and exit codes), `docs/agents/pitfalls.md` (sharp edges).

## Conventions
- Functions in `srcs/utils/*.c` are prefixed by module (`network_`, `tls_`, `server_`, `commands_`, `buffers_`, `screen_`).
- Static helpers are `static` and live at the top of the file that uses them.
- Errors: fatal startup or connect failure goes through `error_exit(syschat, code)`; TLS-required failure goes through `error_tls_exit` (exit 10). Do not add bare `exit()` outside `commands_handle_exit` and the error helpers.
- Nothing is ever sent in clear when `--tls-required` is set. Do not add a code path that bypasses this.
- Do not print a session as "TLS" unless `tls_summary` says it is encrypted.

## Boundaries
- Always: run `./build.sh` and `tests/run_all.sh` before saying a change works; quote the real output.
- Ask first: adding a dependency, changing the exit codes, changing the default TLS behaviour on port 6697, changing the README usage text.
- Never: merge to `master`; run `SYSCHAT_NET_TESTS=1` unprompted; commit `SYSCHAT_DEBUG` trace files (they hold decrypted traffic); remove `-Werror`.

## Pointers
- User-facing behaviour and flags: `README.md`.
- TLS flags, exit codes, strict mode: `docs/agents/tls.md`.
- Toolchain and debugging gotchas: `docs/agents/pitfalls.md`.

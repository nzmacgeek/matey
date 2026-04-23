# Agent Instructions for matey (BlueyOS getty)

> These instructions apply to **every** Copilot coding agent working in this
> repository.  Read this file before starting any task.

---

## 1. What matey does

matey is the getty / login manager for BlueyOS.  It:

1. Opens the TTY device given on the command line (e.g. `/dev/tty1`).
2. Claims a new session (`setsid`) and controlling terminal (`TIOCSCTTY`).
3. Displays the login banner and reads the username.
4. For `root`: reads and verifies the password in-process (hardcoded constant
   `ROOT_LOGIN_PASSWORD` in `matey.c`; no shadow file for root).
5. **Forks a child** to exec the login shell; the **parent waits** with
   `waitpid` so that when the shell exits matey re-displays the login prompt
   without requiring claw to restart it.

**Never replace the fork+wait pattern with a bare `execl`.** If you do, matey
dies when the shell exits, forcing claw to restart it (slow, ugly, loses
terminal state).

---

## 2. Terminal lifecycle — critical rules

### Baseline termios

`setup_terminal()` (called once at startup) applies canonical/echo mode and
saves the pre-setup state into `saved_termios`.

After `setup_terminal()`, **save the canonical state once** into
`canon_termios` (a separate variable).  Use `canon_termios` to restore the
terminal after the shell child exits.  Do **not** re-call `setup_terminal()`
for restoration — bash may have left the terminal in raw mode and a fresh
`tcgetattr` would save that bad state.

```c
// Correct restore sequence after child exits:
tcsetattr(STDIN_FILENO, TCSANOW, &canon_termios);
```

### Foreground process group (TTY job control)

After `fork()`:

1. **Child**: `setpgid(0, 0)` to create its own process group, then reset
   signals to `SIG_DFL` (see below), then `exec`.
2. **Parent**: `setpgid(child_pid, child_pid)` (race-proof), then
   `ioctl(STDIN_FILENO, TIOCSPGRP, &child_pgid)` to hand the terminal to the
   child's pgrp.
3. After `waitpid`: `ioctl(STDIN_FILENO, TIOCSPGRP, &my_pgid)` to reclaim.

### SIGTTOU / SIGTTIN

Ignore both in the **parent** for the entire lifetime of the process (matey
is a background session manager and must never be stopped by terminal I/O
signals):

```c
signal(SIGTTOU, SIG_IGN);
signal(SIGTTIN, SIG_IGN);
```

**In the child**, reset ALL job-control signals to `SIG_DFL` *before* `exec`
so that the shell inherits clean signal disposition:

```c
// child, before exec:
signal(SIGTTOU, SIG_DFL);
signal(SIGTTIN, SIG_DFL);
signal(SIGINT,  SIG_DFL);
signal(SIGQUIT, SIG_DFL);
signal(SIGHUP,  SIG_DFL);
signal(SIGPIPE, SIG_DFL);
```

Failing to do this causes bash job control to malfunction (background jobs
won't stop on terminal I/O).

---

## 3. Verbosity and debug output

matey accepts `-v` / `--verbose` flags and `VERBOSE=N` environment variable
(set by claw from the kernel `verbose=` boot arg).

| Level | Meaning |
|---|---|
| 0 | quiet — errors + lifecycle notices only |
| 1 | info — operational detail |
| 2 | debug — all trace messages including `MATEY_DBG` |

**`MATEY_DBG(fmt, ...)` must only write to the TTY when `g_verbose >= 2`.**
The macro always emits a structured log entry (via `log_debug`) regardless.

```c
// Correct definition:
#define MATEY_DBG(fmt, ...) do { \
    if (g_verbose >= 2) { \
        char _m[512]; \
        snprintf(_m, sizeof(_m), "[matey dbg %s:%d] " fmt "\r\n", \
                 __FILE__, __LINE__, ##__VA_ARGS__); \
        write_str(_m); \
    } \
    log_debug("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0)
```

**Rule**: never add a `write_str` / `printf` call that is unconditional (no
`g_verbose` guard) unless it is a user-visible prompt, banner, or error
message.  Debug-only output must always be gated on `g_verbose >= 2`.

### Claw service verbosity

matey claw service files (`/etc/claw/services.d/matey@ttyN.yml`) must start
matey **without** `-v -v` by default.  Verbose flags must only be present
when explicitly debugging:

```yaml
# Correct (production):
start_cmd: /sbin/matey /dev/tty1

# Only for active debugging sessions:
# start_cmd: /sbin/matey -v -v /dev/tty1
```

---

## 4. Virtual console conventions

matey is started by claw on `/dev/tty1`, `/dev/tty2`, and `/dev/tty3` as
separate service instances (`matey@tty1`, `matey@tty2`, `matey@tty3`).

Each instance opens its TTY device, calls `setsid()` + `TIOCSCTTY`, and
redirects stdio to that device via `dup2`.  After this point all reads and
writes go to the VT-specific kernel buffer — they do **not** share state with
other VTs.

**Rule**: matey must not assume it is on VT1 (the serial console).  Use only
the fd obtained from `open(ttydev, O_RDWR)` and the `dup2`'d stdio fds.

---

## 5. Build and test

```bash
# Build the static i386 ELF:
make

# Install into the baker sysroot (from the baker repo root):
./bin/python baker.py matey

# Rebuild the disk image after changes:
./bin/python baker.py image

# Run the baker test suite (208 tests):
./bin/python -m pytest tests/ -v

# Automated VM boot test (headless):
./bin/python baker.py vm --display none --no-snapshot
```

Validate login/logout changes by booting the VM, logging in as root, running
a command (e.g. `ls /`), typing `exit`, and confirming the login prompt
re-appears without a claw restart delay.

---

## 6. Repo memory hygiene

When you discover a new fact about matey that would help future agents, call
`store_memory` with appropriate subject, fact, citations, and reason fields.

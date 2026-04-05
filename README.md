# matey
getty implementation for BlueyOS

---

> ## ⚠️ IMPORTANT DISCLAIMER
>
> **This is a VIBE CODED, AI-GENERATED RESEARCH PROJECT.**
>
> matey was created as a component of [BlueyOS](https://github.com/nzmacgeek/biscuits),
> an experimental Linux-like OS built by AI models.
>
> **DO NOT USE THIS IN PRODUCTION.**

---

## What is matey?

**matey** is a [getty](https://en.wikipedia.org/wiki/Getty_(Unix)) implementation
for BlueyOS. It manages the terminal login sequence:

1. Opens the TTY device (or uses the inherited stdio fds)
2. Configures the terminal
3. Displays the BlueyOS ASCII banner and optionally `/etc/issue`
4. Reads the login username
5. Execs `/sbin/login` (falling back to `/bin/sh`)

The binary is built as an **i386 ELF** and linked **statically against
[musl-blueyos](https://github.com/nzmacgeek/musl-blueyos)** by default,
with support for a dynamic build as well.

---

## Prerequisites

- `gcc` with `-m32` support (`gcc-multilib` on Debian/Ubuntu)
- `binutils` (`ld`, `ar`)
- `git`
- A musl-blueyos sysroot for i386 — see _Building musl-blueyos_ below

```bash
# Debian/Ubuntu
sudo apt-get install -y gcc-multilib binutils git
```

---

## Building

### 1. musl-blueyos sysroot

matey is built against [musl-blueyos](https://github.com/nzmacgeek/musl-blueyos),
a BlueyOS-flavoured fork of musl libc.

**On a BlueyOS build host** the sysroot is already installed at
`/opt/blueyos-sysroot` — the Makefile detects this automatically and no
extra steps are needed.

**On a fresh host** run the helper script to clone and build it:

```bash
make musl                        # clones musl-blueyos, installs into build/musl/
# or with a custom prefix:
make musl MUSL_PREFIX=/opt/blueyos-sysroot
```

### 2. Build matey

```bash
make                                        # static i386 ELF (default)
make static                                 # same, explicit
make dynamic                                # dynamically linked i386 ELF
make DEBUG=1                                # debug build (-g -O0)
make MUSL_PREFIX=/opt/blueyos-sysroot       # explicit sysroot path
make clean                                  # remove build/
```

Output files:

| Target          | Output                    | Description              |
|-----------------|---------------------------|--------------------------|
| `make` / `static` | `build/matey`           | static i386 ELF (musl)   |
| `make dynamic`  | `build/matey-dynamic`     | dynamic i386 ELF (musl)  |

---

## Usage

```
matey [tty-device]
```

| Invocation            | Description                                 |
|-----------------------|---------------------------------------------|
| `matey`               | Use the inherited stdin/stdout (console)    |
| `matey /dev/tty1`     | Open and take control of `/dev/tty1`        |

Typically called by init for each virtual console, e.g.:

```
/sbin/matey /dev/tty1
```

---

## Integration with BlueyOS (biscuits)

Place the compiled `build/matey` binary at `/sbin/matey` inside the
BlueyOS disk image. Init will invoke it on each console at boot.

See the [biscuits](https://github.com/nzmacgeek/biscuits) repo for the
full BlueyOS build system and disk image creation.

---

## Character mapping

| Component | Character   | Quote                              |
|-----------|-------------|------------------------------------|
| matey     | Bandit      | "G'day! Come on in!"               |
| login prompt | Bandit   | "Who's playing today?"             |

Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
licensed by BBC Studios. This is an unofficial fan/research project with
no affiliation to Ludo Studio or the BBC.

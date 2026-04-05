# matey - getty implementation for BlueyOS
# "Let's play!" - Bluey, Season 1
#
# Targets:
#   make              - static i386 ELF build against musl (default)
#   make static       - same as above, explicit
#   make dynamic      - dynamically linked i386 ELF build against musl
#   make musl         - clone nzmacgeek/musl-blueyos and build for i386 into $(MUSL_PREFIX)
#   make clean        - remove build artefacts
#
# Variables (override on command line):
#   MUSL_PREFIX       - path to an installed musl-blueyos sysroot
#                       Defaults to /opt/blueyos-sysroot when that directory
#                       exists, otherwise falls back to build/musl.
#   BUILD_DIR         - output directory (default: build)
#   DEBUG=1           - enable debug flags (-g -O0 -DDEBUG)
#
# Quick start on a BlueyOS build host (sysroot at /opt/blueyos-sysroot):
#   make                              # MUSL_PREFIX auto-resolves to /opt/blueyos-sysroot
#
# Quick start on a fresh host:
#   make musl                         # clones musl-blueyos and builds into build/musl/
#   make                              # builds matey (static i386 ELF)
#
# Or with a custom musl sysroot:
#   make MUSL_PREFIX=/path/to/sysroot
#
# Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
# licensed by BBC Studios. This is an unofficial fan/research project.
# ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️

# ---------------------------------------------------------------------------
# Directories and tool paths
# ---------------------------------------------------------------------------
BUILD_DIR ?= build

# Prefer the system-wide BlueyOS sysroot (/opt/blueyos-sysroot) when present;
# this is where BlueyOS build hosts install musl-blueyos by default.
# Fall back to the local build/musl tree for fresh/CI environments.
BLUEYOS_SYSROOT ?= /opt/blueyos-sysroot
ifeq ($(shell [ -d $(BLUEYOS_SYSROOT) ] && echo yes),yes)
  MUSL_PREFIX ?= $(BLUEYOS_SYSROOT)
else
  MUSL_PREFIX ?= $(BUILD_DIR)/musl
endif

MUSL_INCLUDE := $(MUSL_PREFIX)/include
MUSL_LIB     := $(MUSL_PREFIX)/lib

# ---------------------------------------------------------------------------
# Source / output
# ---------------------------------------------------------------------------
SRC     := matey.c
TARGET  := $(BUILD_DIR)/matey

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------
CC := gcc

# ---------------------------------------------------------------------------
# Base compiler flags — i386 ELF, strict warnings, no stack protector
# ---------------------------------------------------------------------------
BASE_CFLAGS := \
    -m32 \
    -std=gnu11 \
    -Wall \
    -Wextra \
    -Wno-unused-parameter \
    -fno-stack-protector

ifeq ($(DEBUG),1)
  BASE_CFLAGS += -g -O0 -DDEBUG
else
  BASE_CFLAGS += -O2
endif

# ---------------------------------------------------------------------------
# Linker flags common to both static and dynamic builds
# ---------------------------------------------------------------------------
BASE_LDFLAGS := \
    -Wl,-m,elf_i386 \
    -Wl,-Ttext,0x00400000

# ---------------------------------------------------------------------------
# Static build flags (default)
# ---------------------------------------------------------------------------
STATIC_CFLAGS  := $(BASE_CFLAGS) -fno-pic -isystem $(MUSL_INCLUDE)
STATIC_LDFLAGS := $(BASE_LDFLAGS) -static -no-pie -L$(MUSL_LIB)

# ---------------------------------------------------------------------------
# Dynamic build flags
# ---------------------------------------------------------------------------
DYNAMIC_CFLAGS  := $(BASE_CFLAGS) -fPIC -isystem $(MUSL_INCLUDE)
DYNAMIC_LDFLAGS := $(BASE_LDFLAGS) -L$(MUSL_LIB) -Wl,-rpath,$(MUSL_LIB)

# ---------------------------------------------------------------------------
# Phony targets
# ---------------------------------------------------------------------------
.PHONY: all static dynamic musl musl-check clean help

.DEFAULT_GOAL := all

# ---------------------------------------------------------------------------
# Helper: verify musl is present before trying to build
# ---------------------------------------------------------------------------
define check_musl
	@if [ ! -d "$(MUSL_INCLUDE)" ] || [ ! -f "$(MUSL_LIB)/libc.a" ]; then \
		echo ""; \
		echo "  [MUSL] musl sysroot not found under $(MUSL_PREFIX)"; \
		echo "         expected:"; \
		echo "           $(MUSL_INCLUDE)/  (headers)"; \
		echo "           $(MUSL_LIB)/libc.a  (static library)"; \
		echo ""; \
		echo "  To build musl for BlueyOS:"; \
		echo "    ./tools/build-musl.sh --prefix=$(MUSL_PREFIX)"; \
		echo "  Or point at an existing sysroot:"; \
		echo "    make MUSL_PREFIX=/path/to/musl-sysroot"; \
		echo ""; \
		exit 1; \
	fi
endef

# ---------------------------------------------------------------------------
# all / static — static i386 ELF linked against musl
# ---------------------------------------------------------------------------
all: static

static: $(BUILD_DIR) musl-check
	$(CC) $(STATIC_CFLAGS) $(SRC) $(STATIC_LDFLAGS) -lc -o $(TARGET)
	@echo ""
	@echo "  [LD]  $(TARGET) (i386 ELF, static musl)"
	@echo ""

# ---------------------------------------------------------------------------
# dynamic — dynamically linked i386 ELF against musl
# ---------------------------------------------------------------------------
dynamic: $(BUILD_DIR) musl-check
	$(CC) $(DYNAMIC_CFLAGS) $(SRC) $(DYNAMIC_LDFLAGS) -lc -o $(TARGET)-dynamic
	@echo ""
	@echo "  [LD]  $(TARGET)-dynamic (i386 ELF, dynamic musl)"
	@echo ""

# ---------------------------------------------------------------------------
# musl — clone nzmacgeek/musl-blueyos and build for i386
# ---------------------------------------------------------------------------
musl:
	@bash tools/build-musl.sh --prefix=$(MUSL_PREFIX)

# ---------------------------------------------------------------------------
# musl-check — internal target that runs the check macro
# ---------------------------------------------------------------------------
.PHONY: musl-check
musl-check:
	$(call check_musl)

# ---------------------------------------------------------------------------
# Build output directory
# ---------------------------------------------------------------------------
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# ---------------------------------------------------------------------------
# clean
# ---------------------------------------------------------------------------
clean:
	@if [ -z "$(BUILD_DIR)" ] || [ "$(BUILD_DIR)" = "/" ] || [ "$(BUILD_DIR)" = "." ]; then \
		echo "  [CLEAN] Refusing to remove unsafe BUILD_DIR='$(BUILD_DIR)'"; exit 1; \
	fi
	rm -rf -- "$(BUILD_DIR)"
	@echo "  [CLEAN] Build artefacts removed from $(BUILD_DIR)."

# ---------------------------------------------------------------------------
# help
# ---------------------------------------------------------------------------
help:
	@echo "matey — getty for BlueyOS"
	@echo ""
	@echo "  make              build static i386 ELF (default)"
	@echo "  make musl         clone musl-blueyos and build for i386 (into MUSL_PREFIX)"
	@echo "  make static       same as above, explicit"
	@echo "  make dynamic      build dynamically linked i386 ELF"
	@echo "  make clean        remove build artefacts"
	@echo ""
	@echo "Variables:"
	@echo "  MUSL_PREFIX=...   path to musl sysroot (default: $(BUILD_DIR)/musl)"
	@echo "  BUILD_DIR=...     output directory      (default: build)"
	@echo "  DEBUG=1           enable debug build"
	@echo ""
	@echo "Example:"
	@echo "  make MUSL_PREFIX=/opt/blueyos-sysroot"

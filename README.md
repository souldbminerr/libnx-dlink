# libnx-dlink

a dynamic linker lib for libnx

feel free to use this however much you want, i dont care

the original functionality was ported over from libtransistor using its methods and code as reference, license is as follows:

ISC License

Copyright (c) 2017, ReSwitched Team

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

## Build

Prerequisites: devkitPro + devkitA64 + libnx (`DEVKITPRO` set), GNU make.

```sh
# release + debug libs, outputted at lib/libdlink.a, lib/libdlinkd.a
make -j8

# Testing NRO, needs the lib built first
make -C test -j8

# loadable test module (the NRO the smoke test dlopen()s)
make -C test/module -j8
```

To use from another project, add the repo root to your library path so the
compiler sees include and the linker sees lib:

```make
LIBDIRS += /path/to/libnx-dlink
LIBS += -ldlink
```

Or put it in as a submodule.

## Usage

```c
#include <dlink/dlink.h>

/* Tell the linker where your modules are located (NULL falls back to sdmc:/lib/). */
dlink_set_library_dir("sdmc:/switch/myapp/modules/");

void *mod = dlopen("mymod", RTLD_GLOBAL | RTLD_NOW);
if (!mod) printf("load failed: %s\n", dlerror());

int (*entry)(void) = dlsym(mod, "mod_entry");
if (entry) entry();

dlclose(mod);
```

The `dlink_*` equivalents do the same thing as the unix-style ones.

## Module formats

- **DNRO** - Dynamic NRO, a NRO with a header and extra data for dynamic linking. 

## DNRO and elf2dnro

elf2dnro.py converts a linked ELF into a dnro (Dynamic Nintendo
Relocatable Object). This serves the needs of dynamic links.

## Requirements

You need a host process handle (envGetOwnProcessHandle(), passed by HBL) and ldr:ro access.

Without these, loading fails.

## Example module

example/ is a minimal loadable module: make -C example

Modules must be import-free
Relocation tables are used verbatim from the link, RELA and packed RELR
(RELR=36, RELRSZ=35, RELRENT=37).

## Host imports

Modules that call libc, libnx, or host functions link them as undefined imports instead

- Link the module -shared against tools/module.specs (a copy of
  switch.specs without -pie)
- Keep libc/libnx archives out (-nodefaultlibs, empty LIBS) so every
  library call stays undefined. A minimal lib (like example-host/) provides
  only _start/MOD0 (it's unused and only there to actually link)
- Export the entries via a --dynamic-list file, as usual.

On the host side, expose what modules may import:

- Link the host with --export-dynamic (or a --dynamic-list).
- Call dlink_register_host once
- For functions only modules use (never linked otherwise), name each one
  once with DLINK_PROVIDE

example-host demonstrates all of these.

## Test app

The test folder builds a minimal test app for this functionality, along with a minimal test library.
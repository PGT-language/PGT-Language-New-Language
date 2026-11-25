# PGT-Language-New-Language

## Low-level features

- `asm { }` blocks (inside functions and at top level) are compiled at build time into LLVM inline/module assembly (Intel syntax). Requires `#use "asm"`.
- `c { }` blocks hold inline C compiled by clang and linked in. Requires `#use "c"`.
- `pgt build <dir> --freestanding` (or `target kernel` in `pgt.mod`) builds a freestanding x86-64 kernel; see `syntax.md` ("Режим ядра") and `std/README.md` (`kernel`).
- `os/` is a demo kernel booted by Limine: `bash os/build.sh` builds `os/os.elf` and `os/os.iso`, `bash os/run.sh [--headless]` runs it in VirtualBox (VM `pgt-os`), `bash os/verify.sh` boots it headless and checks the serial log.

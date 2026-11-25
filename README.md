# PGT-Language-New-Language

## Low-level features

- `asm { }` blocks (inside functions and at top level) are compiled at build time into LLVM inline/module assembly (Intel syntax). Requires `#use "asm"`.
- `c { }` blocks hold inline C compiled by clang and linked in. Requires `#use "c"` (`std/c`: a C tokenizer, block checker, libc bindings).
- `pgt build <dir> --freestanding` (or `target kernel` in `pgt.mod`) builds a freestanding x86-64 kernel; see `syntax.md` ("Режим ядра").
- `pgt-os-test-kernel/` is a demo kernel project written in PGT (packages `cpu`, `ports`, `serial`, `console`, `gdt`, `idt`, `pic`, `pit`, `keyboard`, `pmm`, `limine`, `mem`, `text`), booted by Limine: `bash pgt-os-test-kernel/build.sh` builds `os.elf` and `os.iso`, `bash pgt-os-test-kernel/run.sh [--headless]` runs it in VirtualBox (VM `pgt-os`), `bash pgt-os-test-kernel/verify.sh` boots it headless and checks the serial log.

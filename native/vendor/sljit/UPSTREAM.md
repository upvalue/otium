# sljit

Vendored from https://github.com/zherczeg/sljit at commit
`39c508d1b8fbae332d60c7450db20236913089da`: the contents of `sljit_src/`,
without the backends Otium doesn't target. x86, arm64 and RISC-V remain.

The upstream source is distributed under the BSD-2-Clause license in
`LICENSE`.

No local changes. The native compiler builds it with its own executable
memory allocator (`SLJIT_EXECUTABLE_ALLOCATOR` 0) and reads
`struct sljit_compiler`'s `local_size` after `sljit_emit_enter` to learn frame
sizes for its frame descriptors.

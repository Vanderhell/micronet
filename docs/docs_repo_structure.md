# Repository Structure

**Project:** micronet  
**License:** MIT  
**Target platforms:** ESP32, Linux, Windows

---

## Overview

This document describes the repository layout, the main source groups, and the cleanliness rules used by the project.

---

## Top-Level Layout

```text
micronet/
|-- docs/          Project documentation
|-- include/       Public headers
|-- src/           Core implementation
|-- hal/           Platform abstraction layer
|-- tests/         Host-side tests
|-- examples/      Desktop and ESP32 examples
|-- arduino/       Arduino port
|-- CMakeLists.txt Root build file
|-- idf_component.yml  ESP-IDF component metadata
|-- LICENSE
`-- README.md
```

---

## Source Layout

```text
src/
|-- transport/
|-- security/
|-- network/
|-- data/
|-- protocol/
`-- micronet.c
```

Each block has its own header and implementation files, plus dedicated tests.

---

## Build Outputs

Generated files do not belong in version control:

- `build/`
- `dist/`
- object files, libraries, binaries, ELF files, and maps
- ESP-IDF generated configuration files

The repository already contains example build folders for local development, but they should not be committed.

---

## Coding Rules

- Keep the public API stable
- Keep each subsystem in its own directory
- Add tests for each block
- Add documentation for each block
- Prefer clear boundaries between transport, security, network, data, and protocol layers

---

## Release Notes

Planned version milestones:

- `v0.1.0` - transport block complete and tested
- `v0.2.0` - security block
- `v0.3.0` - network block
- `v0.4.0` - data block
- `v0.5.0` - protocol block
- `v0.6.0` - public API
- `v1.0.0` - first stable release with ESP32 support

---

## Clean Repo Rules

1. No binaries, only source and text files
2. No IDE files unless they are explicitly needed
3. No generated build outputs
4. One block per directory under `src/`
5. One test file per block in `tests/`
6. One document per block in `docs/`
7. Commit messages should use a consistent format such as `feat:`, `fix:`, or `docs:`


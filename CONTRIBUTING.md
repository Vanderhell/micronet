# Contributing

Micronet is a protocol-heavy project, so changes should be deliberate and well tested.

## Before You Start

- check whether the change affects wire format, interoperability, or release packaging
- open an issue first for large features or behavior changes
- keep commits focused

## What To Include

- tests for the changed behavior
- docs updates when the public API or usage changes
- example updates when the change affects how users try the project

## Local Checks

- `cmake --build build --parallel`
- `ctest --test-dir build --output-on-failure`
- compile the relevant Arduino or ESP32 example if your change touches that path

## Reporting Issues

Use GitHub Issues for bugs, regressions, and release blockers:

- https://github.com/Vanderhell/micronet/issues

When filing an issue, include:

- what you expected
- what actually happened
- the exact build or runtime command
- logs or screenshots when relevant

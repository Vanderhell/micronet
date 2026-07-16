# Progress

State as of 2026-07-16:

- Stage 3 is complete and verified locally.
- The transport layer now has bounded RX deduplication, retry preflight checks, and internal counters for send/receive/retry health.
- Transport tests were rewritten around a fake HAL and fake clock so the suite is deterministic.

Verification run:

- `cmake --build build --config Debug`
- `ctest --test-dir build -C Debug --output-on-failure`

Notes:

- Duplicate transport packets are ACKed but not re-delivered to the RX ring.
- `retry_count == 0` now means one send without retry state.
- Retry exhaustion no longer stops other due retry entries from being processed in the same tick.

Next likely step:

- Proceed to Stage 4 only if requested.

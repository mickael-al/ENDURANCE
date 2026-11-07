# Networking regression checks

Run from the repository root:

```bash
python3 Tests/Network/check_all.py --sanitize --sdk-check
```

The suite requires Python 3 and a C++17 compiler (`CXX`, default `g++`). It runs without a Steam account, server or graphical context. Omit `--sanitize` if the compiler does not provide AddressSanitizer and UndefinedBehaviorSanitizer. `--sdk-check` additionally compiles the transport and session code against the bundled Steam SDK headers.

The checks execute the production RPC decoder, transport and session implementations. Deterministic Steam/libhv substitutes exercise malformed packets, RPC direction and identity checks, admission, limits, reconnects, callbacks, cancellation, invitations, lobby metadata/languages, host migration and optional resource verification. Separate checks cover launch arguments and canonical SHA-256 digests using independently generated Python `hashlib` vectors.

ENDURANCE-specific cases verify lobby and admission separation from another game sharing the development App ID, the default of two players, configurable start thresholds and resource verification remaining disabled until explicitly enabled. Cancellation after enabling resource verification keeps admission closed.

These tests do not verify actual Steam connectivity, NAT traversal, live invitations or game-state synchronization.

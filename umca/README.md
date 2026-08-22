# UMCA MVP Core

This directory is the single source of truth for the platform-independent
UMCA implementation described by the four documents in `../docs/`.

The current implementation covers the first development gates:

- fixed v0.3.3/v0.1.2 wire frame, strict Flags/QoS/type checks and CRC32C;
- ASCII Topic validation and FNV-1a IDs;
- static multi-instance Contexts, Pub/Sub and sequence filtering;
- Discovery profile with ANNOUNCE/HEARTBEAT/TEARDOWN and bounded node table;
- POSIX PAL reference and fixed-depth Loopback PHY;
- host unit and three-node integration tests.

The Core has no openvela, POSIX, PHY-driver or ai_agent includes. The POSIX and
Loopback code are separate adapters. An openvela PAL and an ai_agent provider
adapter are present outside the Core; the provider remains an explicit runtime
hook until the application supplies a real Context/PHY service instance.

## Host build

```sh
cmake -S contest2026_417_duwen/umca -B /tmp/umca-build \
  -DUMCA_PROFILE=DISCOVERY -DBUILD_TESTING=ON
cmake --build /tmp/umca-build -j2
ctest --test-dir /tmp/umca-build --output-on-failure
```

The `MINIMAL` profile excludes discovery, node-table and RX-sequence source
files at the CMake target level. `CONTEST` additionally enables the thread-safe
configuration and therefore requires a host-provided mutex in `umca_platform_t`.

## Scope boundary

The MVP deliberately does not implement fragmentation, RPC, ACK/ERROR frames,
QoS 1--3, gateways, security, dynamic allocation, or runtime topic changes.

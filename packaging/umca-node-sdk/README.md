# UMCA Node SDK v0.1.2

This is a standalone source snapshot for developers integrating Sensor,
Actuator, or ESP32-S3 gateway nodes with the UMCA contest system.

Start with:

1. `docs/UMCA_Portable_Node_Development_Guide.md`
2. `docs/UMCA_Node_Integration_Guide.md`
3. `docs/UMCA_Protocol_Specification_v0.1.2.md`

The package contains the platform-independent C99 Core, UART and Loopback PHYs,
the POSIX PAL reference, application payload codecs, host tests, and Windows
reference tools. It intentionally excludes board firmware, IDE settings,
hardware PDFs, credentials, captured data, and build artifacts.

## Build and test

```sh
cmake -S . -B build -DUMCA_PROFILE=DISCOVERY -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Expected test count is 3 for `MINIMAL` and 4 for `DISCOVERY` or `CONTEST`.
`CONTEST` requires host thread support for its integration test.

Verify the package before use:

```sh
sha256sum -c MANIFEST.sha256
```

On Windows PowerShell, use `Get-FileHash` for individual files or run the
verification command from Git Bash/WSL.

## Integration boundary

Do not transmit C structure memory images. Use `demo/demo_codec.c` or an
equivalent implementation of the documented network-byte-order payloads.
Do not enable unsupported message types or QoS modes. Any uncertain wire,
identity, Topic, routing, or credential requirement must be confirmed with the
system integrator before implementation.

The final system validation completed real ESP32-S3 three-port routing,
Wi-Fi/TLS, the MiMo cloud Provider, a physical GD32 and Actuator, and the
Windows-hosted Sensor node. Those board-specific firmware implementations and
credentials are intentionally outside this portable SDK snapshot. The Windows
simulator remains available for hardware-independent regression testing.

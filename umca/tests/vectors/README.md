# UMCA frozen vectors

These byte strings are copied from section 20 of
`docs/UMCA_Protocol_Specification_v0.1.2.md`. They are inputs and expected
outputs for tests; the implementation must not generate its own expectations.

* CRC32C(`123456789`) = `e3069283`.
* Topic IDs:
  * `/sensors/temperature` = `0fc95cb0`
  * `/events/temperature/threshold` = `5389c486`
  * `/actuators/fan/command` = `c72fe51a`
  * `/actuators/fan/state` = `6d3f9ebe`

The complete frame vectors remain in `tests/unit/test_umca.c` as byte arrays so
the unit test compares the encoded frame byte-for-byte.

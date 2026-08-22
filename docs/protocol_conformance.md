# UMCA protocol conformance notes

The host tests use the frozen vectors in protocol specification v0.1.2 rather
than deriving expected values from the codec under test. The current checks
cover:

- CRC32C Castagnoli standard vector;
- all four frozen Topic IDs;
- the empty DATA frame byte-for-byte;
- rejection of non-zero Flags;
- ASCII Topic restrictions and traversal/empty-segment rejection;
- strict frame length, CRC, Version, QoS and message semantics in the codec.

The three-node integration test uses the fixed simulation DevIDs and Boot IDs
from the specification and exercises ANNOUNCE, DATA delivery and deterministic
offline timeout handling over three independent Loopback PHY instances.

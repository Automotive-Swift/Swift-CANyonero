# Receive allocation failure regression

Run `make test-memory` with a C++20 compiler. `make test` runs this check before the existing SwiftPM suite.

The standalone executable replaces `operator new` and deliberately rejects the 4095-byte receive reservation. Isolation keeps this allocator out of XCTest, firmware and library products. All eight classic/FD, strict/defensive and standard/extended cases must reject the First Frame without propagating `bad_alloc`, then complete a 4095-byte transfer on the same receiver after allocation is restored.

This verifies failure of the large reservation, not total heap exhaustion. Existing small frame, action/deque and caller allocations can still fail. Builds without C++ exceptions retain their allocator failure policy. The normal successful path still reserves once and moves the completed buffer.

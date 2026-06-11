---
name: test-generation-assistant
description: 'Generate server tests for unit, network, and boundary scenarios. Use when creating tests for protocol routing, room state transitions, battle flow, out-of-order messages, duplicate requests, invalid phase calls, leave-room-after-send, and other regression cases.'
argument-hint: 'What behavior should the tests cover?'
---

# Test Generation Assistant

Generate focused tests for this server project across unit, network, and edge-case scenarios.

## When to Use

- User asks to add or expand tests
- User wants coverage for protocol routing, room state, shop/map/battle flow, or error paths
- User wants adversarial scenarios such as out-of-order messages, duplicate requests, invalid phase calls, or continued sending after leave/logout
- User wants tests that protect a recent bug fix or behavior change

## Scope Rules

- Use unit tests for pure logic, deterministic state transitions, and validation helpers
- Use network tests for TCP framing, JSON payload handling, dispatch, and multi-service interaction
- Use boundary tests for illegal phase access, repeated operations, stale state, race-like ordering, and post-leave/post-logout behavior
- Prefer the narrowest test level that can prove the behavior

## Workflow

1. Identify the behavior anchor
   - Find the exact function, command, phase gate, or request path being protected
   - Determine whether the behavior is owned by protocol, room state, battle logic, or channel dispatch

2. Classify the test type
   - If the code is pure or nearly pure, write a unit test
   - If the code depends on TCP framing, request/response sequencing, or multi-step message flow, write a network test
   - If the bug is about invalid order, repeated action, or phase mismatch, add an explicit boundary-case test

3. Derive test cases from invariants
   - Happy path first, then the smallest set of failing paths that would catch regressions
   - Cover out-of-order input, duplicate input, stale input, wrong-phase input, and post-exit input when relevant
   - Verify both returned envelopes and the absence of a response when the protocol expects async-only behavior

4. Match the existing test style
   - Reuse the current test helpers and fixtures in `tests/unit_tests/` and `tests/network_tests/`
   - Keep test names short, behavior-oriented, and specific to the regression
   - Prefer assertions that prove the state transition, emitted envelope, or rejected command directly

5. Write the minimum useful test set
   - Add one test that proves the intended behavior
   - Add one or more guard tests that would fail if the bug returns
   - Avoid duplicating the same assertion across multiple test files unless the execution layer differs

6. Validate the slice
   - Run the narrowest relevant tests first
   - If the change touches shared protocol or dispatch code, run the nearest unit and network tests together
   - If the failure indicates a broader invariant problem, step one layer outward and add the missing guard test

## Scenario Checklist

Use these prompts when deciding what to generate:

- Out-of-order message arrives before the required setup command
- Duplicate request replays after success or after failure
- Invalid phase call is accepted when it should be rejected
- Client keeps sending after leaving a room or logging out
- Same node, room, or battle action is submitted by different timing paths
- A response should be asynchronous, but a direct response appears anyway
- A state transition succeeds once and must fail on repetition

## Completion Criteria

- The test names explain the behavior they protect
- The regression path fails without the new test
- The happy path still passes
- The test level matches the behavior boundary
- The test captures the actual server contract, not an implementation detail

## References

- `tests/unit_tests/`
- `tests/network_tests/`
- `include/protocol.h`
- `include/room.h`
- `include/battle.h`
- `src/channel.cpp`
- `src/room.cpp`
- `src/battle_room.cpp`
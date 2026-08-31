# Protocol B0 Cycle GREEN Report

## RED evidence

The RED implementation was checked with GitHub Actions run `33333676130`.
That run failed because `prepareB0Cycle` was used without a declaration.

The local ESP32 RED build additionally exposed the stale
`src/main.cpp` call to the removed `OutputController::drainStrengthCommand()`
interface.

The local native command was also run with the required isolated PlatformIO
runtime:

```text
PLATFORMIO_CORE_DIR=.piohome
C:\Users\h\.platformio\penv\Scripts\platformio.exe test -e native
```

Native execution remains environment-blocked because Windows `gcc` and `g++`
are not installed.

## GREEN implementation

`prepareB0Cycle` is declared in `DgLabControl.h` and implemented as a fixed
size helper. It waits for the 100 ms schedule before preparing a command,
does not consume pending strength before that deadline, prepares at most one
strength command per cycle, and does not commit, roll back, update
`lastSendTime`, or allocate dynamically. It copies the supplied wave block
into both A and B frame blocks. A strength command supplies its sequence,
method, and wire strength fields; a wave-only frame clears those fields and
sets `command.valid` to false.

V3 output now performs the controller timeout tick first, then generates and
writes exactly one B0 frame per due 100 ms cycle. Strength and waveform data
share the frame. Successful strength writes commit the prepared command and
show the predicted target strengths as unconfirmed; failed writes roll back
the prepared command so the request remains pending. Matching B1 feedback is
the only response that releases an in-flight command and confirms state, while
all B1 messages still update the displayed actual strengths. Connection and
reset paths remain unconfirmed for DG3. The V2 100 ms behavior is unchanged.

The obsolete immediate `drainStrengthCommand()` call was removed from the
main loop, preserving the loop order: Web handling, BLE events, cleanup,
output scheduling, auto-scan, and delay.

## Verification

- `prepareB0Cycle` and strength-controller behavior are covered by the
  existing native Unity tests in `test/test_dglab_control/test_main.cpp`.
- Local native test execution is blocked by the absent host compiler, as
  recorded above.
- ESP32 verification was run with:

  ```text
  PLATFORMIO_CORE_DIR=.piohome
  C:\Users\h\.platformio\penv\Scripts\platformio.exe run -e esp32dev
  ```

- `git diff --check` is required before commit.

## Reviewer follow-up

The loop now drains BLE events and deferred disconnect cleanup before serving
Web requests, then runs the output cycle, auto-scan, and delay. Strength
requests expose `RequestDisposition` to the Web route: rejected requests are
logged as not sent, queued DG3 requests as queued, and prepared DG3 requests
as waiting for the next cycle. Synchronous DG2 success keeps the existing
adjustment log; actual DG3 transmission remains logged only by the output
cycle after a successful B0 write.

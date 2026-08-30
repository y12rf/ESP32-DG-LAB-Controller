# Protocol Strength Core GREEN Report

## RED evidence

The RED-only test commit was `c536e83`. GitHub Actions run `33332397615`
confirmed the expected failure against the old implementation: the native
test could not compile because `PreparedStrengthCommand` had no
`targetStrengthA/B`, `feedbackSynchronized()` was missing, and
`onStrengthResponse`/`tick` returned `void` instead of the tested `bool`
results. The old implementation also encoded a relative target in the B0
strength field.

The local native command was retried with the configured PlatformIO runtime:

```text
PLATFORMIO_CORE_DIR=.piohome
C:\Users\h\.platformio\penv\Scripts\platformio.exe test -e native
```

It remains environment-blocked because `gcc` and `g++` are not installed on
the workstation. No native pass is claimed locally.

## GREEN implementation

`StrengthController` now keeps one fixed-size intent per channel and separates
prepared command state from new requests arriving before commit. Relative
intents merge as signed direction/magnitude state; an absolute intent replaces
that state, and relative requests after an absolute adjust its clamped target.
Cancellation removes the pending intent. Relative wire values are capped at
200 while `targetStrengthA/B` retain the predicted clamped device targets.
Inactive channel wire fields are zero.

Prepared state is only consumed on commit. A failed write restores the original
intent, while requests arriving during preparation are merged after it. A
successful commit starts the single in-flight B1 wait and marks feedback
unsynchronized. `onStrengthResponse` updates confirmed strengths for every B1,
but only a matching nonzero sequence releases the wait and restores
synchronization. `tick` reports the 500 ms expiry, releases the wait, keeps
feedback unsynchronized, and does not retry the expired command.

## API changes

- Added `PreparedStrengthCommand::targetStrengthA` and `targetStrengthB`.
- Existing `strengthA`/`strengthB` fields remain the B0 wire fields for adapter
  compatibility; relative operations place raw magnitude there.
- Changed `onStrengthResponse(...)` and `tick(...)` to return `bool` indicating
  a matching response or timeout expiry.
- Added `StrengthController::feedbackSynchronized()`.

## Official protocol correspondence

- V3 README B0 sections 27--50: relative increase/decrease fields carry the
  requested magnitude, absolute fields carry the target, and values clamp to
  `0..200`.
- V3 README B1 sections 111--117: every strength change reports actual A/B
  values and matching command sequence numbers identify command feedback.
- V3 README state-machine example sections 174--285: requests accumulate while
  waiting, sequence values are used for command feedback, and cancellation or
  absolute replacement is preserved across cycles.
- Design sections “Strength Intent Rules” and “B1 and Timeout State Machine”:
  one pending intent per channel, a single in-flight command, 500 ms expiry,
  no retry after expiry, and synchronization restored only by valid later
  feedback.

## Verification

- `C:\Users\h\.platformio\penv\Scripts\platformio.exe run -e esp32dev`
  passed; firmware image was linked successfully.
- Native test execution is blocked locally by the absent host compiler as
  documented above; CI RED evidence is recorded without claiming local GREEN.
- `git diff --check` passed for the implementation changes before commit.

# Tests/LibWeb/Integration/TH8 -- TH8 navigation-cancellation race tests

These tests exercise the boundary between a running TH8 script and
browser-level events (navigation, unload, back-forward) that the
single-page `Tests/LibWeb/Text/*` harness cannot reach.

See `../README.md` for prerequisites and how to run.

## Test cases

| #  | File                                                  | State    | Pins                                                  |
|----|-------------------------------------------------------|----------|-------------------------------------------------------|
| 1  | `test_watchdog_meets_navigation_budget.py`            | active   | M2 watchdog fires fast enough that nav completes <= 500 ms while TH8 is in an infinite loop |
| 2  | `test_no_leak_on_navigation_during_eval.py`           | active   | ASAN/LSAN: leak in TH8Context / WebPlatformContext / Interpreter tear-down surfaces at session shutdown |
| 3  | `test_th8_event_handler_self_navigates.py`            | active   | TH8 click handler can set `window.location` via cross-eval without UAF (validates H4 unwind ordering) |
| 4  | `test_sidecar_fetch_resolves_into_dead_document.py`   | partial  | degenerate fail-closed path active; timing-sensitive variant skipped (needs HTTP fixture) |
| 5  | `test_back_forward_across_active_th8.py`              | partial  | E -> B -> back -> forward active; mid-eval back variant skipped (timing-fragile under file://) |
| 6  | `test_kill_switch_under_navigation.py`                | skipped  | needs `window.internals.disableTh8` hook or a Ladybird WebDriver extension; rationale in the file |

## Why budget-based assertions are OK here

Cases 2-6 do not rely on wall-clock measurement; they assert
"completed cleanly" (no crash, no leak, no hang).  Case 1 needs a
numeric bound because the failure mode is "wall-clock watchdog
never fires; nav blocks until step-limit".  The 500 ms budget is
loose enough (250 ms watchdog + IPC + paint slack) to be stable on
slow CI runners while still failing if the watchdog is gone.

When wiring these into CI, run case 1 with a wider budget under
sanitizer / debug builds (factor of 2-4x is reasonable).

## Anti-flakiness notes

- The page-A fixture flips a JS flag via cross-eval BEFORE entering
  the infinite loop so the test driver can poll for readiness
  without a `setTimeout` race.
- `execute_script` is used to read state via the WebDriver
  synchronous-execute route, which blocks until the renderer
  returns the value -- no `time.sleep` polling beyond the readiness
  loop.
- Navigation latency is measured between `navigate(...)` request
  and response on the driver side; this includes IPC overhead.
  Add ~150 ms slack on slow runners (e.g., qemu, sanitizer builds).

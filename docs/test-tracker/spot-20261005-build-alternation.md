# Bench build alternation: why builds right after a successful one failed with missing HAL

Purpose: spontaneous. Issue: #73.

Build only: no board, no probe, no COM port.
The runs below are `bench build` and a throwaway harness that calls bench's own sync and headless command, in Build Trees on `C:` that were deleted afterwards.
The logs are local, so the decisive numbers are copied here.

## Cause

CubeIDE creates empty folders in the Build Tree as the parents of each project's linked files: `CM4/Drivers/STM32WLxx_HAL_Driver`, `CM4/Utilities`, `CM0PLUS/Middlewares/Third_Party/SubGHz_Phy` and the others named in the `linkedResources` of `CM4/.project` and `CM0PLUS/.project` (nine folders).
The repo has none of them, so the `rsync --delete` that precedes every build removed them.
The next CubeIDE session found the folders gone and dropped every link under them from the project:

- `CM4/.project` was saved at 1,282 bytes instead of 6,357, with only the `Common` link left.
- `CM4/.cproject` lost its `Drivers` and `Utilities` source entries.
- `sources.mk` of each configuration lost `Drivers/STM32WLxx_HAL_Driver`, `Utilities` (and `Middlewares/Third_Party/SubGHz_Phy` for CM0+) from its `SUBDIRS`.
  Nothing else in the generated makefiles changed, and the `subdir.mk` files stayed on disk.
- The link then had no HAL objects: 146 CM4 and 423 CM0+ `undefined reference` errors, the same numbers on every failing build.

The next sync restored the repo's `.project` and `.cproject`, so that session read the full project again and recreated the folders.
That is why a build after a failed one passed, and why the two alternated strictly.
The sync was both the cause and the repair.

## Scenarios

| Scenario | Proves or measures |
|---|---|
| none: build only | the host tests below cover the sync; the runs are `bench build` |
| `tools/bench/tests/test_build.py::test_the_parents_of_linked_files_are_found_in_the_project_files` | the folders are read from each project's `.project` |
| `tools/bench/tests/test_build.py::test_sync_keeps_the_folders_cubeide_makes_for_linked_files` | a real rsync keeps them and still deletes a folder the repo dropped (red before the fix) |
| `tools/bench/tests/test_build.py::test_every_build_keeps_its_log_under_a_name_of_its_own` | each build, run and flash included, keeps its log under a unique name |

## Hardware

| Item | Needed |
|---|---|
| none | n/a |

Hands on the bench: none.

## Criteria

The two open questions of the issue.

- [x] Why does the generated makefile of a build after a successful one lose the `Drivers` subdirectory (the `subdir.mk` and `sources.mk` of the configuration)?
  Because the sync deleted the folders CubeIDE made for the linked files, see Cause.
  The `subdir.mk` files are not lost, only the `SUBDIRS` lines of `sources.mk`.
  Proven by run 3 (folders removed by hand, no rsync, fails) and by runs 6 and 7 (protected sync, passes).
- [x] When `_check_images` finds a stale image, `bench` rebuilds clean once for all stale configurations in one CubeIDE call: is that multi-configuration `-cleanBuild` what breaks? A single-class `--clean` build passed once.
  No.
  A multi-configuration `-cleanBuild` of C2 and C3 fails exactly when any other build does (run 4, legacy sync after a pass) and passes with the protected sync (run 5, and step 9 of run 7).
  The single-class `--clean` build that passed was a build after a failed one.
  The stale-image rebuild is a second CubeIDE call with no sync between the two calls, so it cannot delete the folders.

## Runs

Legacy sync: the command before the fix.
Protected sync: the fix, commit "fix(bench): the sync keeps the folders CubeIDE makes for linked files" (builds from it carry `df5bd6e` in their Build ID).

| Date (UTC) | Scenario | Build ID | Verdict | Record | Notes |
|---|---|---|---|---|---|
| 2026-10-05 11:21 | run 1: C2, 6 builds in a fresh tree, legacy sync, nothing changed | `a419c9b`, `b1759a2` | pass, FAIL, pass, FAIL, pass, FAIL | none, numbers here | the failing builds: 146 and 423 errors, 193 `undefined reference` lines, no HAL file compiled. The only difference between the makefiles of a passing and a failing state is the `SUBDIRS` lines above. |
| 2026-10-05 11:26 | run 2: same tree, plain build, `--no-sync`, plain build | `b1759a2` | pass, pass, FAIL | none, numbers here | the build without a sync, right after a pass, passed: the sync is what breaks it. The dry run before the failing build listed `*deleting` for the nine folders. After that failing session `CM4/.project` was 1,282 bytes. |
| 2026-10-05 11:34 | run 3: C2, fresh tree: plain, `--no-sync`, `--no-sync` with the nine empty folders removed by hand, plain, plain, plain | `b1759a2` | pass, pass, **FAIL**, pass, FAIL, pass | none, numbers here | the build with the folders removed by hand and no rsync at all failed with 146 and 423 errors: deleting the folders is enough. The legacy sync after a pass failed again. |
| 2026-10-05 11:35 | run 4: C2 + C3, `-cleanBuild`, fresh tree, legacy sync | `b1759a2` | pass, FAIL, pass | none, numbers here | the multi-configuration clean build fails after a pass (373 undefined references over both configurations) like any build. |
| 2026-10-05 11:45 | run 5: run 4's tree, two `-cleanBuild` of C2 and C3 with the protected sync | `b1759a2` | pass, pass | none, numbers here | after a pass, twice. |
| 2026-10-05 11:47 | run 6: run 3's tree, three builds with the protect filters added to the sync command | `b1759a2` | pass, pass, pass | none, numbers here | three builds in a row after a pass, each of which fails under the legacy sync. |
| 2026-10-05 12:38 | run 7: `bench build` of the fix in two worktrees at the same time, fresh Build Trees, 10 builds each: no change, touched source, same again, changed header, C2+C3, no change, override, multi-configuration clean, no change | `df5bd6e` and its dirty variants | A: 10 of 10 pass. B: 9 of 10 pass | none, numbers here | B's second build failed with 0 errors and a different cause (CubeIDE did not find the CM4 configuration, host sleep suspected): invalid, see below. |
| 2026-10-05 12:59 | run 8: three Build Trees, `bench build` of C2, 8 rounds started together and 8 rounds one after another | `df5bd6e` | 48 of 48 pass | none, numbers here | no failure of either kind. |
| 2026-10-05 13:16 | run 9: six pairs of fresh Build Trees, `bench build` of C2 twice in each, both worktrees at the same time (D's failing situation) | `df5bd6e` | 24 of 24 pass | none, numbers here | no failure of either kind. |

The branch was rebased before it landed, so the commit in a Build ID is not the one on `main`: `b1759a2` is `da3c9ec` (the logs), `df5bd6e` is `992d5da` (the sync fix), and `a419c9b` is unchanged.
Runs 1 to 6 used a harness (not committed) that calls bench's own `rsync_command` and `headless_command`, with the protect filters stripped or added by hand; its trees and logs were deleted.
An earlier attempt at runs 2 and 3 was discarded: the code under test had been edited while the harness imported it, so its later builds already used the protected sync.

## One build that did not find the CM4 configuration (host sleep suspected)

In run 7, worktree B's second build (a build right after the first, no change) failed with `WARNING: No Config matched "ArcLoRaM_Base_CM4/Debug_C2". Skipping...` and no error line.
The workspace log shows `required build system is not installed` four times (`CProjectDescriptionManager.getProvider`, from the headless builder's `matchConfigurations`), then `the project does not contain valid configurations`.
It is not the #73 mechanism: the nine folders existed, `.project` and `.cproject` were identical to the repo's, and CM0+ built.
Worktree A started the same step within a second and passed.
Runs 8 and 9 tried to reproduce it: 72 builds, 24 of them with three CubeIDE instances started together and 24 as the second build of a fresh tree beside another, and none failed.
So it happened once in 92 builds.
Simon's reading is that the laptop had just come out of sleep, and the timestamps agree: run 6's last-but-one build shows a 44 minute stall (started 13:49, CubeIDE working at 14:33, local time), and run 7 started at 14:38, the failing build at 14:39.
That is consistent, not proven: both worktrees ran at that moment and only one failed.
If it is a host sleep, it is an invalid run in the sense of `README.md`: no verdict on bench or firmware, run again.
Bench reported `FAILED, 0 error(s)`; the next build of that tree passed.
Tracked in #88 as a start-up condition of CubeIDE's project loading, to be closed if it does not come back.

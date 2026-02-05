# PGO + build profiling workflow (Makefile.profile)

This note describes the three-step profiling flow wired into **Makefile.profile**.

## Files and outputs

* Instrumentation / profile data directory: `./.pgo/`
* Emulator binary: `./emulator`
* Clang raw profile output (when used): `./.pgo/pgo_%p.profraw`
* Clang merged profile (created by build step): `./.pgo/pgo.profdata`
* Clang compile-time traces (per-object): `*.json` next to each `*.o` (enabled by `-ftime-trace`)
* GCC compile-time timing: printed in build output (via `-ftime-report`)

## Step 1 — build an instrumented binary

Build the emulator with instrumentation enabled so it can record runtime behaviour.

```sh
make -f Makefile.profile profile
```

Expected result:

* `./emulator` is rebuilt with profile-generate instrumentation.
* `./.pgo/` exists and will receive profile data during execution.
* The Pi-side kernel/modules build is triggered (`kernel_module`) so pistorm.ko matches the instrumented binary.

## Step 2 — run the instrumented binary to collect profile data

Run the emulator in a way that exercises the hot paths that matter (boot, disk I/O, RTG, whatever is representative).

```sh
make -f Makefile.profile runprofile
```

Passing arguments to the emulator:

```sh
make -f Makefile.profile runprofile PROFILE_ARGS="<args passed to ./emulator>"
```

Examples:

```sh
make -f Makefile.profile runprofile PROFILE_ARGS="-c configs/a500.cfg"
make -f Makefile.profile runprofile PROFILE_ARGS="-h"
```

Expected result:

* **Clang PGO**: one or more `./.pgo/pgo_*.profraw` files appear.
* **GCC PGO**: `./.pgo/` fills with `*.gcda` (and related) files.

## Step 3 — rebuild using the collected profile (PGO use)

This rebuild uses the profile data to guide optimization and also enables compile-time hotspot reporting.

```sh
make -f Makefile.profile buildprofile
```

What this does:

* forces a clean rebuild (so everything is built with PGO-use)
* **clang**: merges `*.profraw` into `./.pgo/pgo.profdata` using `llvm-profdata` and compiles with `-fprofile-use=...`
* enables compile-time hotspot reporting:

  * **clang**: `-ftime-trace` JSON next to each object
  * **gcc**: `-ftime-report` in build logs
* Rebuilds the Pi-side kernel modules (`kernel_module`) to keep the driver in sync with the PGO-enabled emulator.

## Reading the results

### Runtime PGO effect

Compare performance metrics between:

* baseline build (no PGO)
* instrumented build (slower; for data collection)
* PGO-use build (should be faster and/or smoother)

In your world that likely means:

* boot time / perceived responsiveness
* instruction throughput (Dhrystones etc.)
* RTG draw speed
* disk / PiSCSI transfer speed

### Compile-time hot spots

* **Clang**: open the `*.json` `-ftime-trace` files in a trace viewer.

  * Typical workflow is to sort by longest tasks and find which headers/templates are dominating.
* **GCC**: scan the build output for the `-ftime-report` summaries.

These data point to:

* translation units that should be split
* headers that should be pruned
* places where `-O` level, inlining, or LTO choices are hurting compile time

## Reset / start over

To discard old profiles and regenerate:

```sh
rm -rf .pgo
make -f Makefile.profile clean
make -f Makefile.profile profile
make -f Makefile.profile runprofile PROFILE_ARGS="..."
make -f Makefile.profile buildprofile
```

## Suggested profile run (representative workload)

For best PGO results:

* do a run that matches real usage
* include boot + at least one application/benchmark
* avoid only running a help screen or an immediate quit

PGO learns what it sees.

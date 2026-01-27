## Workplan: include Musashi + SoftFloat diagnostics (clang)

### Scope (updated)

* Fix clang diagnostics (notes / warnings / errors) reported in `clang-build.log`.
* Musashi and SoftFloat are now **in scope**:

  * `src/musashi/*`
  * `src/musashi/softfloat/*`
* Preserve emulator behaviour. Changes are limited to:

  * format / printf correctness
  * signed / unsigned conversion safety
  * narrowing / overflow safety
  * const-correctness
  * harmless refactors that do not change logic (local casts, helper macros, type annotations)

---

### Hard rules: generated and macro-driven files

Musashi produces additional files during build. These are **build artifacts** and must not become the primary edit targets.

Observed build artifacts (appear after build, absent after clean):

* `src/musashi/m68kops.c`
* `src/musashi/m68kops.h`
* `src/musashi/m68kops.o`
* `src/musashi/m68kops.d`
* `src/musashi/m68kdasm.o` / `.d`
* `src/musashi/m68kcpu.o` / `.d`
* other `.o` / `.d` objects

Policy:

1. **Do not edit** `m68kops.c` or `m68kops.h` directly.
2. Changes that affect these must be made upstream in the real sources (commonly `m68k_in.c` and/or the generator `m68kmake.c`), then regenerated via the normal build.
3. Treat warnings emitted inside generated code as signals pointing back to the upstream source or generator.

About `m68k_in.c`:

* `m68k_in.c` exists even after clean and is macro-heavy.
* It is allowed to change, while changes must be:

  * minimal
  * type / format-only
  * never logic-changing
* Any change here can change generated output (`m68kops.*`). That is expected, while edits must stay strictly within warning fixes.

---

### Canonical workflow (broader capture)

* Build and capture:

  * `./rebuild.sh` → produces `clang-build.log`
* Count and triage:

  * `./rebuildcount.sh` → produces `clang-diagnostics.log`
  * `./rebuildcount.sh 1` → bottom-up excerpts (newest first)
  * `./rebuildcount.sh src/musashi/` → only Musashi diagnostics
  * `./rebuildcount.sh src/softfloat/` → only SoftFloat diagnostics

---

### Safe editing procedure for Musashi / SoftFloat

Before every edit pass:

1. `git status --porcelain` must be clean (or known clean with intentional local changes).
2. Run `make clean` once at the start of a Musashi pass, then `./rebuild.sh` to re-establish a baseline.
3. After each fix group, run `./rebuild.sh` again and re-check counts.

During fixes:

* Fix **errors first**, then warnings, then notes.
* Prefer localized fixes:

  * explicit casts with range justification
  * `PRIu32`, `PRIx32`, etc. via `<inttypes.h>` for printf
  * `size_t` correctness for sizes / indices
  * avoid refactors that reorder code, restructure switch tables, or alter generated opcode logic
* For varargs / printf, remember default integer promotions for `uint8_t` / `int8_t`.

---

### SoftFloat rules

SoftFloat is correctness-sensitive. Fixes must be “type hygiene” only:

* no algorithmic changes
* no reordering of operations
* no replacing macros with functions unless behaviour stays bit-exact
* permitted changes:

  * cast / format fixes
  * signed / unsigned alignment
  * const correctness
  * unused parameter annotations (only when behaviour unchanged)

---

### Musashi rules

Musashi is a CPU core: the goal is “compiler clean” without altering emulation results.

Allowed:

* printf / format fixes
* narrowing fixes with explicit bounds
* sign conversion fixes
* const correctness
* unused parameter silencing (only when safe)

Avoid:

* changing opcode decode logic
* altering tables / defines that control opcode generation semantics
* wide refactors across `m68k_in.c`

---

### Guardrails against accidental behaviour changes

After every round that touches `src/musashi/` or `src/musashi/softfloat/`:

1. Confirm only intended files changed:

   * `git diff --stat`
2. Confirm no direct edits landed in `m68kops.c` / `m68kops.h`:

   * `git diff --name-only | rg 'm68kops\.(c|h)$'` must be empty
3. Rebuild and confirm diagnostics trend to zero:

   * `./rebuild.sh`
   * `./rebuildcount.sh src/musashi/`
   * `./rebuildcount.sh src/softfloat/`

---

### Definition of done (updated)

* `./rebuild.sh` produces a `clang-build.log` with:

  * **0 errors**
  * warnings driven toward **0** (notes acceptable, though preferred low)
* `./rebuildcount.sh src/musashi/` and `./rebuildcount.sh src/softfloat/` show warnings reduced to the agreed target (ideally 0).
* No manual edits to generated `m68kops.*` files.


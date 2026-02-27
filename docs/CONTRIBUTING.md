# Contributing to PiStorm64 / JANUS Bus Engine

Welcome, traveller of the 68k realm.

This project is more than “an emulator that happens to be fast”. The goal is to build a coherent, Amiga‑native hardware ecosystem powered by the JANUS Bus Engine: Pi‑side intelligence, Amiga‑side elegance, joined through clean Zorro‑style abstractions.

This document explains how to contribute while keeping that vision intact.

---

## Project philosophy

1. **Amiga first, Pi second**
   The Amiga sees proper devices: Zorro II / Zorro III boards, SANA‑II NICs, autoconfig ROMs, filesystems, handlers. The Pi is an implementation detail.

2. **JANUS Bus Engine everywhere**
   All new host/Amiga integrations should be modelled as JANUS devices or clean Zorro‑style cards. No ad‑hoc backdoors into the emulator core.

3. **Modern code, classic behaviour**
   New code lives in C (or 68k asm where required), structured, testable and portable, while preserving the timing, semantics and expectations of real hardware.

4. **No “host hacks” that break real hardware semantics**
   If a feature could not plausibly exist on a real Amiga (seen from the OS side), it probably belongs behind a JANUS service or a separate experimental branch.

5. **Performance with discipline**
   JIT, DMA, and zero‑copy paths are welcome, while correctness and determinism come first. A fast bug is still a bug.

---

## Scope: what belongs in this tree

In‑scope contributions:

* New or improved **Zorro II / Zorro III devices** (e.g. net64, piscsi64, rtg64) with proper autoconfig behaviour.
* Enhancements to the **JANUS Bus Engine** and its APIs.
* Platform glue for Amiga‑side libraries, devices and handlers that talk to JANUS / Pi‑side services.
* Performance, stability and portability improvements to the emulator that benefit Amiga platforms.
* Documentation, tools and scripts that help develop, test or integrate the above.

Out‑of‑scope (for this repo’s mainline):

* Host‑only features that never present as an Amiga device or service.
* Hacks that depend on a specific Linux distro, desktop environment or kernel fork.
* Non‑Amiga platforms (e.g. generic 68k boards) unless they share the JANUS/Amiga abstraction cleanly.

Experimental work is welcome in separate branches or forks as long as it does not destabilise the mainline.

---

## Code of conduct

This project follows the **Contributor Covenant**. Be constructive, respectful and kind. Retro hardware is hard enough; people should not be.

---

## Repository layout (high‑level)

* `src/` – emulator core, platforms and devices.

  * `src/platforms/amiga/` – Amiga‑specific platform code.

    * `net/` – legacy / existing networking glue (kept stable).
    * `net64/` – new JANUS‑driven Z2/Z3 NIC implementation.
    * `piscsi64/` – Pi‑side SCSI and autoboot ROM logic.
    * `rtg64/` – (planned) RTG and chunky display devices.
* `docs/` – design notes, docs, and wiki sources.
* `tools/` – helper utilities, build scripts and analysis tools.

When in doubt, keep Amiga‑facing devices under `src/platforms/amiga/` and Pi‑only helpers in a clearly separated area.

---

## Development environment

You may use any host OS you like, however the canonical build and test environments use:

* A recent Linux (Arch/Alpine and friends are well‑tested).
* GCC or Clang for C/C++ host builds.
* `m68k-amigaos-*` toolchains for Amiga‑side code (devices, libraries, ROMs).
* Python 3 for tools and scripts.

Try to avoid introducing mandatory dependencies on heavyweight stacks (Docker, complex service meshes, etc.) for core workflows.

---

## Coding style

Host C/C++ code:

* K&R brace style, always use braces, even for single statements:

  ```c
  if (a) {
      b;
  }

  while (x) {
      step();
  }

  for (int i = 0; i < n; i++) {
      work(i);
  }
  ```

* Keep functions focused and small where sensible.

* Avoid global mutable state unless it truly represents hardware.

* Prefer `static` for internal helpers; keep headers minimal.

* New files must include clear ownership in a comment header and reference the project’s MIT license.

Amiga‑side 68k code:

* Follow the traditional Amiga calling conventions and register usage.
* Use official includes (`ndk`, `SANA-II`, etc.) where possible rather than duplicating definitions.
* Keep ROM and device code position‑independent and autoconfig‑correct.

---

## Commit and branch guidelines

* Work on a **feature branch** in your fork (e.g. `feature/net64-mtu-control`).
* Keep commits focused and logically grouped. Avoid mixing refactors and behaviour changes in the same commit.
* Commit messages should be clear and imperative, e.g. `net64: add SANA-II config query`.
* Where appropriate, reference issues (e.g. `Fixes #42`).

---

## Testing requirements

Before opening a pull request, please:

1. Build and run the emulator on at least one supported Pi model.
2. Run any relevant unit or integration tests for your area.
3. For device changes (piscsi64, net64, rtg64, etc.):

   * Confirm autoconfig works on at least one classic Amiga OS (e.g. 3.1, 3.1.4, 3.9).
   * Confirm basic functionality (e.g. mount a disk, transfer a file, open a TCP connection).
   * Where performance is the goal, capture a small before/after benchmark or qualitative notes.

Document any known limitations or regressions in the PR description.

---

## Adding a new device (example checklist)

When adding a new Zorro II / Zorro III device under `src/platforms/amiga/`:

1. Define a clear **autoconfig identity** (manufacturer/product ID, size, type).
2. Model it as a proper Amiga device: implement configuration, ROM (if needed) and clean register map.
3. Expose functionality via JANUS / Pi‑side services rather than ad‑hoc shared memory blobs.
4. Provide at least one **Amiga‑side driver** (device, SANA‑II interface, library or handler) in a suitable tree.
5. Add documentation under `docs/` explaining:

   * What the device does.
   * How it appears to Amiga OS.
   * How to install it (mountlists, devs: entries, etc.).

---

## Opening a pull request

When your changes are ready:

1. Ensure your branch is rebased on the current `main` (or the branch you target).
2. Open a PR with:

   * A concise title: `net64: implement DHCP support`.
   * A description covering motivation, design overview, and testing performed.
   * Any compatibility notes (required ROMs, OS versions, Pi models).
3. Be prepared to iterate. Reviews are about keeping the architecture clean and consistent.

---

## Design discussions and big ideas

Feature‑sized or architecture‑level changes (for example, a new JANUS service class, RTG overhaul, or bus‑level timing model changes) should start as an **issue or design document** in `docs/`. Sketch the intent, constraints, and alternatives so discussion can happen before code lands.

Some guiding questions:

* Does this look like a plausible Amiga device or service?
* Can a future hardware implementation follow this contract?
* Does it strengthen the JANUS Bus Engine, or bypass it?

---

## Commercial use & derivatives

The code is MIT‑licensed for maximum flexibility. However, if you build commercial products or services on top of this work, you are strongly encouraged (not legally obliged) to:

* Attribute the project clearly in documentation and marketing materials.
* Contribute improvements that are broadly useful back upstream.
* Avoid shipping proprietary changes that lock out the community from long‑term maintenance of core features.

Retro ecosystems live or die on shared stewardship. Please help keep this one healthy.

---

## Getting help

If you are unsure whether an idea fits the vision or where it should live in the tree, open an issue titled `RFC: ...` with a short description. The maintainers can help map it to the JANUS Bus Engine model or suggest a better place.

Welcome aboard. Let us build hardware that never quite existed, for a machine that never quite died.

# KernelPiStorm64 upstream-prep plan (DT/YAML/Kconfig/UAPI)

Goal: turn the current out-of-tree `kernel_module/src/pistorm.c` into a *proper* upstreamable Linux kernel driver.

Upstream expectations in one sentence: **DT describes the hardware; the driver consumes DT-described resources using modern APIs; UAPI is stable; clock/GPIO ownership is safe and reviewable; minimal, boring, maintainable.**

This plan is written as tasks Qwen can execute on a clean branch. Each task includes acceptance criteria.

---

## 0) Create a clean upstream-prep branch

Create a new branch dedicated to kernel upstream prep.

* Branch name suggestion: `kernel/upstream-prep` or `kernel/dt-binding`
* Keep existing out-of-tree build working while migrating APIs.

Acceptance:

* Branch builds current module exactly as before.

---

## 1) Decide the target placement + naming

Upstream requires the driver placed under `drivers/` with Kconfig/Makefile glue.

Recommended target:

* `drivers/misc/pistorm/` (or a single file in `drivers/misc/` initially)

Recommended naming:

* Driver name: `pistorm`
* Module description: “GPIO/GPCLK bus engine for PiStorm CPLD bridge”

Acceptance:

* A clear rationale comment in the driver header describing what the driver does and *does not* do.

---

## 2) Freeze + clean the UAPI (`include/uapi/linux/pistorm.h`)

Treat UAPI as a long-term contract.

Tasks:

1. Ensure kernel-style comments and stable struct layouts.
2. Ensure `__u8` fields are padded/aligned consistently.
3. Add an explicit versioning strategy.

Recommended UAPI updates:

* Add an ABI version constant:

  * `#define PISTORM_ABI_VERSION 1`
* Add a `__u32 abi_version;` field to a new query struct (preferred over changing existing structs).
* Add a new ioctl to query version/capabilities:

  * `PISTORM_IOC_QUERY`
* Keep existing ioctls intact.

Avoid:

* Changing existing ioctl numbers or struct layouts used by userspace.

Acceptance:

* UAPI header passes `scripts/checkpatch.pl` (where applicable).
* A short UAPI documentation note exists in `Documentation/ABI/` or in the driver docs (see section 9).

---

## 3) Convert from raw GPIO numbers to descriptor-based GPIO (gpiod)

Current code uses `gpio_request()` and hardcoded pins 0–23.

Upstream wants:

* GPIOs described in DT and acquired using `devm_gpiod_get()` / `devm_gpiod_get_array()`.

Approach:

* Split GPIOs into named groups:

  * control outputs: A0, A1, RESET, RD, WR
  * control inputs: TXN_IN_PROGRESS, IPL_ZERO
  * data bus: D0..D15 (bi-directional)
  * clock pin: CLK (ALT0 / GPCLK0), see section 4

Implementation notes:

* Use `struct gpio_desc *` for each control line.
* Use `struct gpio_descs *data` array for D0..D15.
* Replace `ps_set_bus_dir()` logic:

  * Instead of changing GPFSEL registers directly, set direction via gpiod:

    * output mode for data pins when writing
    * input mode for data pins when reading

Important: performance.

* gpiod direction switching per operation may be too slow.
* For upstream, *correctness and safety first*; performance later.
* A compromise is allowed:

  * Keep *fast register path* for data bus in one place, but still acquire pins from DT.
  * If keeping register path, do not use `gpio_request()`; use DT + pinctrl + documented assumptions.

Acceptance:

* No `gpio_request()` remains.
* All GPIOs acquired via `devm_*` APIs.
* The module works using DT-defined GPIOs.

---

## 4) Stop “compat scanning”; use DT resources (reg + clocks)

Current code searches DT for `brcm,bcm2835-gpio` and `brcm,bcm2835-cprman` nodes.

Upstream wants:

* The *device node for pistorm* contains `reg` resources and `clocks`.
* Driver binds to its own compatible string and uses `platform_driver`.

Tasks:

1. Convert to a real `platform_driver` with `probe()` and `remove()`.
2. Use `platform_get_resource()` + `devm_ioremap_resource()`.
3. Acquire clock via `devm_clk_get()` and enable via `clk_prepare_enable()`.

Clock reality check:

* GPCLK0 might not be exposed as a standard CCF clock on all Pi DTs.
* Two options:

Option A (preferred): CCF-backed clock

* DT supplies a clock phandle.
* Driver uses `clk_set_rate()` + `clk_prepare_enable()`.

Option B (fallback): documented CPRMAN MMIO

* DT supplies a `reg` region for cprman.
* Driver still writes CPRMAN registers.
* Must include a strong comment explaining why CCF is not viable.

Also:

* Use `pinctrl` to set GPIO4 to ALT0 (GPCLK0) via DT.
* Do not manually set fsel for the clock pin in code.

Acceptance:

* Driver no longer finds GPIO/CPRMAN by compat scanning.
* Driver binds only via its own `compatible` node.
* All mappings come from the pistorm DT node resources.

---

## 5) Introduce pinctrl states for the bus

DT should provide pinctrl groups:

* `pinctrl-0` default: clock pin muxed to GPCLK0, control pins as GPIO, data bus default input.
* Optional additional states:

  * `pinctrl-data-out`
  * `pinctrl-data-in`

However:

* pinctrl state switching per bus op may be heavy.
* Prefer data direction switching via gpiod if possible.
* Use pinctrl mainly for GPCLK muxing and any required pulls.

Acceptance:

* DT includes pinctrl.
* Driver requests pinctrl and applies default state.

---

## 6) Move to devm allocations + per-device struct

Current code uses a single global `ps_dev`.

Upstream wants:

* Per-device `struct pistorm_dev` stored with `platform_set_drvdata()`.
* No globals.

Tasks:

* Replace `static struct pistorm_dev *ps_dev;` global.
* Make helpers take `struct pistorm_dev *ps` always.
* Use `devm_kzalloc()`.
* Replace `pr_*` with `dev_*` where appropriate.

Acceptance:

* No globals.
* `probe()` allocates and initializes device context.

---

## 7) Replace miscdevice or justify it

`miscdevice` is acceptable for simple character devices.

Tasks:

* Keep `miscdevice` unless reviewers demand class/dev node.
* Use `devm_misc_register()`.

Also:

* Ensure permissions are handled by udev rules in userspace (fine) and/or document.

Acceptance:

* `/dev/pistorm` appears via misc.

---

## 8) Harden IOCTL handling (kernel style)

Tasks:

* Validate user pointers and sizes.
* Ensure `_IOC_DIR` and size checks are correct.
* Ensure compat ioctl correctness for 32-bit userspace.

Batched ops:

* Keep batch API.
* Ensure `ops_count` limit remains.
* Consider `copy_struct_from_user()` where supported.

Logging:

* Remove chatty `pr_debug` unless behind dynamic_debug.

Acceptance:

* `checkpatch.pl` clean.
* No unbounded loops on user input.

---

## 9) Add DT binding YAML + Documentation

Add a DT binding file:

* `Documentation/devicetree/bindings/misc/pistorm.yaml`

Must include:

* compatible: `akadata,pistorm` (or community-agreed string)
* reg: for GPIO and optional CPRMAN, or reg-names if multiple.
* gpclk clock phandle if possible.
* gpio properties:

  * `txn-in-progress-gpios`
  * `ipl-zero-gpios`
  * `a0-gpios`
  * `a1-gpios`
  * `reset-gpios`
  * `rd-gpios`
  * `wr-gpios`
  * `data-gpios` (array of 16)
* pinctrl-0 / pinctrl-names
* optional properties for tuning (div/src): should be DT properties, not module params, for upstream.

Example DT snippet to include in YAML.

Also add ABI documentation:

* `Documentation/ABI/testing/dev-pistorm` describing `/dev/pistorm` and ioctls.

Acceptance:

* `make dt_binding_check` passes.
* YAML includes examples.

---

## 10) Convert module params into DT properties

Upstream dislikes module params for hardware configuration.

Tasks:

* Replace `gpclk_src` and `gpclk_div` module params with DT properties.

  * `akadata,gpclk-src`
  * `akadata,gpclk-div`
* Provide defaults in code if properties absent.

Acceptance:

* No module params for gpclk.
* DT controls behaviour.

---

## 11) Power, reset, and failure modes

Tasks:

* Ensure gpclk is disabled on remove.
* Ensure reset lines are returned to safe state.
* Ensure timeouts are bounded and errors propagate.

Consider:

* A sysfs attribute or debugfs for pin state could be useful; avoid for first upstream.

Acceptance:

* Clean unload/reload works.

---

## 12) Kernel style and correctness pass

Tasks:

* Run `scripts/checkpatch.pl`.
* Fix indentation (kernel uses tabs, not spaces).
* Wrap lines <= 80 columns where practical.
* Replace C99 `for (int i=...)` with kernel-friendly style if needed (kernel supports C99 features variably; best to keep older style).
* Ensure types use `u32/u16/u8` and `bool` properly.

Acceptance:

* `checkpatch.pl` reports no serious issues.

---

## 13) Add Kconfig/Makefile + MAINTAINERS

Files:

* `drivers/misc/Kconfig` add an entry, or create `drivers/misc/pistorm/Kconfig`.
* `drivers/misc/Makefile` add obj-$(CONFIG_PISTORM) += pistorm/
* `MAINTAINERS` entry.

Kconfig should:

* depend on `ARCH_BCM2835` / `ARCH_BCM2711` as appropriate, or `ARCH_BRCMSTB` not correct; pick carefully.
* select `GPIOLIB` and `PINCTRL`.

Acceptance:

* `make menuconfig` shows PISTORM driver.
* Building kernel with config builds the driver.

---

## 14) Provide an out-of-tree “staging” path (optional but helpful)

While upstreaming, keep your repository building as an external module.

Tasks:

* Provide a `Kbuild` and external module Makefile that points into the kernel tree.

Acceptance:

* `make -C /lib/modules/$(uname -r)/build M=$PWD modules` works.

---

## 15) Submission strategy (what to send as patches)

Upstream prefers a patch series:

1. DT binding YAML
2. UAPI header + ABI doc
3. Driver + Kconfig/Makefile + MAINTAINERS

Each patch should be bisectable.

Acceptance:

* Series applies cleanly.

---

## Minimal DT example (for YAML + testing)

This is an illustrative example. Actual addresses depend on Pi SoC/DT.

```
pistorm@0 {
    compatible = "akadata,pistorm";

    reg = <0x0 0x7e200000 0x0 0x100>,   /* gpio */
          <0x0 0x7e101000 0x0 0x200>;   /* cprman (optional) */
    reg-names = "gpio", "cprman";

    pinctrl-names = "default";
    pinctrl-0 = <&pistorm_pins>;

    txn-in-progress-gpios = <&gpio 0 GPIO_ACTIVE_HIGH>;
    ipl-zero-gpios        = <&gpio 1 GPIO_ACTIVE_HIGH>;
    a0-gpios              = <&gpio 2 GPIO_ACTIVE_HIGH>;
    a1-gpios              = <&gpio 3 GPIO_ACTIVE_HIGH>;
    reset-gpios           = <&gpio 5 GPIO_ACTIVE_HIGH>;
    rd-gpios              = <&gpio 6 GPIO_ACTIVE_HIGH>;
    wr-gpios              = <&gpio 7 GPIO_ACTIVE_HIGH>;

    data-gpios = <&gpio 8 GPIO_ACTIVE_HIGH>, <&gpio 9 GPIO_ACTIVE_HIGH>,
                 <&gpio 10 GPIO_ACTIVE_HIGH>, <&gpio 11 GPIO_ACTIVE_HIGH>,
                 <&gpio 12 GPIO_ACTIVE_HIGH>, <&gpio 13 GPIO_ACTIVE_HIGH>,
                 <&gpio 14 GPIO_ACTIVE_HIGH>, <&gpio 15 GPIO_ACTIVE_HIGH>,
                 <&gpio 16 GPIO_ACTIVE_HIGH>, <&gpio 17 GPIO_ACTIVE_HIGH>,
                 <&gpio 18 GPIO_ACTIVE_HIGH>, <&gpio 19 GPIO_ACTIVE_HIGH>,
                 <&gpio 20 GPIO_ACTIVE_HIGH>, <&gpio 21 GPIO_ACTIVE_HIGH>,
                 <&gpio 22 GPIO_ACTIVE_HIGH>, <&gpio 23 GPIO_ACTIVE_HIGH>;

    akadata,gpclk-src = <5>;
    akadata,gpclk-div = <6>;
};
```

And a pinctrl group example (platform-specific):

```
pistorm_pins: pistorm-pins {
    brcm,pins = <4>;          /* GPIO4 */
    brcm,function = <4>;      /* ALT0 for GPCLK0 */
    brcm,pull = <0>;          /* no pull */
};
```

---

## Notes on performance vs upstream correctness

Two viable upstream approaches exist:

A) **Pure gpiod** (slow but clean):

* All bus operations drive pins using gpiod.
* Likely too slow for real use; acceptable as a correctness-first upstream baseline, but may disappoint users.

B) **Hybrid** (best chance of both acceptance and usefulness):

* DT + pinctrl + gpiod for ownership and safety.
* Keep the fast MMIO path for data lines and/or GPSET/GPCLR toggling.
* Document the dependency on BCM GPIO register layout.

Recommended: B) Hybrid.

---

## Deliverables checklist

* [ ] Converted to `platform_driver`
* [ ] DT binding YAML + examples
* [ ] UAPI frozen + ABI doc
* [ ] No compat scanning; resources from DT
* [ ] No `gpio_request()`; gpiod + pinctrl
* [ ] `devm_*` allocation and registration
* [ ] Kconfig/Makefile/MAINTAINERS
* [ ] `checkpatch.pl` clean
* [ ] Remove module params; DT properties instead
* [ ] Clean unload/reload

---

## Qwen execution order (do this in sequence)

1. Refactor to platform_driver + per-device struct + devm + misc register.
2. Add DT binding YAML + ABI doc (even stubbed, but correct format).
3. Swap compat scanning for DT resources (`reg`, `reg-names`).
4. Add pinctrl default for clock muxing.
5. Replace gpio_request with gpiod descriptors.
6. Replace module params with DT properties.
7. Style pass + checkpatch.
8. Add Kconfig/Makefile/MAINTAINERS.
9. Validate dt_binding_check.

That is the shortest path from “works locally” to “reviewable upstream”.


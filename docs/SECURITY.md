# Security Policy

This document describes how to report security issues in the AKADATA PiStorm64 family of projects (including `pistorm64` emulator, `piscsi64` boot ROM and SCSI stack, `net64` network device, and future `rtg64` or related components).

The goal is to protect users and their data while keeping the projects open, hackable, and fun.

---

## Supported Projects and Branches

Security reports are welcome for:

* The main branch of this repository on GitHub.
* Active feature branches that are clearly advertised in the README.
* Released binaries or ROM images that are linked from this repository.

Old experimental branches, local forks, and unmodified upstream projects fall outside this policy.

---

## What Counts as a Security Issue

Examples of issues that are treated as security-relevant:

* Emulator or device behaviour that can corrupt Amiga storage in ways a normal user cannot reasonably avoid.
* Remote or local code execution within the Pi host through data supplied to the Amiga side.
* Privilege escalation on the Pi host caused by running the emulator or helpers.
* Network issues in `net64` (or future network devices) that allow:

  * Packet spoofing beyond what the Amiga config describes.
  * Information leaks of host-side data that should never reach the Amiga.
  * Denial of service that persists after the emulator stops.
* Malicious behaviour in prebuilt ROMs or binaries linked from this repository.

Bugs that only crash the emulator or Amiga software without any realistic security impact are treated as regular defects, not vulnerabilities.

---

## How to Report a Vulnerability

Private disclosure is preferred, so problems can be analysed and fixed before public discussion.

Use one of these channels:

* Email: **[security@akadata.ltd](mailto:security@akadata.ltd)**
* GitHub: open a **Private security advisory** for the repository (GitHub supports this in the "Security" tab).

When sending a report, include where possible:

* A clear description of the issue and why it matters.
* Exact version or commit hash in use.
* Platform details (Pi model, OS, kernel, Amiga ROM and Workbench version, and configuration used).
* Minimal steps to reproduce the issue.
* Any proof-of-concept scripts, images, or configuration files required.

Logs, screenshots, and configuration snippets are also very helpful.

---

## What to Expect

After a report arrives via one of the channels above:

1. **Acknowledgement** – A short confirmation will be sent once the report has been read.
2. **Initial triage** – The issue will be reproduced where possible and classified as:

   * Security vulnerability, or
   * Regular bug / configuration issue.
3. **Fix planning** – For confirmed vulnerabilities:

   * A fix or mitigation will be developed.
   * A coordinated disclosure date will be agreed, where appropriate.
4. **Resolution** – A release or patch will be published, and the reporter may be credited in the changelog (with consent).

Timing depends on severity, complexity, and available time, however security issues always take priority over new features.

---

## Responsible Disclosure and Public Discussion

When a serious issue is found, please avoid posting full exploits or step-by-step instructions publicly until a fix is available.

General discussion of emulator security, risk models, or hardening strategies is very welcome in public issues or forums. Concrete vulnerabilities are better handled through private disclosure first.

---

## Out of Scope

The following are currently out of scope for this policy:

* Security flaws in third-party Amiga software, Kickstart ROMs, Workbench distributions, or commercial games and demos.
* Bugs in the Raspberry Pi kernel, firmware, or operating system that are not triggered by this project in a special way.
* Hardware-level attacks that require physical access to the machine (for example voltage glitching, desoldering ROMs, or JTAG on the Pi).
* Behaviour that only occurs on heavily modified local forks not maintained by AKADATA.

Such issues may still be interesting, yet they are better reported to the relevant upstream projects or hardware vendors.

---

## Cryptography and Sensitive Data

Some future components (for example storage encryption helpers or network tunnelling) may work with keys or other sensitive data.

When problems are found in these areas, please:

* Treat sample keys and credentials as secrets.
* Avoid committing them into public repositories or gists.
* Prefer encrypted attachments or password-protected archives when sending proof-of-concept material.

The aim is to ensure that reports never make the situation worse for real users.

---

## Thanks

Every report that improves the safety and robustness of these projects is appreciated.

The retro-computing and Amiga communities rely on shared hardware and images that often circulate for decades. Strong security practices help keep that shared history running safely for everyone.

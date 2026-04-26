# AGENTS.md

Dobbscoin Autonomous Build and Maintenance Policy

---

## Purpose

This document defines the operational rules for automated agents (such as Codex) working within the Dobbscoin codebase.

The goal is to enable safe autonomous troubleshooting, build stabilization, and modernization while preserving:

* Network stability
* Blockchain integrity
* Deterministic builds
* Backward compatibility
* Reproducible release processes

This policy applies to all automated actions performed inside the repository.

---

## Project Context

Dobbscoin is a cryptocurrency derived from Bitcoin Core 0.10.x.

The current modernization effort focuses on:

* Stabilizing builds on modern Linux systems
* Producing reliable Windows binaries
* Maintaining compatibility with the existing blockchain
* Incrementally modernizing infrastructure without protocol changes

The daemon (`dobbscoind`) remains the authoritative system.

All automation must respect that constraint.

---

## Primary Objective

The agent’s primary objective is:

Produce deterministic, reproducible Dobbscoin builds that run reliably on supported platforms.

Secondary objectives:

* Diagnose build failures
* Maintain toolchain compatibility
* Stabilize dependency builds
* Generate release artifacts

---

## Operating Principles

All actions must follow these rules:

1. Stability over speed
2. Determinism over experimentation
3. Minimal changes over large refactors
4. Reproducibility over convenience
5. Safety over automation

---

## Allowed Autonomous Actions

The agent may perform the following actions without human approval:

### Build Operations

* Run build commands
* Retry failed builds
* Resume interrupted builds
* Clean and rebuild individual components
* Regenerate build artifacts
* Re-run configure scripts
* Re-run dependency builds

### Diagnostics

* Parse build logs
* Identify compiler errors
* Identify linker errors
* Identify missing headers
* Identify missing libraries
* Identify configuration failures
* Extract first root error from logs

### Safe File Modifications

The agent may modify:

* build scripts
* configuration files
* dependency recipes
* patch files
* Makefiles
* qmake files
* CMake files
* packaging scripts
* documentation files

Only when required to fix deterministic build failures.

### Controlled Rebuilds

The agent may remove and rebuild:

depends/work/build/*

and

build directories

but must not remove the entire repository.

---

## Restricted Actions

The agent must NOT perform the following actions without explicit approval:

### System-Level Changes

Do not:

* Install system packages
* Remove system packages
* Upgrade system packages
* Change compiler versions
* Modify operating system configuration
* Change kernel settings
* Modify firewall rules
* Modify network configuration

---

### Repository-Level Destructive Actions

Do not:

* Delete the repository
* Remove source history
* Reset branches
* Rewrite commits
* Force-push changes
* Delete tags
* Remove the depends directory
* Remove the src directory
* Remove the doc directory

---

### Consensus-Sensitive Code Changes

The agent must never modify:

* Consensus validation logic
* Block validation rules
* Transaction validation rules
* Proof-of-work logic
* Block serialization
* Transaction serialization
* Network protocol behavior
* Wallet database format
* Cryptographic algorithms

These components define network consensus.

Unauthorized changes could fork the blockchain.

---

## Qt and Dependency Build Policy

The agent may modify dependency builds to stabilize compilation.

Allowed dependency adjustments:

* Disable optional Qt modules
* Adjust link order
* Adjust compiler flags
* Disable unused features
* Fix dependency paths
* Apply deterministic patches
* Rebuild individual dependency packages

Optional subsystems that may be disabled:

* DBus
* Accessibility
* Multimedia
* Bluetooth
* WebKit
* Sensors
* SQL drivers
* Printing services
* OpenGL features
* ICU
* GTK integration

Required subsystems must remain enabled.

---

## Deterministic Failure Handling

When a build fails, the agent must follow this sequence:

Step 1 — Identify first error
Step 2 — Determine root cause
Step 3 — Apply minimal fix
Step 4 — Retry build

Never apply multiple unrelated fixes simultaneously.

Never guess.

Always base actions on the first real error.

---

## Retry Policy

The agent may retry deterministic failures automatically.

Maximum retries:

3 attempts per failure

If the same failure occurs three times:

Stop and report.

---

## Reporting Requirements

The agent must report immediately when:

1. A build completes successfully
2. A deterministic failure persists after three attempts
3. A required system dependency is missing
4. A restricted action is required
5. A consensus-related file would be modified
6. A patch cannot be applied cleanly
7. A reproducibility issue is detected

---

## Logging Requirements

All automated actions must be logged.

Required logging:

* command executed
* file modified
* patch applied
* error detected
* retry attempt
* build completion

Logs must be written to:

build-windows-autonomous.log

or equivalent.

---

## Reproducibility Requirements

All builds must remain reproducible.

The agent must:

* Use deterministic build flags
* Avoid environment-specific hacks
* Avoid random configuration changes
* Preserve dependency versions
* Preserve compiler versions
* Preserve toolchain versions

---

## Platform Support Scope

The agent may build for:

Linux
Windows
macOS

The agent must not introduce platform-specific behavior that breaks other supported platforms.

---

## Packaging Policy

The agent may create release artifacts.

Allowed artifacts:

* dobbscoin-qt.exe
* dobbscoind.exe
* dobbscoin-cli.exe
* zip archives
* tar archives
* build documentation

Required packaging behavior:

Include all runtime dependencies
Preserve directory structure
Verify executable existence

---

## Stop Conditions

The agent must stop immediately if:

* A consensus rule would be modified
* A database format would change
* A cryptographic algorithm would change
* A protocol rule would change
* A destructive repository action would occur
* A system package installation is required
* A build failure persists after three attempts

---

## Completion Conditions

The agent’s task is considered complete when:

qtbase/.stamp_built exists

and

dobbscoin-qt executable is produced

and

build artifacts are packaged successfully

---

## Escalation Rule

If the agent encounters an unfamiliar failure:

Stop
Report the first error
Wait for instruction

Do not attempt experimental fixes.

---

## Default Behavior

When uncertain:

Do nothing
Report status
Wait for instruction

---

End of AGENTS.md


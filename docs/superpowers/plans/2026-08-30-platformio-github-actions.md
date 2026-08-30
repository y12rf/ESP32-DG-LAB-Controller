# PlatformIO GitHub Actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a GitHub Actions workflow that compiles the PlatformIO project for pushes to `main` and pull requests targeting `main`.

**Architecture:** A single workflow runs on an Ubuntu GitHub-hosted runner. It installs a pinned PlatformIO Core, restores download caches, and delegates all firmware configuration to the repository's existing `platformio.ini` by executing `pio run`.

**Tech Stack:** GitHub Actions, Python 3.12, PlatformIO Core 6.1.18, Espressif 32 platform 6.13.0

## Global Constraints

- Trigger only for pushes to `main`, pull requests targeting `main`, and manual dispatch.
- Grant only `contents: read` permission.
- Compile with `pio run` and the existing `platformio.ini`.
- Do not upload artifacts, publish releases, flash hardware, or continue after failures.
- Cache only pip downloads and PlatformIO's download cache.

---

### Task 1: Add the PlatformIO CI workflow

**Files:**
- Create: `.github/workflows/platformio.yml`

**Interfaces:**
- Consumes: `platformio.ini` and the source tree under `src/`.
- Produces: A GitHub status check named `PlatformIO CI / build` whose result is the exit status of `pio run`.

- [ ] **Step 1: Verify the workflow is absent**

Run:

```powershell
Test-Path '.github/workflows/platformio.yml'
```

Expected: `False`.

- [ ] **Step 2: Create the workflow**

Create `.github/workflows/platformio.yml` with exactly:

```yaml
name: PlatformIO CI

on:
  push:
    branches:
      - main
  pull_request:
    branches:
      - main
  workflow_dispatch:

permissions:
  contents: read

jobs:
  build:
    runs-on: ubuntu-latest

    steps:
      - name: Check out repository
        uses: actions/checkout@v6

      - name: Set up Python
        uses: actions/setup-python@v6
        with:
          python-version: "3.12"

      - name: Cache PlatformIO downloads
        uses: actions/cache@v4
        with:
          path: |
            ~/.cache/pip
            ~/.platformio/.cache
          key: ${{ runner.os }}-pio-6.1.18-${{ hashFiles('platformio.ini') }}
          restore-keys: |
            ${{ runner.os }}-pio-6.1.18-

      - name: Install PlatformIO Core
        run: python -m pip install --upgrade pip platformio==6.1.18

      - name: Build firmware
        run: pio run
```

- [ ] **Step 3: Validate YAML and workflow semantics**

Parse the file with a YAML parser, then assert that:

- `push.branches` is exactly `[main]`.
- `pull_request.branches` is exactly `[main]`.
- `workflow_dispatch` exists.
- `permissions.contents` is `read`.
- The final step runs exactly `pio run`.
- The file contains none of `upload-artifact`, `release`, `--target upload`, or `continue-on-error`.

Expected: all assertions pass with exit code 0.

- [ ] **Step 4: Build locally**

Run:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

Expected: `[SUCCESS]` for `esp32dev`.

- [ ] **Step 5: Check and commit the patch**

Run:

```powershell
git diff --check
git status --short
git add .github/workflows/platformio.yml
git commit -m "ci: add PlatformIO build workflow"
```

Expected: no whitespace errors, only the planned workflow is added, and the commit succeeds.

- [ ] **Step 6: Push the completed work**

Run:

```powershell
git push origin main
```

Expected: the remote `main` branch advances to the local verified commit.

# Agent Guidance

## Project status

Prismatic Launcher is a local C++20/Qt 6 Widgets desktop mod launcher. The user's autonomous product implementation mission extends the original infrastructure-only foundation. Read `infrastructure_plan.md`, `docs/decisions/001-local-mod-profiles.md`, `docs/decisions/002-phase2-mod-services-and-packaging.md`, and `PROGRESS.md` for current decisions, limitations, and verification. `docs/phase2-report.md` records packaging artifacts and native-platform limits. The historical infrastructure plan is in `prismatic-launcher/documents/infrastructure_plan.md`.

## Repository map

- `CMakeLists.txt`, `CMakePresets.json` — CMake/CTest configuration.
- `vcpkg.json`, `vcpkg-configuration.json` — C++ dependency manifest and pinned registry baseline.
- `scripts/` — infrastructure checks and Linux CI entrypoint.
- `tests/infrastructure/` — historical infrastructure-only test location; product tests live under `tests/core/` and `tests/ui/`.
- `Dockerfile.ci`, `.dockerignore` — Linux validation infrastructure only.
- `.github/workflows/pr-checks.yml`, `.github/workflows/package-*.yml`, `.github/workflows/release.yml` — CI, native artifact builds, and manual packaging-policy validation; no automatic release publication.
- `README.md`, `infrastructure_plan.md` — developer documentation and approved plan.
- `PROGRESS.md`, `docs/decisions/` — resumable status and implementation decisions.
- `src/core/` — explicit core modules for atomic files, standard data paths/migration, instances, game launching, mods, global inventory, dependency resolution, and reviewed download sources.
- `prismatic-launcher/src/qt/` — canonical Qt UI and application entry point.
- `tests/core/`, `tests/ui/` — fixture-based core and offscreen widget tests.
- `.agents/skills/` — local skill instructions.
- `packaging/` — native runtime deployment helpers, offline packaging-policy tests, desktop integration, and reviewed dependency catalog. Backend/API directories remain unimplemented. Historical alternate entry points and old SMAPI utilities remain unbuilt reference sources.

## Required reading and skill selection

Read `infrastructure_plan.md`, this file, and the relevant `.agents/skills/*/SKILL.md` before changing the repository. Use `infra-planner` to revise infrastructure decisions; `infra-builder` for plan-approved tooling; `incremental-implementation` and `test-driven-development` for product changes; `browser-testing-with-devtools` or `test-in-browser` only for browser surfaces; `security-and-hardening` for untrusted inputs or external integrations; `documentation-and-adrs` for durable design records; `code-review-and-quality` before merge; `ci-cd-and-automation` for workflow changes; and `git-workflow-and-versioning` for every change.

## Boundaries

- Preserve the instance isolation contract: game files and saves are shared, profile mods are separate. Never swap or overwrite the game's existing Mods directory.
- Keep development writes inside this experimental worktree, use disposable fixtures, and never execute game/mod binaries in tests. Do not contact GitHub, push, publish, or alter Git remotes.
- Preserve C++20, Qt 6, vcpkg and the documented infrastructure. JSON is the experimental persistence implementation; SQLite is a historical plan, not a shipped feature. Record architectural changes in decisions and progress documentation.
- Default persistence uses Qt AppLocalDataLocation outside application bundles; explicit development roots remain available. Never treat another profile's dependency installation as satisfying the selected profile. Arbitrary update keys do not establish trusted publishers; downloads require explicit user authorization and the safe archive installer. Ordinary tests use mocked providers only.
- Do not commit secrets, signing certificates, generated build output, coverage reports, vcpkg installations, or local game/mod data.
- Docker validates Linux only; it does not replace native Windows/macOS packaging, signing, or macOS notarization.

## Verification

Run these commands from the repository root when their prerequisites are installed:

```bash
./scripts/verify-infrastructure.sh
./scripts/format-check.sh
./scripts/static-analysis.sh
docker build --pull=false -f Dockerfile.ci -t prismaticlauncher-ci:local .
docker run --rm prismaticlauncher-ci:local
```

CI mirrors these checks in `.github/workflows/pr-checks.yml`. No service lifecycle is required: the Docker image is disposable, starts no daemon, exposes no ports, and should be removed with `docker image rm prismaticlauncher-ci:local` when no longer needed.

## Change checklist

1. Read the plan and applicable local instructions.
2. Keep the change within its approved boundary and update documentation when commands, paths, prerequisites, or CI behavior change.
3. Run the relevant local checks and report anything not verified.
4. Inspect `git diff --check` and the changed-file list before handoff.

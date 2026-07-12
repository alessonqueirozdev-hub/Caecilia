<!--
Copyright (c) 2026 Alesson Queiroz. All rights reserved.
Caecilia is proprietary and confidential; unauthorized copying,
distribution, or use of any part is prohibited. See LICENSE.
-->

# Contributing to Caecilia

Caecilia is proprietary, closed-source software (see [LICENSE](LICENSE)).
Contribution is restricted to the copyright owner and expressly authorized
collaborators. These conventions keep the history clean, reviewable, and
professional. **All wording — commits, branches, PRs, comments, docs — is in
English.**

## Commit convention: Conventional Commits

Every commit message follows
[Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <imperative subject, <= 72 chars>

<body: explain WHY, wrapped ~72 cols>

<footer: BREAKING CHANGE / references>
```

### Types

| Type       | Use for                                                        |
|------------|----------------------------------------------------------------|
| `feat`     | A new capability or user-visible feature                       |
| `fix`      | A bug fix                                                       |
| `docs`     | Documentation only                                             |
| `style`    | Formatting / whitespace, no behavior change                    |
| `refactor` | Code change that neither fixes a bug nor adds a feature        |
| `perf`     | A performance improvement                                      |
| `test`     | Adding or correcting tests                                     |
| `build`    | Build system, CMake, or dependency changes                     |
| `ci`       | CI configuration and scripts                                   |
| `chore`    | Routine maintenance that doesn't fit the above                 |

### Scope

The scope is the module the change touches: `core`, `engine`, `synthesis`,
`wind`, `model`, `dsp`, `tuning`, `midi`, `registration`, `control`, `plugin`,
`ui`, or `build` for cross-cutting build changes.

### Examples

```
feat(dsp): add FDN reverb skeleton
fix(engine): guard voice pool exhaustion
docs(ui): describe dual-mode console
perf(synthesis): vectorize additive partial accumulation
build(core): fetch Catch2 only when tests are enabled
```

### Subject rules

- **Imperative mood**: "add", "fix", "guard" — not "added" / "adds".
- **<= 72 characters**, no trailing period.
- Lowercase after the `type(scope):` prefix.

### Body

Explain **why** the change is needed and the consequences of the approach, not
just what changed (the diff already shows what). Wrap at ~72 columns.

### Footer

- Breaking changes: start a footer paragraph with `BREAKING CHANGE:` and
  describe the migration.
- Reference issues/tasks where applicable.

## Branch & PR flow

- `master` is the protected mainline; never commit directly to it.
- Branch per unit of work using the pattern `type/short-topic`, e.g.
  `feature/algorithmic-reverb`, `fix/voice-pool-steal`, `docs/architecture`.
- Open a Pull Request into `master`. The PR description states **what** changed
  and **why**, links related tasks, and calls out RT-safety or license-boundary
  implications.
- Keep PRs focused; prefer several small PRs over one sprawling change.

## Formatting & layering

- Code style is enforced by [`.clang-format`](.clang-format) (LLVM-based,
  C++20). Run `clang-format` before committing; CI will reject unformatted code.
- Respect the strict layering: `caecilia_core` is pure and **JUCE-free**; only
  `plugin` and `ui` may include JUCE. Never allocate, lock, log, or throw on any
  `process()` / `render()` / `step()` audio path.
- Every source, header, and CMake file begins with the exact proprietary header
  block. Never introduce GPL/MIT/BSD or any open-source header text.

# nxe-cedar

[Cedar policy language](https://www.cedarpolicy.com/) library for nginx
modules (NginX Extension Cedar Library), distributed as a git submodule.

## Overview

nxe-cedar implements a subset of the Cedar policy language in C, designed to
be embedded into nginx modules via the nginx core allocator. It is consumed
as a git submodule by `nginx-auth-cedar` and other nginx authorization
modules.

It parses Cedar policy text into an AST and evaluates it against a
request-scoped context (`principal`, `action`, `resource`, `context`) without
spawning a Cedar runtime, depending on Rust, or allocating with
`malloc`/`free`. Every allocation flows through `ngx_pool_t`, so policy ASTs
live on the configuration pool and per-request evaluation state lives on the
request pool.

- **No runtime dependencies.** nginx core only (`ngx_pool_t`, `ngx_str_t`,
  `ngx_array_t`, …). No libcurl, no jansson at runtime, no Rust.
- **Pool-allocated.** Compatible with nginx's lifetime model — AST on
  `cf->pool`, evaluation context on `r->pool`.
- **Cedar-compatible subset.** A subset of the Cedar grammar is implemented.
  The full upstream-Cedar feature matrix with explicit
  Supported / Partial / Not-yet / Out-of-scope status for every operator,
  data type, method, and extension is in [`docs/FEATURES.md`](docs/FEATURES.md).
- **Oracle-tested.** A test-only Rust FFI bridge runs the same inputs through
  the upstream `cedar-policy` crate and compares decisions, so behavior is
  validated against the reference implementation.

## Scope and non-goals

nxe-cedar is the **Cedar evaluator for the nginx process**, not a drop-in
replacement for upstream Cedar. The design is optimized along three axes:

- **`ngx_pool_t`-native.** Policy ASTs and per-request evaluation state live
  on nginx pools. No `malloc`/`free`, no foreign heap to bridge.
- **Submodule-friendly.** Embeds via a single `config.ngx` include. No Rust
  toolchain, no WASM runtime, no extra build dependency on the consuming
  nginx module.
- **Behavioral conformance via differential testing.** Semantic equivalence
  with upstream Cedar is asserted by the FFI oracle (`tests/ffi/`) for every
  case under `tests/cases/`. Coverage against the official `cedar-spec`
  corpus is work in progress.

Explicit **non-goals**:

- **Full Cedar 4.x feature parity.** Policy templates, entity tags, schema
  validation, and the AVP `entityList` legacy format are out of scope today.
  (`datetime` / `duration` are now supported.) The per-feature status is in
  [`docs/FEATURES.md`](docs/FEATURES.md).
- **Formal verification of the C implementation.** The upstream Lean model
  does not extend here, and machine-checked Lean-to-C is not viable. The
  guarantee offered is *behavioral* conformance, not *formal* conformance.
- **A drop-in replacement for AWS Verified Permissions or
  [`cedar-local-agent`](https://github.com/cedar-policy/cedar-local-agent).**
  If strict Cedar conformance, formal verification, the full AVP API
  surface, or policy-store synchronization is a requirement, use one of
  those instead.

**Fit:** authoring Cedar-style authorization policies for nginx request
handling, in a submodule that ships without a Rust toolchain. Other Cedar
deployment shapes (AWS-resident control planes, language SDKs, formally
verified evaluators) are better served by the upstream Rust SDK or AVP.

## Feature matrix (summary)

| Area | Headline features |
| --- | --- |
| Policy structure | `permit` / `forbid`, scope (`==` / `in` / `is` on principal / action / resource), `when` / `unless`, annotations (`@id`, `@advice`, …) |
| Operators | logical / equality, numeric ordering (`<`, `<=`, `>`, `>=`), arithmetic (`+`, `-`, `*`) on `Long`, `if … then … else …`, `has`, `like` with `*` wildcard, `is` / `is … in …` |
| Data / access | set / entity / record literals, attribute access (dot and bracket `expr["key"]`), nested record attribute access |
| Extension types | `ip("…")` (`isInRange`, `isIpv4` / `isIpv6` / `isLoopback` / `isMulticast`), `decimal("…")` (`lessThan` / `lessThanOrEqual` / `greaterThan` / `greaterThanOrEqual`), `datetime("…")` / `duration("…")` |
| Set methods | `contains`, `containsAll`, `containsAny`, `isEmpty` |

For the exhaustive feature table — every Cedar operator, type, method, and
extension annotated with Supported / Partial / Not-yet / Out-of-scope — see
[`docs/FEATURES.md`](docs/FEATURES.md). A per-commit feature history lives in
[`CHANGELOG.md`](CHANGELOG.md).

## Repository layout

```
src/
  nxe_cedar_types.h    — All data structures (tokens, AST nodes, scopes, eval ctx)
  nxe_cedar_util.h     — Inline helpers (string equality, etc.)
  nxe_cedar_lexer.c/h  — Tokenizer + shared escape decoder
  nxe_cedar_parser.c/h — Recursive-descent parser, text → AST
  nxe_cedar_expr.c/h   — AST walker, value representation, method dispatch
  nxe_cedar_eval.c/h   — Policy-set evaluation (forbid-priority), public API
tests/
  Makefile             — C unit tests (test / test-asan)
  test_runner.c        — Native test runner (jansson-backed JSON cases)
  ngx_compat/          — malloc-backed stubs for nginx types
  ffi/                 — Rust Cedar FFI oracle (test-only, Cargo project)
  cases/               — Policy + context + expected decision fixtures
```

## Integration with an nginx module

nxe-cedar is not built as a standalone `.a`/`.so`. The parent module sources
`config.ngx`, which exports the source list, include paths, and dependency
headers. A typical `nginx-auth-cedar/config` fragment:

```sh
nxe_cedar_dir="$ngx_addon_dir/nxe-cedar"
. "$nxe_cedar_dir/config.ngx"

ngx_module_deps="$nxe_cedar_module_deps $ngx_addon_dir/src/..."
ngx_module_incs="$nxe_cedar_module_incs $ngx_addon_dir/src"
ngx_module_srcs="$nxe_cedar_module_srcs $ngx_addon_dir/src/..."
ngx_module_libs="$nxe_cedar_module_libs"
```

## Quick start

```c
#include <ngx_core.h>
#include "nxe_cedar_eval.h"

static ngx_int_t
authorize(ngx_pool_t *pool, ngx_log_t *log, ngx_str_t *policy_text,
    ngx_str_t *user_id)
{
    nxe_cedar_policy_set_t *ps = nxe_cedar_parse(pool, log, policy_text);
    if (ps == NULL) {
        return NGX_ERROR;
    }

    nxe_cedar_eval_ctx_t *ctx = nxe_cedar_eval_ctx_create(pool);
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_str_t user_type = ngx_string("User");
    ngx_str_t action_type = ngx_string("Action");
    ngx_str_t action_id = ngx_string("GET");
    ngx_str_t resource_type = ngx_string("Endpoint");
    ngx_str_t resource_id = ngx_string("/api/v1/users");

    nxe_cedar_eval_ctx_set_principal(ctx, &user_type, user_id);
    nxe_cedar_eval_ctx_set_action(ctx, &action_type, &action_id);
    nxe_cedar_eval_ctx_set_resource(ctx, &resource_type, &resource_id);

    ngx_str_t role_attr = ngx_string("role");
    ngx_str_t role_val = ngx_string("admin");
    (void) nxe_cedar_eval_ctx_add_principal_attr(ctx, &role_attr, &role_val);

    nxe_cedar_decision_t decision = nxe_cedar_eval(ps, ctx, log);
    return decision == NXE_CEDAR_DECISION_ALLOW ? NGX_OK : NGX_HTTP_FORBIDDEN;
}
```

A matching policy:

```cedar
permit (
    principal,
    action == Action::"GET",
    resource
) when {
    principal.role == "admin"
};
```

Use `nxe_cedar_eval_detail()` if you need the policies that determined the
decision (for example, to log a `@id` / `@advice` annotation on deny).

## Evaluation model

Standard Cedar `forbid`-priority evaluation:

1. Every policy is evaluated.
2. If any `forbid` matches → **DENY**.
3. Otherwise, if any `permit` matches → **ALLOW**.
4. Otherwise → **DENY** (default deny).

Entity hierarchies for `in` are not resolved internally. The caller injects
the transitive closure of each entity's ancestors via
`nxe_cedar_eval_ctx_add_{principal,action,resource}_parent()`. The reflexive
case (`X in X`) is automatic.

## Building and testing

nxe-cedar itself does not build alone; for the nginx integration build, see
`nginx-auth-cedar`. The repository ships two test runners:

```sh
# C unit tests (jansson required, Rust not required)
cd tests
make test                                  # all cases
make test-asan                             # with AddressSanitizer

# FFI oracle tests (compare C implementation against upstream cedar-policy)
cd tests/ffi
cargo test                                 # oracle only (skips C vs oracle)
cargo test -- --include-ignored            # include the C vs oracle diff
```

The FFI bridge and the Rust dependency are **test-only** — they never reach
the nginx build artifact.

## Formatting

Format with uncrustify using the shipped configuration:

```sh
uncrustify -c .uncrustify.cfg -l c --no-backup --replace --mtime src/<file>.c
```

Indentation is four spaces, line width 80 columns, nginx-style braces.

## License

MIT. See [`LICENSE`](LICENSE).

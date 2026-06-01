# nxe-cedar Feature Support

This document enumerates every feature in the [Cedar policy language
reference](https://docs.cedarpolicy.com/) and records whether nxe-cedar
supports it. The intent is to give integrators a single place to confirm
whether a Cedar policy will be accepted and evaluated identically against the
upstream `cedar-policy` crate.

## Status legend

| Symbol | Meaning |
| --- | --- |
| ✅ | Supported. Behavior matches the upstream `cedar-policy` reference (verified via the FFI oracle for cases under `tests/cases/`). |
| ⚠️ | Partially supported. The feature works but with a documented restriction. See the Notes column. |
| ❌ | Not yet supported. Implementing it is consistent with the project scope and may be added in a future release. |
| 🚫 | Out of scope. Will not be implemented; alternatives are noted. |

A per-commit feature history is in [`CHANGELOG.md`](../CHANGELOG.md).

## Policy structure

| Feature | Syntax | Status | Notes |
| --- | --- | --- | --- |
| `permit` effect | `permit (scope) conditions;` | ✅ |  |
| `forbid` effect | `forbid (scope) conditions;` | ✅ |  |
| `when` clause | `when { expr }` | ✅ | multiple clauses AND-combined |
| `unless` clause | `unless { expr }` | ✅ | multiple clauses AND-combined |
| Annotations | `@key`, `@key("value")` | ✅ | up to 16 per policy; duplicate keys rejected at parse time |
| Line comments | `// …` | ✅ |  |
| Block comments | `/* … */` | ❌ | Not implemented; Cedar reference parser accepts `//` only as well |

## Scope constraints

| Constraint | Syntax | Status | Notes |
| --- | --- | --- | --- |
| Unconstrained | `principal,` | ✅ |  |
| Equality | `principal == Entity::"id"` | ✅ | also `action`, `resource` |
| Hierarchy | `principal in Entity::"id"` | ✅ | ancestors injected by caller (see [Entity hierarchy](#entity-hierarchies)) |
| Action set | `action in [Action::"a", Action::"b"]` | ✅ | **action only** — non-entity elements rejected at parse time |
| Type check | `principal is Type` | ✅ | principal/resource only; rejected on action |
| Type + hierarchy | `principal is Type in Entity::"id"` | ✅ | principal/resource only |
| Namespaced type | `principal is NS::Sub::Type` | ✅ |  |

## Data types

| Type | Syntax / constructor | Status | Notes |
| --- | --- | --- | --- |
| `Bool` | `true`, `false` | ✅ |  |
| `Long` | `42`, `-100` | ✅ | pinned to `int64_t` for cross-platform i64 integrity; overflow surfaces as evaluation error |
| `String` | `"..."` | ✅ | escapes `\n` `\r` `\t` `\\` `\"` `\'` `\xHH` `\u{…}`; `\*` allowed only in `like` patterns |
| `Set` | `[expr, …]` | ✅ | in policy text; element-injection API also available |
| `Record` | `{key: expr, …}` in policy text; injection API for ctx | ✅ | up to 64 entries; up to 16 depth; trailing comma accepted |
| `Entity` | `Type::"id"`, `NS::Type::"id"` | ✅ | namespaced types supported |
| `ipaddr` | `ip("…")` | ✅ | IPv4 / IPv6 / CIDR; dot-notation IPv4-mapped IPv6 rejected per Cedar spec |
| `decimal` | `decimal("d.d")` | ✅ | i64-backed with scale 10^4; range −922337203685477.5808 to 922337203685477.5807 |
| `datetime` | `datetime("…")` | ✅ | i64 UTC epoch ms; `YYYY-MM-DD` or `YYYY-MM-DDThh:mm:ss(.SSS)?(Z\|±hhmm)`; timezone designator mandatory when a time is present; distinct type from `Long` |
| `duration` | `duration("…")` | ✅ | signed i64 ms; `[-]?` then `d`/`h`/`m`/`s`/`ms` units in descending order, each at most once, ≥1 unit; distinct type from `Long` |

## Variables

| Variable | Status | Notes |
| --- | --- | --- |
| `principal` | ✅ |  |
| `action` | ✅ | attribute injection (`add_action_attr_*`) supported |
| `resource` | ✅ |  |
| `context` | ✅ | usable both as `context.attr` and as a whole record value (`==`, `!=`, `has`); an unset context is the empty record |

## Comparison operators

| Operator | Operand types | Status | Notes |
| --- | --- | --- | --- |
| `==` | any | ✅ | total function: a type mismatch is `false`, never an error; sets and records compare order-independently with bijective matching |
| `!=` | any | ✅ | total function: a type mismatch is `true`, never an error |
| `<` `<=` `>` `>=` | `Long` | ✅ |  |
| `<` `<=` `>` `>=` | `datetime`, `duration` | ✅ | Both operands must share the same type (`datetime` with `datetime`, `duration` with `duration`); mixing with `Long` or each other is an error |
| `.lessThan` `.lessThanOrEqual` `.greaterThan` `.greaterThanOrEqual` | `decimal` | ✅ |  |

## Logical operators

| Operator | Status | Notes |
| --- | --- | --- |
| `&&` | ✅ | short-circuit per Cedar spec |
| `\|\|` | ✅ | short-circuit per Cedar spec |
| `!` | ✅ |  |
| `if-then-else` | ✅ | only the selected branch is evaluated |

## Arithmetic operators

| Operator | Operand types | Status | Notes |
| --- | --- | --- | --- |
| `+` | `Long + Long` | ✅ | overflow → evaluation error |
| `-` (binary) | `Long - Long` | ✅ | overflow → evaluation error |
| `*` | `Long * Long` | ✅ | overflow → evaluation error; `INT64_MIN * -1` rejected |
| `-` (unary) | `Long` | ✅ |  |

## String operators

| Operator | Status | Notes |
| --- | --- | --- |
| `like` | ✅ | `*` = zero-or-more, `\*` = literal `*`; `\x2A` / `\u{2A}` treated as wildcards per Cedar spec |

## Hierarchy operator

| Form | Status | Notes |
| --- | --- | --- |
| `entity in entity` | ✅ | reflexive matching + ancestor lookup; caller injects ancestors via `nxe_cedar_eval_ctx_add_{principal,action,resource}_parent()` |
| `entity in [entity, …]` | ✅ | scope is action-only / expression-level RHS validates all set elements are entities |

## Type-check operator

| Form | Status | Notes |
| --- | --- | --- |
| `expr is Type` (expression) | ✅ | LHS must be entity-typed, else evaluation error |
| `expr is Type in expr` (expression) | ✅ |  |
| `principal is Type` (scope) | ✅ |  |
| `principal is Type in entity_ref` (scope) | ✅ |  |

## Attribute / record / tag operators

| Operator | Form | Status | Notes |
| --- | --- | --- | --- |
| `.attr` (dot access) | `expr.ident` | ✅ |  |
| `["key"]` (bracket access) | `expr["X-Header"]` | ✅ | only string literals accepted inside `[ ]` |
| Nested access | `expr.a.b`, `expr["a"].b`, `expr.a["b"]` | ✅ | up to `NXE_CEDAR_MAX_MEMBER_CHAIN` = 16 |
| `has` (single key) | `expr has ident`, `expr has "string"` | ✅ |  |
| `has` (nested path) | `expr has a.b.c` | ⚠️ | Single identifier RHS only; chain not parsed. Workaround: chain explicit `has` with `&&` |
| `.hasTag(string)` | entity tag presence | ❌ | Entity tags not implemented. Use a record-valued attribute as a workaround |
| `.getTag(string)` | entity tag value | ❌ | See `.hasTag` |

## Set methods

| Method | Receiver | Status | Notes |
| --- | --- | --- | --- |
| `.contains(elt)` | Set | ✅ | argument may be any type; type mismatch returns `false`, not error |
| `.containsAll(set)` | Set | ✅ | both operands must be sets |
| `.containsAny(set)` | Set | ✅ | both operands must be sets |
| `.isEmpty()` | Set | ✅ |  |

## `ipaddr` methods

| Method | Status | Notes |
| --- | --- | --- |
| `.isInRange(ipaddr)` | ✅ | receiver CIDR must be at least as specific as argument range; family mismatch → `false` |
| `.isIpv4()` | ✅ |  |
| `.isIpv6()` | ✅ |  |
| `.isLoopback()` | ✅ | receiver CIDR must be entirely within `127.0.0.0/8` or `::1/128` |
| `.isMulticast()` | ✅ | receiver CIDR must be entirely within `224.0.0.0/4` or `ff00::/8` |

## `decimal` methods

| Method | Status | Notes |
| --- | --- | --- |
| `.lessThan(decimal)` | ✅ |  |
| `.lessThanOrEqual(decimal)` | ✅ |  |
| `.greaterThan(decimal)` | ✅ |  |
| `.greaterThanOrEqual(decimal)` | ✅ |  |

## `datetime` / `duration` methods

| Method | Status | Notes |
| --- | --- | --- |
| `.offset(duration)` | ✅ | Returns a `datetime`; i64 overflow is an evaluation error |
| `.durationSince(datetime)` | ✅ | Returns a signed `duration` (receiver − argument) |
| `.toDate()` | ✅ | Truncates to 00:00:00 UTC of the same day |
| `.toTime()` | ✅ | Returns a `duration`: milliseconds since `.toDate()` |
| `.toMilliseconds()` `.toSeconds()` `.toMinutes()` `.toHours()` `.toDays()` | ✅ | Return `Long`; division truncates toward zero |

## Extension constructors

| Constructor | Status | Notes |
| --- | --- | --- |
| `ip("…")` | ✅ |  |
| `decimal("…")` | ✅ | grammar `[-]?d+\.d{1,4}` strictly enforced |
| `datetime("…")` | ✅ | Eagerly validated at injection time; argument grammar enforced strictly |
| `duration("…")` | ✅ | Eagerly validated at injection time; argument grammar enforced strictly |

## Entity attribute / hierarchy injection (API surface)

| Capability | API | Status |
| --- | --- | --- |
| Set principal / action / resource | `nxe_cedar_eval_ctx_set_{principal,action,resource}` | ✅ |
| Scalar attributes (String / Long / Bool / IP / Decimal) | `nxe_cedar_eval_ctx_add_*_attr{,_long,_bool,_ip,_decimal}` | ✅ |
| Set-valued attributes | `nxe_cedar_eval_ctx_add_*_attr_set` + `nxe_cedar_set_add_*` | ✅ |
| Record-valued attributes (nested) | `nxe_cedar_eval_ctx_add_*_attr_record` + `nxe_cedar_record_add_*` | ✅ |
| Entity-valued attributes | `nxe_cedar_eval_ctx_add_*_attr_entity` | ✅ |
| Datetime / Duration attributes | `nxe_cedar_eval_ctx_add_*_attr_{datetime,duration}` + `nxe_cedar_record_add_{datetime,duration}` + `nxe_cedar_set_add_{datetime,duration}` | ✅ |
| Entity tags | — | ❌ |
| Entity ancestor injection | `nxe_cedar_eval_ctx_add_{principal,action,resource}_parent` | ✅ |
| Entity literal attribute / `has` / `in` resolution | — | ✅ A literal `Foo::"id"` that names the principal / action / resource resolves attributes, `has`, and `in` through that request entity. A literal naming any other entity has no store, so attribute / `has` access errors (policy not applicable) |
| External entity store / dynamic hierarchy resolution | — | 🚫 Caller injects the transitive closure of ancestors; nxe-cedar does not query a store |

## Advanced Cedar features (out of scope)

These are intentionally **not** in scope for nxe-cedar. Each row notes why and
the recommended alternative.

| Feature | Status | Why / alternative |
| --- | --- | --- |
| Schema validation | 🚫 | Out of scope for runtime evaluation. Run static checks with the official Cedar CLI before deploying policies |
| Template-linked policies (`?principal`, `?resource`) | 🚫 | nginx ingress-time policies are static. Inline the placeholder via a context attribute if needed |
| Partial evaluation (`is_authorized_partial`) | 🚫 | All inputs are available at the time the nginx module evaluates the request |
| Multi-file namespace separation | 🚫 | A single policy set is sufficient for the nginx authorization use case |
| Record literals outside `when`/`unless` (e.g. in scope) | 🚫 | Cedar reference parser also rejects this; record literals are only meaningful in conditions |
| Dynamic entity hierarchy from an external store | 🚫 | Caller injects ancestors with `*_parent()` APIs after computing the transitive closure |

## Evaluation model

| Aspect | Status | Notes |
| --- | --- | --- |
| `forbid` priority | ✅ | Any matching `forbid` → DENY; otherwise any matching `permit` → ALLOW; otherwise DENY |
| Default deny | ✅ | Empty policy set evaluates to DENY |
| Determining policies in result | ✅ | `nxe_cedar_eval_detail()` returns the policies that caused the decision (use to log `@id` / `@advice`) |
| Recursion depth guard | ✅ | Expression evaluator caps at `NXE_CEDAR_MAX_EVAL_DEPTH = 128`; parser caps at `NXE_CEDAR_MAX_PARSE_DEPTH = 64` |
| Order-independent record / set equality | ✅ | Bijective bitmap matching; containers larger than 1024 elements return error rather than degrade |
| Eager extension validation on injection | ✅ | `ip()`, `decimal()`, `datetime()`, and `duration()` constructors validate at injection time; invalid input returns `NGX_ERROR` instead of a deferred runtime error |

## Entity hierarchies

Cedar resolves `entity in entity` over a hierarchy graph. nxe-cedar does
**not** maintain that graph internally; the caller is responsible for
computing the transitive closure of every relevant entity's ancestors and
injecting them via:

- `nxe_cedar_eval_ctx_add_principal_parent(ctx, type, id)`
- `nxe_cedar_eval_ctx_add_action_parent(ctx, type, id)`
- `nxe_cedar_eval_ctx_add_resource_parent(ctx, type, id)`

The reflexive case (`X in X`) is automatic. This design keeps nxe-cedar
free of any entity-store dependency and matches the lifecycle of an nginx
request where the caller already knows the user's group memberships.

## Cross-platform integer width

`Long` values use `int64_t` everywhere — AST literals, runtime values,
attribute storage, and the public API for long attribute injection. This
preserves Cedar's i64 semantics on 32-bit platforms where `ngx_int_t`
(`intptr_t`) would otherwise collapse to 32 bits.

# Changelog

## [b5ee6ac](../../commit/b5ee6ac) - 2026-06-01

### Added

- Resolve attribute / `has` / `in` access on request entity literals
  - An entity literal `Foo::"id"` that names the principal, action, or resource now resolves attribute access, `has`, and `in` through that request entity, matching reference Cedar which resolves the literal via the entity store. Previously the literal evaluated to an entity value carrying no slot tag, so the attribute-access and `has` slow paths rejected it as a non-record (evaluation error → policy not applicable) and diverged from the oracle whenever a policy referenced a request entity by literal instead of by the `principal` / `action` / `resource` keyword
  - `NXE_CEDAR_NODE_ENTITY_REF` evaluation stamps the value with the matching request slot via the new `nxe_cedar_entity_request_slot()` (principal is checked first as the deterministic tie-break on a cross-slot `(type, id)` collision); the attribute-access and `has` slow paths then resolve a slot-bearing entity through `nxe_cedar_resolve_slot_attrs()`. A literal that names no request entity keeps `SLOT_NONE`, so attribute / `has` access still errors and `in` matches reflexively only — the same decision the oracle reaches, since its entity store also lacks the literal. The slot is cleared at composite boundaries as before, so derived entities (record-pulled, if-then-else results) are unchanged
  - A new `tests/cases/entity_literal_access.json` covers attribute and `has` access on principal / action / resource literals, the literal-equals-keyword identity, the `has`-guarded attribute-access form, and the negative cases (a literal naming no request entity erroring on attribute access and on a result-consuming `!=`); all are oracle-parity

## [cc744bf](../../commit/cc744bf) - 2026-06-01

### Fixed

- Treat `==` / `!=` over mismatched operand types as `false` / `true`
  - Cedar's `==` and `!=` are total functions: comparing two values of different types is not an error but simply "not equal", so `==` yields `false` and `!=` yields `true`. The expression evaluator instead returned an evaluation error on a type mismatch via an early `left.type != right.type` guard placed before `nxe_cedar_value_equals()` in both the `NXE_CEDAR_OP_EQ` and `NXE_CEDAR_OP_NE` arms
  - The guard is removed for both operators; `nxe_cedar_value_equals()` already returns `0` (not equal) for mismatched types, so delegating to it produces `false` for `==` and `true` for `!=`, while the preceding `RVAL_ERROR` propagation guards on each operand are kept intact. The ordering operators (`<` `<=` `>` `>=`) are unaffected and still require matching `datetime` / `duration` types
  - The divergence stayed latent while a comparison was the top-level `when` condition (error and `false` both deny), but surfaced when the result was consumed by another operator — e.g. `(1 == duration("0ms")) == false` evaluates to `allow` in Cedar yet denied in nxe-cedar. A new `tests/cases/equality_type_mismatch.json` covers result-consuming forms (`(a == b) == false`, top-level `!=`, `!((a == b))`) across `long`×`string`, `long`×`duration`, `string`×`datetime`, `bool`×`long`, `set`×`long`, and `record`×`duration`

## [026f9bb](../../commit/026f9bb) - 2026-05-29

### Added

- Implement Cedar `datetime` / `duration` extension types
  - The lexer recognizes `datetime` / `duration` as keywords (still usable as attribute identifiers like `ip` / `decimal`), and the parser produces `NXE_CEDAR_NODE_DATETIME_LITERAL` / `NXE_CEDAR_NODE_DURATION_LITERAL` from the `ip()`-style constructor shape; runtime values gain `NXE_CEDAR_RVAL_DATETIME` (UTC epoch milliseconds) and `NXE_CEDAR_RVAL_DURATION` (signed milliseconds), both backed by `int64_t` and kept distinct from `Long` with no implicit conversion
  - `nxe_cedar_make_datetime()` parses ISO 8601 — either `YYYY-MM-DD` or `YYYY-MM-DDThh:mm:ss(.SSS)?(Z|±hhmm)` — validating field ranges with leap-year-aware day limits and computing epoch days via the proleptic Gregorian `days_from_civil` algorithm; a time component requires a timezone designator and a bare trailing `T` is rejected, matching the `cedar-policy` 4.9.1 reference. `nxe_cedar_make_duration()` parses `[-]?` followed by `d`/`h`/`m`/`s`/`ms` unit groups in strictly descending order (each at most once, at least one present), distinguishing `m` from `ms` by a trailing `s` and rejecting overflow
  - The `datetime` methods `offset(duration)`, `durationSince(datetime)`, `toDate()`, and `toTime()` and the `duration` conversions `toMilliseconds()` / `toSeconds()` / `toMinutes()` / `toHours()` / `toDays()` are dispatched in the expression evaluator; `offset` and `durationSince` surface i64 overflow as an evaluation error, and the conversions truncate toward zero
  - The `<`, `<=`, `>`, `>=` operators now accept two `datetime` or two `duration` operands (the scaled i64 representation is order-preserving) in addition to `Long`; mixing types remains an evaluation error, and `==` / `!=` compare the underlying representation so canonical-form equality holds (e.g. a date-only value equals the same instant written with an explicit `Z`)
  - The public injection API gains `nxe_cedar_eval_ctx_add_{principal,action,resource,context}_attr_{datetime,duration}()`, `nxe_cedar_record_add_{datetime,duration}()`, and `nxe_cedar_set_add_{datetime,duration}()`; each takes the constructor string and validates it eagerly through the same parser, rejecting malformed input with `NGX_ERROR` so callers cannot inject a deferred `RVAL_ERROR` attribute

## [4d8085e](../../commit/4d8085e) - 2026-05-28

### Fixed

- Guard `nxe_cedar_make_ip()` against NULL input
  - The length-bound check at the top of the function (`s->len == 0 || s->len > 43`) dereferenced `s` before any NULL test, so a NULL pointer reaching this entry point — declared in the public header `nxe_cedar_expr.h` — would crash the process; the sibling builder `nxe_cedar_make_decimal()` already returned `RVAL_ERROR` on NULL, so the two parallel value builders now share the same defensive contract
  - All current callers (`NODE_IP_LITERAL` evaluation in `nxe_cedar_expr.c` and the eager `nxe_cedar_eval_ctx_add_*_attr_ip` injection path) feed in addresses of struct members and cannot pass NULL today, so this is a defense-in-depth fix for new callers that may grow against the public symbol in the future
  - A new C unit test `unit/make_value_null_guard` covers `make_ip(NULL)`, `make_ip(&{NULL, 0})` and `make_decimal(NULL)` to keep both builders aligned

## [f9254b5](../../commit/f9254b5) - 2026-05-26

### Added

- Implement `nxe_cedar_eval_detail()` diagnostic API (issue #028)
  - The new entry point evaluates a policy set and writes the matching policies into a caller-supplied `nxe_cedar_decision_detail_t`: every matching `forbid` when at least one fired (the decision is DENY), every matching `permit` on ALLOW, and an empty list (`policies = NULL`, `npolicies = 0`) on default-deny when no policy matched
  - `nxe_cedar_eval()` becomes a thin wrapper that passes `out = NULL`, so existing callers that only need the decision keep the previous early-exit fast path; the detail-collecting path enumerates every matching policy because the API contract is to return all contributing policies, not just the first
  - A companion `nxe_cedar_policy_get_annotation(policy, key)` helper returns the value `ngx_str_t` for a given annotation key (or `NULL` when absent / valueless distinction preserved via empty-string values), so callers can lift `@id` / `@advice` off the returned policies for audit logging without rolling their own annotation walk
  - The pointer arrays inside the detail struct are allocated from `ctx->pool` and share the policy set's lifetime; an allocation failure mid-collection still returns a correct decision (the rest of the list is simply truncated)

## [43cbdcf](../../commit/43cbdcf) - 2026-05-22

### Added

- Implement Cedar `decimal(...)` extension type (issue #004)
  - The lexer now recognizes `decimal` as a Phase 3 keyword (reusable as an attribute identifier alongside `ip`); the parser produces `NXE_CEDAR_NODE_DECIMAL_LITERAL` for `decimal("d.d")` reusing the `ip()` token / parentheses / string-arg shape, so the AST surface grows by one well-bounded node and one keyword
  - Runtime values gain `NXE_CEDAR_RVAL_DECIMAL` backed by `int64_t` with an implicit scale of 10^4 — `"1.23"` becomes `12300`, `"-0.0001"` becomes `-1`, and `nxe_cedar_make_decimal()` enforces the Cedar grammar (`[-]?d+\.d{1,4}`) with overflow detection on `int_part * 10000 + frac_part` so malformed or out-of-range inputs surface as `RVAL_ERROR` instead of silently truncating
  - The four ordering methods `lessThan`, `lessThanOrEqual`, `greaterThan`, `greaterThanOrEqual` evaluate via direct i64 comparison on the scaled representation; the `<`, `>`, `<=`, `>=` operators remain Long-only (Cedar exposes decimal ordering only through methods, never through binary operators), so the type system stays consistent
  - The public injection API gains `nxe_cedar_eval_ctx_add_{principal,action,resource,context}_attr_decimal()`, `nxe_cedar_record_add_decimal()`, and `nxe_cedar_set_add_decimal()`; each validates the input string at insertion time through `nxe_cedar_make_decimal()` and rejects malformed decimals with `NGX_ERROR`, mirroring the eager IP injection path so callers cannot accidentally inject a "deferred RVAL_ERROR" attribute

## [b14263b](../../commit/b14263b) - 2026-05-21

### Fixed

- Reject duplicate keys in the attribute / record injection API (issue #008, injection side)
  - The parser refused duplicate keys in record literals at parse time, but the runtime injection helpers (`nxe_cedar_eval_ctx_add_*_attr*`, `nxe_cedar_record_add_*`) used `ngx_array_push()` unconditionally — a second `add_*_attr()` call with the same name silently stacked a second tuple on the entity / context / record, breaking the uniqueness invariant that equality and member access assume
  - The new internal helper `nxe_cedar_attrs_has_name()` guards every shared add path (`add_str_attr` / `add_long_attr` / `add_bool_attr` / `add_ip_attr` / `add_record_attr` / `add_set_attr` / `add_entity_attr`); the check covers entity-level attributes (principal / action / resource / context) and record fields uniformly, including the cross-kind collision case (e.g. `str` followed by `long` under the same name)
  - Duplicate insertions now return `NGX_ERROR` / `NULL` before any push so the caller can surface the failure, and downstream value walks can assume the parser contract

## [e05a53b](../../commit/e05a53b) - 2026-05-21

### Added

- Cap expression-evaluator recursion depth (issue #007)
  - `nxe_cedar_expr_eval()` is now a thin wrapper that increments / decrements a new `eval_depth` counter on `nxe_cedar_eval_ctx_t` around the real body (`nxe_cedar_expr_eval_body`); every internal recursive call goes through the public name so the guard trips on each re-entry, no matter which helper function (attribute access, method call, binop, …) triggered it
  - `NXE_CEDAR_MAX_EVAL_DEPTH = 128` (≈2× the parser's `NXE_CEDAR_MAX_PARSE_DEPTH = 64`) leaves plenty of headroom for any AST the parser accepts while staying well under typical thread stack sizes; exceeding the limit short-circuits to `RVAL_ERROR`, which propagates to the policy as deny

### Fixed

- Harden record / set equality with bijective matching (issue #008, equality side)
  - The previous `nxe_cedar_value_equals()` walk over records and sets was a one-sided linear scan that could return `true` for cases like `{x:1, x:1} == {x:1, y:2}` once a duplicate key slipped past the injection API
  - The match now tracks the consumed indices on the `b` side with a stack `uint64_t` bitmap (`NXE_CEDAR_VALUE_EQUALS_MAX_ELTS = 1024`, well above `NXE_CEDAR_MAX_RECORD_ENTRIES = 64` and `NXE_CEDAR_MAX_SET_ELEMENTS = 256`); containers larger than the bitmap return `NGX_ERROR` rather than degrade to a sloppy match
  - Record equality additionally short-circuits to `false` when a name matches but the value differs, since unique keys mean there is no second chance to satisfy `a[i]`

## [ad67aca](../../commit/ad67aca) - 2026-05-21

### Docs

- Document `nxe_cedar_parse()` NULL preconditions (issue #006)
  - The public header now states that `pool` / `log` / `text` are required and that the entry point returns `NULL` on any `NULL` argument without dereferencing it
  - The runtime guard was already in place; this only formalizes the contract for callers and is paired with the new `unit/parse_null_guard` test cases that exercise each argument

## [f86d785](../../commit/f86d785) - 2026-05-20

### Fixed

- Clear entity slot tag at composite-expression boundaries (issue #024)
  - The slot tag introduced for #023 (`NXE_CEDAR_ENTITY_SLOT_{PRINCIPAL,ACTION,RESOURCE}`) was stamped on entity values produced by `NXE_CEDAR_NODE_VAR` but copied unchanged through record literals, set literals, and if-then-else results because `nxe_cedar_value_t` flows by value
  - A derived entity such as `({p: principal}).p` therefore still carried `slot=PRINCIPAL` into `in` evaluation, and `lookup_parents()` returned `principal_parents` — flipping permit/forbid decisions whenever a composite expression appeared on the left of `in`
  - `nxe_cedar_clear_entity_slot()` now strips the slot back to `NXE_CEDAR_ENTITY_SLOT_NONE` at three composite boundaries: set element insertion, record entry insertion, and if-then-else result return
  - After clearing, `lookup_parents` falls back to reflexive comparison only — matching the semantics for derived entities that no longer have a syntactic chain to the principal / action / resource keyword
  - The fix is localized to `nxe_cedar_expr_eval()`; expression-evaluator signatures, the public API context manipulators, and the value union layout are unchanged

## [1bf018f](../../commit/1bf018f) - 2026-05-20

### Fixed

- Disambiguate the `in` operator by entity origin slot (issue #023)
  - `nxe_cedar_eval_ctx_lookup_parents()` resolved an entity's ancestor list by `(type, id)` linear scan, so it always returned the first matching slot; when principal / action / resource shared the same identity, `resource in <group>` would silently consult `principal_parents` and could flip permit/deny
  - The scope path was unaffected (it passes the per-slot parents array directly), but the expression-level `in` inside `when` / `unless` and the `is T in expr` expression went through `lookup_parents` and were vulnerable
  - `nxe_cedar_value_t.v.entity` now carries a `slot` tag (`NXE_CEDAR_ENTITY_SLOT_{NONE,PRINCIPAL,ACTION,RESOURCE}`); `NXE_CEDAR_NODE_VAR` evaluation stamps the slot on the produced value, and `lookup_parents()` switches on it instead of probing identities
  - Derived entities (literals, attribute lookups, set elements) inherit `NXE_CEDAR_ENTITY_SLOT_NONE` via `ngx_memzero`, so `lookup_parents` returns NULL for them and `in` falls back to reflexive comparison only — matching Cedar's semantics for entities that carry no ancestor information
  - The fix is localized to the value union, the VAR evaluation case, and `lookup_parents()`; expression-evaluator signatures and the public context manipulators stay unchanged

## [c8debca](../../commit/c8debca) - 2026-05-20

### Added

- Inject set- and entity-valued attributes (issues #002, #019)
  - New public APIs `nxe_cedar_eval_ctx_add_*_attr_{set,entity}`, `nxe_cedar_record_add_{set,entity}`, and `nxe_cedar_set_add_{str,long,bool,ip,entity,set,record}` complete the previously-missing leg of the injection surface, so callers can pass `principal.scopes`, `principal.manager`, set-of-entities bag attributes, etc. without flattening through strings
  - Set element type-checking happens at injection time: `set_add_ip` parses through `nxe_cedar_make_ip()` and rejects malformed strings with `NGX_ERROR`, mirroring scalar IP behavior
  - Nested set values share the existing record depth ceiling via the new alias `NXE_CEDAR_MAX_SET_DEPTH = NXE_CEDAR_MAX_RECORD_DEPTH`; `set_add_record()` / `record_add_set()` inherit the parent's depth instead of restarting at 1, so any mixed graph respects one limit regardless of how kinds alternate
  - `nxe_cedar_attr_t` was already `{name, value_t}` after the Phase C unification, so no value-type ID or storage-layout change is needed — only the injection path was missing

## [8c26a51](../../commit/8c26a51) - 2026-05-20

### Added

- Implement entity hierarchy for the `in` operator (issues #001, #003, #010, #020)
  - Callers register the transitive closure of an entity's ancestors via the new public APIs `nxe_cedar_eval_ctx_add_{principal,action,resource}_parent()`; the evaluator then resolves `principal in Group::"admins"` and friends without falling back to `==`
  - Coverage spans the `principal/action/resource in entity_ref` scope form, the action-only `action in [Action::"a", Action::"b"]` set form, the expression-level `in` inside `when`/`unless`, and the `is T in expr` scope/expression — all share one helper (`nxe_cedar_entity_in_target`) so reflexive `X in X` and ancestor lookup stay in lockstep
  - `nxe_cedar_eval_ctx_t` carries three ancestor arrays (`principal_parents` / `action_parents` / `resource_parents`); the previously implicit `SCOPE_EQ` / `SCOPE_IN` overlap is now dispatched separately so `==` keeps strict equality and `in` is the only path that consults parents
  - `entity in set` is position-independent: every element is type-checked before deciding, so `[matching_entity, 1]` and `[1, matching_entity]` both surface the ERROR from the non-entity element (Cedar requires homogeneous RHS for `in`); short-circuit was sacrificed because attribute-side sets are small and the order dependency was a real user-facing inconsistency once set-valued attribute injection landed
  - Action attribute injection (`nxe_cedar_eval_ctx_add_action_attr_*`) was already implemented and is now confirmed reachable through the test wrapper, closing the documentation gap from #010

## [9d6ae41](../../commit/9d6ae41) - 2026-04-23

### Refactor

- Tighten `nxe_cedar_make_ip` length guard to the Cedar IP literal spec maximum
  - Fast-path reject changes from `> 45` to `> 43`, matching the longest valid form `xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx/128` (43 chars)
  - No user-visible behavior change: 44- and 45-char inputs were already rejected downstream by `parse_ipv4` / `parse_ipv6`, both of which clamp at `data + len`
  - Comment rewritten so the role of this check — fast-path only, real OOB protection lives in the parsers — is unambiguous

## [e885b0c](../../commit/e885b0c) - 2026-04-23

### Refactor

- Zero-initialise all `nxe_cedar_value_t` constructors for symmetry
  - `nxe_cedar_make_{bool,string,long,entity,record}` now prepend `ngx_memzero(&val, sizeof(nxe_cedar_value_t))`, matching the existing behavior of `nxe_cedar_make_{error,ip,ip_range}`
  - No current read path observes the untouched union bytes (every consumer dispatches on `.type` before touching the union), but the asymmetry was a foot-gun for future changes — a value-level `memcmp` or serialization path would have surfaced uninitialised padding from the five remaining constructors
  - Cost is negligible (`nxe_cedar_value_t` is ~40 bytes) and every constructor now returns a value whose inactive union fields are zero

## [3b52edf](../../commit/3b52edf) - 2026-04-22

### Changed

- Lift record literal value type restriction (Phase 4)
  - Record literal fields may now hold any Cedar runtime value — string, long, bool, set, entity, IP, or nested record — bringing behavior in line with the Cedar reference parser
  - Replaces the Phase B MVP restriction that errored on set / entity / IP fields; the FFI oracle now agrees on `{xs: [1,2]}.xs == [1,2]` and similar shapes
  - Record equality and `has` continue to work through the existing order-independent comparison via `nxe_cedar_value_equals`

## [1f0866e](../../commit/1f0866e) - 2026-04-22

### Refactor

- Unify `nxe_cedar_attr_t` to `{name; nxe_cedar_value_t}` (internal only)
  - Entity attributes and record entries now share a single storage format instead of a parallel `NXE_CEDAR_VALUE_*` union + tag
  - `nxe_cedar_value_t` and `NXE_CEDAR_RVAL_*` constants move from `nxe_cedar_expr.h` to `nxe_cedar_types.h` so attr_t can embed value_t without a circular include
  - `nxe_cedar_make_ip()` is exposed via `nxe_cedar_expr.h` so the injection API can parse IP strings eagerly; invalid IP strings now fail at `add_*_attr_ip()` with `NGX_ERROR` instead of surfacing as a silent evaluation error on first access
  - Public API signatures are unchanged; `nginx-auth-cedar` does not reference `attr_t` directly and is unaffected

## [75f7ef8](../../commit/75f7ef8) - 2026-04-22

### Added

- Add record literal `{key: expr, ...}` syntax in policy expressions (Phase 4)
  - Syntax: `{IDENT | STRING : expr (, ...)? [,]}` inside `when` / `unless` conditions
  - Empty record `{}` is allowed; trailing comma after the last entry is accepted to match the Cedar reference parser
  - Keys may be identifiers or quoted strings; duplicate keys are rejected at parse time
  - Entry count capped at `NXE_CEDAR_MAX_RECORD_ENTRIES` (64)
  - Disambiguation from policy-body `when { ... }` is automatic: the outer `{` after `when` / `unless` is consumed by `nxe_cedar_parse_condition` before the expression parser runs, so any `{` reaching `parse_primary` is a record literal
  - New token `NXE_CEDAR_TOKEN_COLON` for single `:`; the existing `::` path still takes precedence when two colons are adjacent
  - New AST node `NXE_CEDAR_NODE_RECORD` and parse-time entry type `nxe_cedar_record_entry_t { key, value }`
  - Phase B MVP scope: scalar (string, long, bool) and nested record values are supported inside record literals; set / entity / IP values produce an evaluation error and are deferred to a later phase (test case marked `c_limit: true` to skip FFI oracle comparison)
  - Record literals compose naturally with existing features: dot access, bracket access, `has`, order-independent equality, and comparison against attribute-injected records

## [a151b88](../../commit/a151b88) - 2026-04-21

### Added

- Add public record attribute API for populating nested records from callers (Phase 4)
  - Opaque handle `nxe_cedar_record_t` exposed from `nxe_cedar_eval.h`
  - Per-target entry points `nxe_cedar_eval_ctx_add_{principal,action,resource,context}_attr_record(ctx, name)` return a record handle on the corresponding entity / context
  - Scalar fields populated via `nxe_cedar_record_add_{str,long,bool,ip}(rec, name, value)`, reusing the existing scalar add helpers internally
  - Nested records created via `nxe_cedar_record_add_record(rec, name)`; returns `NULL` when the resulting depth would exceed `NXE_CEDAR_MAX_RECORD_DEPTH` (= 16, matching `NXE_CEDAR_MAX_MEMBER_CHAIN`) so every writable field stays reachable from policy text
  - Existing 16 scalar attribute entry points are unchanged (no format or ABI change)
  - JSON decoding remains the caller's responsibility; nxe-cedar keeps its "nginx core only" dependency policy

## [f743358](../../commit/f743358) - 2026-04-21

### Added

- Add nested attribute access `expr.a.b` via record-valued attributes (Phase 4)
  - Syntax: any chain of `.ident` or `["key"]` steps on record-typed attributes in `when` / `unless` conditions
  - Record literals in policy text (`{foo: 1}.foo`) are out of scope; records must be injected through the eval-context API (added in a follow-up commit)
  - New runtime value `NXE_CEDAR_RVAL_RECORD` and attribute variant `NXE_CEDAR_VALUE_RECORD` holding an `ngx_array_t *` of `nxe_cedar_attr_t`
  - `NXE_CEDAR_MAX_RECORD_DEPTH` = 16, aligned with `NXE_CEDAR_MAX_MEMBER_CHAIN`
  - Generalized `nxe_cedar_eval_attr_access()` and `nxe_cedar_eval_has()` so the VAR fast path is unchanged and non-VAR objects are evaluated recursively; records are descended into, other types return error
  - Order-independent record equality added to `nxe_cedar_value_equals()`

## [a029a0d](../../commit/a029a0d) - 2026-04-20

### Fixed

- Reject empty-string key `[""]` in bracket access at parse time (Phase 4)
  - `expr[""]` now returns a parse error; previously it was accepted and surfaced only as an attribute-lookup failure at evaluation
  - Failure is reported at the parse site rather than as a missing-attribute error at runtime
  - nxe-cedar specific restriction: Cedar's reference implementation accepts empty-string keys at parse time (matching test case marked `c_limit: true` to skip FFI oracle comparison)

## [8aed9e1](../../commit/8aed9e1) - 2026-04-20

### Fixed

- Clarify `\*` escape error message in string-literal (STR) contexts
  - Unified wording at the five STR-context sites (entity id, string literal, `ip()` argument, bracket / `has` via shared helper, annotation value): `invalid escape sequence \*: only valid in like patterns`
  - Prior phrasing "\\* escape is only valid in like patterns" implied a context-dependent rule; `\*` is simply undefined as an escape inside a regular string literal
  - The pattern-context (PAT) message is unchanged

## [9e85471](../../commit/9e85471) - 2026-04-20

### Added

- Add `isEmpty` method for set emptiness check (Phase 4)
  - Syntax: `expr.isEmpty()` in `when` / `unless` conditions
  - Receiver must be set-typed; non-set receiver (string, integer, boolean, entity, IP) returns evaluation error
  - Returns `true` when the set has zero elements, `false` otherwise
  - Dispatch reorganized in `nxe_cedar_eval_method_call()` zero-arg branch so `isEmpty` checks Set-receiver before the existing IP inspection methods check IP-receiver

## [e402a6f](../../commit/e402a6f) - 2026-04-20

### Added

- Add bracket access `expr["key"]` for attribute references (Phase 4)
  - Syntax: `expr["key"]` in `when` / `unless` conditions; semantically equivalent to `expr.key`
  - Supports attribute names that are not valid identifiers (e.g. `context["X-Request-Id"]`)
  - Supports attribute names that collide with Cedar keywords (e.g. `context["ip"]`, `principal["is"]`)
  - Grammar: `member = primary { "." IDENT [ "(" [ expr_list ] ")" ] | "[" STRING "]" }`
  - Only string literals are accepted inside brackets; non-string tokens and `\*`-escaped strings are parse errors
  - Bracket and dot access can be mixed on the same chain (`principal.role && principal["email"]`)
  - Both forms produce `NXE_CEDAR_NODE_ATTR_ACCESS`; evaluator, `has` operator, and runtime semantics are unchanged
  - Bracket steps participate in the existing `NXE_CEDAR_MAX_MEMBER_CHAIN` depth limit

## [bb73d72](../../commit/bb73d72) - 2026-04-17

### Changed

- Pin Cedar Long values to `int64_t` for i64 integrity across platforms
  - `long_val` fields (AST node, runtime value, attribute) use `int64_t`
  - Overflow-checked helpers, unary-minus boundary, and literal parsing use `INT64_MAX` / `INT64_MIN`
  - Public API `nxe_cedar_eval_ctx_add_{principal,action,resource,context}_attr_long()` now takes `int64_t` (was `ngx_int_t`); on 32-bit builds this is a breaking signature change
  - Motivation: `ngx_int_t` is `intptr_t`, which collapses to 32 bits on 32-bit platforms and would diverge from the Cedar i64 reference semantics

## [a1fa4f9](../../commit/a1fa4f9) - 2026-04-17

### Added

- Add arithmetic operators `+`, `-`, `*` on Long values (Phase 4)
  - Syntax: `expr + expr`, `expr - expr`, `expr * expr` in `when` / `unless` conditions
  - Both operands must be Long; other types (string, entity, etc.) return evaluation error
  - Operator precedence: `*` > `+`, `-` > comparison operators (left-associative within each level)
  - Overflow / underflow yields an evaluation error and the policy is not applicable
  - `NXE_CEDAR_TOKEN_PLUS` and `NXE_CEDAR_TOKEN_STAR` tokens added; `NXE_CEDAR_TOKEN_NEGATE` renamed to `NXE_CEDAR_TOKEN_MINUS` (shared by unary and binary minus per Cedar spec)
  - `NXE_CEDAR_OP_PLUS`, `NXE_CEDAR_OP_MINUS`, `NXE_CEDAR_OP_MUL` operators added to `nxe_cedar_op_t`
  - Grammar: `relation -> add -> mult -> unary` with `add = mult { (+|-) mult }` and `mult = unary { * unary }`
  - `is-in` RHS and relational operator RHS now parse `add` expressions so arithmetic composes with entity hierarchy and comparison
  - Unified overflow-checked helper `nxe_cedar_long_arith()` (dispatching on `nxe_cedar_op_t`) wraps `__builtin_{add,sub,mul}_overflow` and handles the `INT64_MIN` boundary (e.g. `MIN * -1` rejected, `MIN` as a multiplication result accepted)

## [51d92d4](../../commit/51d92d4) - 2026-04-17

### Added

- Add `is` operator for entity type checks (Phase 4)
  - Scope syntax: `principal is Type`, `principal is Type in entity_ref` (same for `resource`)
  - Expression syntax: `expr is Type`, `expr is Type in expr` in `when` / `unless` conditions
  - `is` in action scope is rejected as parse error (principal / resource only)
  - Type name supports namespaces: `principal is NS::User`
  - Expression `is` requires LHS to evaluate to an entity; non-entity LHS returns evaluation error
  - `is-in` variant additionally performs entity-hierarchy membership (`in` degrades to `==` per existing `in` semantics)
  - `NXE_CEDAR_TOKEN_IS` keyword added to lexer (reserved: not usable as attribute name)
  - `NXE_CEDAR_NODE_IS` AST node holds `{object, entity_type, in_entity}`
  - `NXE_CEDAR_SCOPE_IS` / `NXE_CEDAR_SCOPE_IS_IN` scope constraint kinds added; `entity_type` field added to `nxe_cedar_scope_t`
  - `nxe_cedar_parse_type_name()` helper introduced for `IDENT { "::" IDENT }` paths without a trailing quoted id

## [365c197](../../commit/365c197) - 2026-04-17

### Added

- Add IP inspection methods `isIpv4`, `isIpv6`, `isLoopback`, `isMulticast` (Phase 4)
  - Syntax: `expr.isIpv4()`, `expr.isIpv6()`, `expr.isLoopback()`, `expr.isMulticast()` in `when` / `unless` conditions
  - Receiver must be IP-typed; non-IP receiver returns evaluation error
  - `isIpv4` / `isIpv6`: true when receiver is IPv4 / IPv6 (host or CIDR)
  - `isLoopback`: receiver CIDR must be entirely within `127.0.0.0/8` (IPv4) or `::1/128` (IPv6)
  - `isMulticast`: receiver CIDR must be entirely within `224.0.0.0/4` (IPv4) or `ff00::/8` (IPv6)
  - Zero-argument method call syntax `method()` supported in parser (`arg = NULL` in AST)
  - `nxe_cedar_ip_cidr_contains()` helper shared with `isInRange` for consistent CIDR membership semantics

## [89673d8](../../commit/89673d8) - 2026-04-16

### Added

- Add annotation parsing for policy metadata (Phase 4)
  - Syntax: `@key` or `@key("value")` before `permit` / `forbid`
  - Multiple annotations per policy supported (max 16)
  - Duplicate annotation keys within a single policy rejected as parse error
  - Annotations do not affect evaluation semantics (forbid-priority preserved)
  - `nxe_cedar_annotation_t` struct: key/value pair stored in `nxe_cedar_policy_t.annotations` (`ngx_array_t`)
  - Lazy-create pattern: annotations array allocated only when `@` is encountered
  - `\*` escape in annotation values rejected (only valid in `like` patterns)

## [e739b8b](../../commit/e739b8b) - 2026-04-16

### Added

- Add `isInRange` method for IP address range membership (Phase 3)
  - Syntax: `expr.isInRange(expr)` in `when` / `unless` conditions
  - Both receiver and argument must be IP-typed; type mismatch returns evaluation error
  - IPv4/IPv6 family mismatch returns `false`
  - CIDR prefix matching: full byte comparison + remaining bits mask
  - Receiver CIDR must be at least as specific as argument range (`/24` in `/8` → true, `/8` in `/24` → false)
  - Single address (implicit `/32` or `/128`) treated as most-specific prefix
  - Context attribute support: `context.ip.isInRange(ip("10.0.0.0/8"))`

## [76ddcb5](../../commit/76ddcb5) - 2026-04-16

### Added

- Add `ip()` extension function for IP address literals (Phase 3)
  - Syntax: `ip("addr")` in `when` / `unless` conditions
  - IPv4 (`1.2.3.4`), IPv6 (`::1`, `2001:db8::1`), CIDR (`10.0.0.0/8`, `fe80::/10`)
  - Binary representation: 4-byte (IPv4) or 16-byte (IPv6) network byte order with prefix length
  - Equality / inequality operators (`==`, `!=`) between same-family IP values
  - Context attribute support via `__extn` JSON format and `nxe_cedar_eval_ctx_add_*_attr_ip()` API
  - `ip` keyword usable as attribute name in member access (`context.ip`) and `has` operator (`context has ip`)
  - Strict parsing: reject leading zeros in octets/groups/prefix, max 4 hex digits per IPv6 group
  - IPv4-mapped IPv6 dot notation (`::ffff:192.168.1.1`) rejected per Cedar spec; hex form (`::ffff:c0a8:0101`) accepted
  - `nxe_cedar_parse_cidr_prefix()` shared helper for IPv4/IPv6 prefix parsing
  - `nxe_cedar_token_is_ident()` helper for extensible keyword-as-attribute-name handling

## [3c9d106](../../commit/3c9d106) - 2026-04-15

### Added

- Add `contains` single element set membership method (Phase 3)
  - Syntax: `expr.contains(expr)` in `when` / `unless` conditions
  - Receiver must be set-typed; non-set receiver returns evaluation error
  - Argument can be any value type (string, integer, boolean, entity)
  - Type mismatch between set elements and argument returns `false` (not error)
  - Reuses existing `nxe_cedar_value_equals()` for element comparison

## [b8c4385](../../commit/b8c4385) - 2026-04-15

### Added

- Add `if-then-else` conditional expression (Phase 2)
  - Syntax: `if expr then expr else expr` in `when` / `unless` conditions
  - Condition must evaluate to boolean; non-boolean condition returns evaluation error
  - Short-circuit evaluation: only the selected branch is evaluated (Cedar spec compliant)
  - Supports `has` guard pattern: `if principal has attr then principal.attr else default`
  - Supports arbitrary result types (boolean, integer, string, entity) in then/else branches
  - Nestable: `if c1 then (if c2 then a else b) else c`

## [58d3e58](../../commit/58d3e58) - 2026-04-15

### Added

- Add `containsAll` / `containsAny` set methods (Phase 2)
  - Syntax: `expr.containsAll(expr)`, `expr.containsAny(expr)` in `when` / `unless` conditions
  - Both operands must be set-typed; type mismatch returns evaluation error
  - General method call parsing in `nxe_cedar_parse_member_expr()`: `expr.ident(expr)` pattern
  - Method dispatch by name in `nxe_cedar_eval_method_call()` (extensible for Phase 3 `contains`, `isInRange`)

## [83dcfdc](../../commit/83dcfdc) - 2026-04-15

### Added

- Add `like` operator for wildcard pattern matching on strings (Phase 2)
  - Syntax: `expr like "pattern"` in `when` / `unless` conditions
  - `*` matches zero or more arbitrary characters; `\*` matches a literal `*`
  - Escape sequences `\x2A` and `\u{2A}` are treated as wildcards (Cedar spec)
  - Pattern compiled at parse time: unescaped `*` → 0xFF sentinel, consecutive wildcards compressed
  - `\*` escape is accepted only in `like` patterns; rejected in regular strings and entity IDs at parse time
  - Reject raw 0xFF bytes in pattern source to prevent wildcard sentinel collision
  - O(n+m) greedy/backtracking matcher in `nxe_cedar_like_match()`
  - Refactor string escape decoding into shared `nxe_cedar_decode_escape()` (lexer + pattern compiler)
  - Add `raw` and `has_star_escape` fields to `nxe_cedar_token_t` for pattern compilation

## [cd48da3](../../commit/cd48da3) - 2026-04-14

### Added

- Add `has` operator for attribute existence checks (Phase 2)
  - Syntax: `expr has ident` / `expr has "string"` in `when` / `unless` conditions
  - Supported on all variable types: `principal`, `action`, `resource`, `context`
  - Returns boolean; `false` when attribute array is absent (safe guard for `&&` chaining)
  - Refactor variable-to-attribute resolution into `nxe_cedar_resolve_var_attrs()`

## [4fa236b](../../commit/4fa236b) - 2026-04-14

### Added

- Add typed attribute builders (`_long`, `_bool`) for all entity types (principal, action, resource, context)

## [61ff651](../../commit/61ff651) - 2026-04-14

### Added

- Add missing Cedar string escape sequences: `\r` (carriage return), `\xHH` (2-digit ASCII hex), `\u{...}` (1-6 digit Unicode)

## [4fdc11d](../../commit/4fdc11d) - 2026-04-14

### Fixed

- Allow `principal in entity_ref` and `resource in entity_ref` scope constraints per Cedar spec (previously only `action in` was accepted)

## [8ef7e85](../../commit/8ef7e85) - 2026-04-14

### Added

- Validate that scope set literals (`action in [...]`) contain only entity references; reject non-entity elements (integers, variables, etc.) at parse time

## [ce3e49e](../../commit/ce3e49e) - 2026-04-14

### Added

- Add unary minus operator (`-`) for negative integer literals (e.g., `context.val == -1`)
- Restructure parser grammar to match Cedar spec operator precedence: `And → Relation → Unary → Member → Primary`

## [b36a754](../../commit/b36a754) - 2026-04-14

### Fixed

- Restrict scope set literals to action only per Cedar spec

## [650d77b](../../commit/650d77b) - 2026-04-14

### Added

- Add expression evaluator and policy evaluation engine
  - Forbid-priority evaluation model (forbid → deny, permit → allow, default deny)
  - Runtime value types: string, long, boolean, entity reference, set
  - Operators: `==`, `!=`, `&&`, `||`, `!`, `in` (entity hierarchy)
  - Attribute access on `principal`, `action`, `resource`, `context` variables
  - Scope matching: `NONE` (unconstrained), `==` (equality), `in` (hierarchy/set target)
  - Condition evaluation: `when` / `unless` with AND semantics
  - Public API: `nxe_cedar_eval()`, `nxe_cedar_eval_ctx_create()`, `nxe_cedar_eval_ctx_set_principal()`, `nxe_cedar_eval_ctx_set_action()`, `nxe_cedar_eval_ctx_set_resource()`, `nxe_cedar_eval_ctx_add_*_attr()` functions

## [b1ef81d](../../commit/b1ef81d) - 2026-04-14

### Added

- Add Cedar lexer and recursive descent parser
  - Policy effects: `permit`, `forbid`
  - Scope clauses: `principal`, `action`, `resource` with `==` and `in` operators
  - Condition clauses: `when { expr }`, `unless { expr }` (multiple per policy)
  - Literals: boolean (`true`/`false`), string (with escape sequences), integer, entity reference (`Type::"id"`)
  - Set literals: `[expr, ...]`
  - Variables: `principal`, `action`, `resource`, `context`
  - Member access: `expr.ident` (chained)
  - Operators: `==`, `!=`, `&&`, `||`, `!`, `in`
  - Line comments: `//`
  - Entity type paths: `A::B::C::"id"` (nested `::` separator)
  - Public API: `nxe_cedar_parse()`

## [f884682](../../commit/f884682) - 2026-04-14

### Changed

- Move source files under `src/` directory

## [0daed6d](../../commit/0daed6d) - 2026-04-08

### Added

- Add `nxe_cedar_types.h` with all data structure definitions
  - Token types for Cedar keywords, operators, and delimiters
  - AST node types: literals, variables, binary/unary operators, member access, set literals
  - Policy structure: effect, scope constraints, conditions
  - Evaluation context: entity references, attribute key-value store (string, long, boolean)
  - Phase 2/3 token and node type placeholders (`has`, `like`, `if-then-else`, `ip`)

# Changelog

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

# Changelog

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

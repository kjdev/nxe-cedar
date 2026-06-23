# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [0.3.0] - 2026-06-01

### Added

- Cedar `datetime` / `duration` extension types
  - `datetime("...")` parses ISO 8601 (`YYYY-MM-DD` or `YYYY-MM-DDThh:mm:ss(.SSS)?(Z|±hhmm)`); `duration("...")` parses `[-]?` plus `d`/`h`/`m`/`s`/`ms` unit groups in descending order
  - `datetime` methods `offset` / `durationSince` / `toDate` / `toTime` and `duration` conversions `toMilliseconds` / `toSeconds` / `toMinutes` / `toHours` / `toDays`
  - The `<` `<=` `>` `>=` operators accept two `datetime` or two `duration` operands; `==` / `!=` compare canonical form
  - Injection API: `add_*_attr_{datetime,duration}`, `record_add_{datetime,duration}`, `set_add_{datetime,duration}`, each validating the constructor string eagerly
- Resolve attribute / `has` / `in` access on request entity literals
  - An entity literal naming the principal, action, or resource now resolves attribute access, `has`, and `in` through that request entity, matching reference Cedar. A literal naming no request entity still errors on attribute / `has` access and matches `in` reflexively only

### Fixed

- Materialize a bare `context` as a record value
  - `context` is now usable as a whole record — as an operand of `==`, `!=`, `has`, and as a function argument — instead of only through `context.attr` member access. An unset context materializes as the empty record
- Treat `==` / `!=` over mismatched operand types as `false` / `true`
  - `==` and `!=` are total: comparing two values of different types is no longer an evaluation error but simply "not equal", so `==` yields `false` and `!=` yields `true`. Surfaced when the result was consumed by another operator

## [0.2.0] - 2026-05-26

### Added

- `nxe_cedar_eval_detail()` diagnostic API
  - Evaluates a policy set and writes the matching policies into a caller-supplied detail struct: every matching `forbid` on DENY, every matching `permit` on ALLOW, an empty list on default-deny
  - `nxe_cedar_eval()` becomes a thin wrapper that keeps the previous early-exit fast path
  - Companion `nxe_cedar_policy_get_annotation(policy, key)` lifts `@id` / `@advice` off a returned policy for audit logging

## [0.1.0] - 2026-05-25

Initial release covering the core Cedar policy subset.

### Added

#### Parsing & evaluation

- Cedar lexer and recursive descent parser
  - Policy effects `permit` / `forbid`; scope clauses on `principal` / `action` / `resource` with `==` and `in`; `when` / `unless` condition clauses
  - Literals (boolean, string with escapes, integer, entity reference), set literals, chained member access, entity type paths (`A::B::C::"id"`), line comments
- Expression evaluator and policy evaluation engine
  - Forbid-priority model (forbid → deny, permit → allow, default deny)
  - Operators `==` `!=` `&&` `||` `!` `in`; attribute access on `principal` / `action` / `resource` / `context`; scope matching for unconstrained / equality / hierarchy targets
- Recursion-depth cap in the expression evaluator

#### Operators & expressions

- Unary minus and Cedar-compliant operator precedence
- `like` wildcard pattern matching (`*` wildcard, `\*` literal)
- `if-then-else` conditional expression with short-circuit evaluation
- `has` attribute-existence check
- Arithmetic operators `+` `-` `*` on Long values with overflow detection
- `is` entity type check (`expr is Type`, `expr is Type in expr`)
- Bracket access `expr["key"]` for keys that are not valid identifiers or that collide with keywords
- Nested attribute access `expr.a.b` over record-valued attributes
- Record literal `{key: expr, ...}` syntax

#### Set methods

- `containsAll` / `containsAny`
- `contains` (single-element membership)
- `isEmpty`

#### IP extension

- `ip("addr")` IP address literals (IPv4 / IPv6 / CIDR) with `==` / `!=`
- `isInRange` CIDR range membership
- IP inspection methods `isIpv4` / `isIpv6` / `isLoopback` / `isMulticast`

#### decimal extension

- `decimal("d.d")` extension type with ordering methods `lessThan` / `lessThanOrEqual` / `greaterThan` / `greaterThanOrEqual`

#### Entity hierarchy

- Entity hierarchy for the `in` operator, with caller-injected ancestor closures (`add_{principal,action,resource}_parent`)
- Disambiguate the `in` operator by the entity's origin slot when principal / action / resource share an identity

#### Attributes & injection API

- Typed attribute builders `_long` / `_bool` for all entity types
- Record attribute API for populating nested records from callers
- Set- and entity-valued attribute injection
- String escape sequences `\r`, `\xHH`, `\u{...}`
- Policy annotation parsing (`@key`, `@key("value")`)

#### Scope constraints

- `principal in entity_ref` and `resource in entity_ref` scope constraints
- Validate that scope set literals contain only entity references

### Changed

- Pin Cedar Long values to `int64_t` for i64 integrity across platforms
  - The `add_*_attr_long()` API now takes `int64_t` (was `ngx_int_t`); on 32-bit builds this is a breaking signature change
- Lift the record-literal value-type restriction so fields may hold any Cedar value (string, long, bool, set, entity, IP, or nested record)

### Fixed

- Restrict scope set literals to `action` only, per the Cedar specification
- Reject duplicate keys in the attribute / record injection API
- Harden record / set equality with bijective matching
- Clear the entity slot tag at composite-expression boundaries so derived entities fall back to reflexive `in` comparison
- Reject the empty-string key `[""]` in bracket access at parse time
- Clarify the `\*` escape error message in string-literal contexts

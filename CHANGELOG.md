# Changelog

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

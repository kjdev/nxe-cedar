//! cedar_ffi - FFI bridge to the official Rust Cedar implementation
//!
//! Serves as a test oracle for nxe-cedar, providing Cedar policy evaluation results.
//! Called from C test runner via `extern "C"` functions.

use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::str::FromStr;

use cedar_policy::{
    Authorizer, Context, Decision, Entities, Entity, EntityId, EntityTypeName,
    EntityUid, PolicySet, Request, RestrictedExpression,
};
use serde::Deserialize;

thread_local! {
    static LAST_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

fn set_error(msg: &str) {
    LAST_ERROR.with(|e| {
        *e.borrow_mut() = CString::new(msg).ok();
    });
}

fn clear_error() {
    LAST_ERROR.with(|e| {
        *e.borrow_mut() = None;
    });
}

/// Entity reference from JSON request
#[derive(Debug, Deserialize)]
struct EntityRef {
    #[serde(rename = "type")]
    entity_type: String,
    id: String,
}

/// Full JSON request
#[derive(Debug, Deserialize)]
struct FfiRequest {
    principal: EntityRef,
    action: EntityRef,
    resource: EntityRef,
    #[serde(default)]
    context: serde_json::Value,
    #[serde(default)]
    principal_attrs: HashMap<String, serde_json::Value>,
    #[serde(default)]
    action_attrs: HashMap<String, serde_json::Value>,
    #[serde(default)]
    resource_attrs: HashMap<String, serde_json::Value>,
    #[serde(default)]
    principal_parents: Vec<EntityRef>,
    #[serde(default)]
    action_parents: Vec<EntityRef>,
    #[serde(default)]
    resource_parents: Vec<EntityRef>,
}

/// Nesting limit mirrored from the C implementation
/// (`NXE_CEDAR_MAX_RECORD_DEPTH`). Kept in sync so oracle comparisons
/// see the same depth ceiling for record attributes.
const MAX_RECORD_DEPTH: usize = 16;

/// Convert serde_json::Value to Cedar RestrictedExpression.
///
/// Plain (non-`__extn` / non-`__entity`) objects are interpreted as
/// records and recursed into. JSON arrays become sets and recurse with
/// the same `depth` ceiling (matching the C side, where
/// `NXE_CEDAR_MAX_SET_DEPTH == NXE_CEDAR_MAX_RECORD_DEPTH`). `depth` is
/// 0 for the top-level attribute value and increments by one for each
/// nested record / set level; once it reaches `MAX_RECORD_DEPTH`
/// further nesting is rejected to match the C API.
fn json_value_to_restricted_expr(
    value: &serde_json::Value,
    depth: usize,
) -> Result<RestrictedExpression, String> {
    match value {
        serde_json::Value::String(s) => Ok(RestrictedExpression::new_string(s.clone())),
        serde_json::Value::Number(n) => {
            if let Some(i) = n.as_i64() {
                Ok(RestrictedExpression::new_long(i))
            } else {
                Err(format!("unsupported number: {n}"))
            }
        }
        serde_json::Value::Bool(b) => Ok(RestrictedExpression::new_bool(*b)),
        serde_json::Value::Array(items) => {
            if depth >= MAX_RECORD_DEPTH {
                return Err(format!(
                    "set nesting exceeds oracle limit {MAX_RECORD_DEPTH}"
                ));
            }
            let mut elts = Vec::with_capacity(items.len());
            for item in items {
                elts.push(json_value_to_restricted_expr(item, depth + 1)?);
            }
            Ok(RestrictedExpression::new_set(elts))
        }
        serde_json::Value::Object(obj) => {
            if let Some(extn) = obj.get("__extn") {
                let fn_name = extn
                    .get("fn")
                    .and_then(|v| v.as_str())
                    .ok_or_else(|| "missing __extn.fn".to_string())?;
                let arg = extn
                    .get("arg")
                    .and_then(|v| v.as_str())
                    .ok_or_else(|| "missing __extn.arg".to_string())?;
                let expr_str = format!("{fn_name}(\"{arg}\")");
                return RestrictedExpression::from_str(&expr_str)
                    .map_err(|e| format!("extension parse error: {e}"));
            }
            if let Some(entity) = obj.get("__entity") {
                let entity_obj = entity
                    .as_object()
                    .ok_or_else(|| "__entity must be an object".to_string())?;
                let entity_type = entity_obj
                    .get("type")
                    .and_then(|v| v.as_str())
                    .ok_or_else(|| "missing __entity.type".to_string())?;
                let entity_id = entity_obj
                    .get("id")
                    .and_then(|v| v.as_str())
                    .ok_or_else(|| "missing __entity.id".to_string())?;
                let type_name =
                    EntityTypeName::from_str(entity_type).map_err(|e| e.to_string())?;
                let id = EntityId::new(entity_id);
                let uid = EntityUid::from_type_name_and_id(type_name, id);
                return Ok(RestrictedExpression::new_entity_uid(uid));
            }
            if depth >= MAX_RECORD_DEPTH {
                return Err(format!(
                    "record nesting exceeds oracle limit {MAX_RECORD_DEPTH}"
                ));
            }
            let mut fields = Vec::with_capacity(obj.len());
            for (k, v) in obj {
                let child = json_value_to_restricted_expr(v, depth + 1)?;
                fields.push((k.clone(), child));
            }
            RestrictedExpression::new_record(fields)
                .map_err(|e| format!("record construction error: {e}"))
        }
        _ => Err(format!("unsupported JSON value type: {value}")),
    }
}

/// Check that a JSON value tree does not nest deeper than
/// `MAX_RECORD_DEPTH`. `__extn` and `__entity` objects are treated as
/// opaque scalars (matching the attribute-build path, which converts
/// them to extension calls / entity-UID literals at the current depth
/// without recursing). Arrays count toward the same ceiling because
/// they become Cedar sets, and the C side aliases
/// `NXE_CEDAR_MAX_SET_DEPTH` to `NXE_CEDAR_MAX_RECORD_DEPTH`.
fn validate_json_record_depth(
    value: &serde_json::Value,
    depth: usize,
) -> Result<(), String> {
    match value {
        serde_json::Value::Object(obj) => {
            if obj.get("__extn").is_some() || obj.get("__entity").is_some() {
                return Ok(());
            }
            if depth >= MAX_RECORD_DEPTH {
                return Err(format!(
                    "record nesting exceeds oracle limit {MAX_RECORD_DEPTH}"
                ));
            }
            for v in obj.values() {
                validate_json_record_depth(v, depth + 1)?;
            }
            Ok(())
        }
        serde_json::Value::Array(items) => {
            if depth >= MAX_RECORD_DEPTH {
                return Err(format!(
                    "set nesting exceeds oracle limit {MAX_RECORD_DEPTH}"
                ));
            }
            for v in items {
                validate_json_record_depth(v, depth + 1)?;
            }
            Ok(())
        }
        _ => Ok(()),
    }
}

/// Validate each top-level context field as its own depth-0 record.
fn validate_context_record_depth(context: &serde_json::Value) -> Result<(), String> {
    if let Some(obj) = context.as_object() {
        for value in obj.values() {
            validate_json_record_depth(value, 0)?;
        }
    }
    Ok(())
}

/// Create EntityUid from entity reference
fn make_entity_uid(entity_ref: &EntityRef) -> Result<EntityUid, String> {
    let type_name =
        EntityTypeName::from_str(&entity_ref.entity_type).map_err(|e| e.to_string())?;
    let id = EntityId::new(&entity_ref.id);
    Ok(EntityUid::from_type_name_and_id(type_name, id))
}

/// Build Entity from attribute map and (transitive) parent list
fn build_entity(
    entity_ref: &EntityRef,
    attrs: &HashMap<String, serde_json::Value>,
    parents: &[EntityRef],
) -> Result<Entity, String> {
    let uid = make_entity_uid(entity_ref)?;

    let mut attr_map = HashMap::new();
    for (key, value) in attrs {
        let expr = json_value_to_restricted_expr(value, 0)?;
        attr_map.insert(key.clone(), expr);
    }

    let mut parent_set = HashSet::new();
    for parent in parents {
        parent_set.insert(make_entity_uid(parent)?);
    }

    Entity::new(uid, attr_map, parent_set).map_err(|e| e.to_string())
}


/// Build a parent-only Entity stub so Cedar's hierarchy resolution finds
/// the ancestor in the entity store. C-side parents are flat transitive
/// closures; mirror that by adding each parent as its own entity with no
/// attributes and no further parents.
fn build_parent_stub(entity_ref: &EntityRef) -> Result<Entity, String> {
    let uid = make_entity_uid(entity_ref)?;
    Entity::new(uid, HashMap::new(), HashSet::new()).map_err(|e| e.to_string())
}

/// Main authorization logic
fn authorize_inner(
    policy_text: &str,
    request: &FfiRequest,
) -> Result<Decision, String> {
    // parse policy set
    let policy_set: PolicySet = policy_text.parse().map_err(|e| format!("{e}"))?;

    // build entities
    let mut entities_vec = Vec::new();

    // principal entity (with attributes and parents)
    let principal_entity = build_entity(
        &request.principal,
        &request.principal_attrs,
        &request.principal_parents,
    )?;
    entities_vec.push(principal_entity);

    // action entity (with attributes and parents)
    let action_entity = build_entity(
        &request.action,
        &request.action_attrs,
        &request.action_parents,
    )?;
    entities_vec.push(action_entity);

    // resource entity (with attributes and parents)
    let resource_entity = build_entity(
        &request.resource,
        &request.resource_attrs,
        &request.resource_parents,
    )?;
    entities_vec.push(resource_entity);

    // parent stubs: Cedar requires referenced ancestors to exist in the
    // entity store. The C side carries them implicitly via the flat
    // parent list; mirror that here.
    for parent in &request.principal_parents {
        entities_vec.push(build_parent_stub(parent)?);
    }
    for parent in &request.action_parents {
        entities_vec.push(build_parent_stub(parent)?);
    }
    for parent in &request.resource_parents {
        entities_vec.push(build_parent_stub(parent)?);
    }

    let entities =
        Entities::from_entities(entities_vec, None).map_err(|e| e.to_string())?;

    // build context
    let context = if request.context.is_null() || request.context.is_object() && request.context.as_object().map_or(true, |m| m.is_empty()) {
        Context::empty()
    } else {
        validate_context_record_depth(&request.context)?;
        Context::from_json_value(request.context.clone(), None)
            .map_err(|e| e.to_string())?
    };

    // build request
    let principal_uid = make_entity_uid(&request.principal)?;
    let action_uid = make_entity_uid(&request.action)?;
    let resource_uid = make_entity_uid(&request.resource)?;

    let request = Request::new(
        principal_uid,
        action_uid,
        resource_uid,
        context,
        None,
    )
    .map_err(|e| e.to_string())?;

    // authorize
    let authorizer = Authorizer::new();
    let response = authorizer.is_authorized(&request, &policy_set, &entities);

    Ok(response.decision())
}

#[unsafe(no_mangle)]
pub extern "C" fn cedar_ffi_authorize(
    policy_text: *const c_char,
    request_json: *const c_char,
) -> i32 {
    clear_error();

    if policy_text.is_null() || request_json.is_null() {
        set_error("null pointer argument");
        return -1;
    }

    let policy_str = match unsafe { CStr::from_ptr(policy_text) }.to_str() {
        Ok(s) => s,
        Err(e) => {
            set_error(&format!("invalid policy_text UTF-8: {e}"));
            return -1;
        }
    };

    let request_str = match unsafe { CStr::from_ptr(request_json) }.to_str() {
        Ok(s) => s,
        Err(e) => {
            set_error(&format!("invalid request_json UTF-8: {e}"));
            return -1;
        }
    };

    let request: FfiRequest = match serde_json::from_str(request_str) {
        Ok(r) => r,
        Err(e) => {
            set_error(&format!("JSON parse error: {e}"));
            return -1;
        }
    };

    match authorize_inner(policy_str, &request) {
        Ok(Decision::Allow) => 1,
        Ok(Decision::Deny) => 0,
        Err(e) => {
            set_error(&e);
            -1
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn cedar_ffi_parse_check(
    policy_text: *const c_char,
) -> i32 {
    clear_error();

    if policy_text.is_null() {
        set_error("null pointer argument");
        return -1;
    }

    let policy_str = match unsafe { CStr::from_ptr(policy_text) }.to_str() {
        Ok(s) => s,
        Err(e) => {
            set_error(&format!("invalid UTF-8: {e}"));
            return -1;
        }
    };

    match policy_str.parse::<PolicySet>() {
        Ok(_) => 0,
        Err(e) => {
            set_error(&format!("parse error: {e}"));
            -1
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn cedar_ffi_last_error() -> *const c_char {
    LAST_ERROR.with(|e| match &*e.borrow() {
        Some(cstr) => cstr.as_ptr(),
        None => std::ptr::null(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    #[test]
    fn test_basic_permit() {
        let policy = CString::new("permit (principal, action, resource);").unwrap();
        let request = CString::new(
            r#"{
                "principal": {"type": "User", "id": "alice"},
                "action": {"type": "Action", "id": "GET"},
                "resource": {"type": "Endpoint", "id": "/api/data"}
            }"#,
        )
        .unwrap();

        let result = cedar_ffi_authorize(policy.as_ptr(), request.as_ptr());
        assert_eq!(result, 1, "unconditional permit should allow");
    }

    #[test]
    fn test_basic_forbid() {
        let policy = CString::new("forbid (principal, action, resource);").unwrap();
        let request = CString::new(
            r#"{
                "principal": {"type": "User", "id": "alice"},
                "action": {"type": "Action", "id": "GET"},
                "resource": {"type": "Endpoint", "id": "/api/data"}
            }"#,
        )
        .unwrap();

        let result = cedar_ffi_authorize(policy.as_ptr(), request.as_ptr());
        assert_eq!(result, 0, "unconditional forbid should deny");
    }

    #[test]
    fn test_empty_policy() {
        let policy = CString::new("").unwrap();
        let request = CString::new(
            r#"{
                "principal": {"type": "User", "id": "alice"},
                "action": {"type": "Action", "id": "GET"},
                "resource": {"type": "Endpoint", "id": "/api/data"}
            }"#,
        )
        .unwrap();

        let result = cedar_ffi_authorize(policy.as_ptr(), request.as_ptr());
        assert_eq!(result, 0, "empty policy should deny (default deny)");
    }

    #[test]
    fn test_parse_check_valid() {
        let policy = CString::new("permit (principal, action, resource);").unwrap();
        let result = cedar_ffi_parse_check(policy.as_ptr());
        assert_eq!(result, 0);
    }

    #[test]
    fn test_parse_check_invalid() {
        let policy = CString::new("invalid policy text").unwrap();
        let result = cedar_ffi_parse_check(policy.as_ptr());
        assert_eq!(result, -1);
        assert!(!cedar_ffi_last_error().is_null());
    }
}

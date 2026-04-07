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
}

/// Convert serde_json::Value to Cedar RestrictedExpression
fn json_value_to_restricted_expr(
    value: &serde_json::Value,
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
        _ => Err(format!("unsupported JSON value type: {value}")),
    }
}

/// Create EntityUid from entity reference
fn make_entity_uid(entity_ref: &EntityRef) -> Result<EntityUid, String> {
    let type_name =
        EntityTypeName::from_str(&entity_ref.entity_type).map_err(|e| e.to_string())?;
    let id = EntityId::new(&entity_ref.id);
    Ok(EntityUid::from_type_name_and_id(type_name, id))
}

/// Build Entity from attribute map
fn build_entity(
    entity_ref: &EntityRef,
    attrs: &HashMap<String, serde_json::Value>,
) -> Result<Entity, String> {
    let uid = make_entity_uid(entity_ref)?;

    let mut attr_map = HashMap::new();
    for (key, value) in attrs {
        let expr = json_value_to_restricted_expr(value)?;
        attr_map.insert(key.clone(), expr);
    }

    Entity::new(uid, attr_map, HashSet::new()).map_err(|e| e.to_string())
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

    // principal entity (with attributes)
    let principal_entity = build_entity(&request.principal, &request.principal_attrs)?;
    entities_vec.push(principal_entity);

    // action entity (with attributes)
    let action_entity = build_entity(&request.action, &request.action_attrs)?;
    entities_vec.push(action_entity);

    // resource entity (with attributes)
    let resource_entity = build_entity(&request.resource, &request.resource_attrs)?;
    entities_vec.push(resource_entity);

    let entities =
        Entities::from_entities(entities_vec, None).map_err(|e| e.to_string())?;

    // build context
    let context = if request.context.is_null() || request.context.is_object() && request.context.as_object().map_or(true, |m| m.is_empty()) {
        Context::empty()
    } else {
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

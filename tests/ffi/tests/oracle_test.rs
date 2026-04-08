//! oracle_test.rs - FFI test runner
//!
//! Loads JSON test cases and validates in two stages:
//! 1. Rust oracle (cedar_ffi_authorize) matches expected results
//! 2. C implementation (nxe_cedar_test_evaluate) matches oracle results

use std::env;
use std::ffi::CString;
use std::fs;
use std::os::raw::c_char;
use std::path::PathBuf;

use serde::Deserialize;

// Rust FFI (oracle) - via lib.rs
// rlib + staticlib, so extern "C" symbols are linked

extern "C" {
    fn cedar_ffi_authorize(
        policy_text: *const c_char,
        request_json: *const c_char,
    ) -> i32;
}

// reference to include cedar_ffi crate in link
use cedar_ffi as _;

// C implementation (test wrapper)
extern "C" {
    fn nxe_cedar_test_evaluate(
        policy_text: *const c_char,
        request_json: *const c_char,
    ) -> i32;

    fn nxe_cedar_test_last_error() -> *const c_char;
}

#[derive(Debug, Deserialize)]
struct TestFile {
    #[allow(dead_code)]
    description: String,
    phase: u32,
    tests: Vec<TestCase>,
}

#[derive(Debug, Deserialize)]
struct TestCase {
    name: String,
    #[allow(dead_code)]
    description: String,
    policy: String,
    request: serde_json::Value,
    expected: String,
}

fn expected_to_int(expected: &str) -> i32 {
    match expected {
        "allow" => 1,
        "deny" => 0,
        _ => panic!("unknown expected value: {expected}"),
    }
}

fn result_to_str(result: i32) -> &'static str {
    match result {
        0 => "deny",
        1 => "allow",
        -1 => "error",
        _ => "unknown",
    }
}

fn get_c_error() -> String {
    let ptr = unsafe { nxe_cedar_test_last_error() };
    if ptr.is_null() {
        String::from("(no error message)")
    } else {
        unsafe { std::ffi::CStr::from_ptr(ptr) }
            .to_string_lossy()
            .into_owned()
    }
}

fn test_cases_dir() -> PathBuf {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    PathBuf::from(manifest_dir).join("../cases")
}

fn max_phase() -> u32 {
    match env::var("NXE_CEDAR_TEST_PHASE") {
        Err(_) => u32::MAX,
        Ok(v) => v
            .parse()
            .unwrap_or_else(|_| panic!("invalid NXE_CEDAR_TEST_PHASE: {}", v)),
    }
}

fn collect_test_files(dir: &PathBuf) -> Vec<PathBuf> {
    let mut files = Vec::new();

    if !dir.exists() {
        return files;
    }

    fn walk(dir: &PathBuf, files: &mut Vec<PathBuf>) {
        let entries = fs::read_dir(dir)
            .unwrap_or_else(|e| panic!("failed to read directory {}: {e}", dir.display()));
        for entry in entries {
            let entry = entry
                .unwrap_or_else(|e| panic!("failed to read entry in {}: {e}", dir.display()));
            let path = entry.path();
            if path.is_dir() {
                walk(&path, files);
            } else if path.extension().map_or(false, |e| e == "json") {
                files.push(path);
            }
        }
    }

    walk(dir, &mut files);
    files.sort();
    files
}

/// Extract label from test file path: .../cases/phase1/basic_permit.json -> phase1/basic_permit
fn extract_label(path: &PathBuf) -> String {
    let s = path.to_string_lossy();
    let start = s.find("cases/").map(|i| i + 6).unwrap_or(0);
    let end = s.rfind('.').unwrap_or(s.len());
    s[start..end].to_string()
}

/// Oracle unit test: verify Rust Cedar result matches expected
#[test]
fn oracle_validation() {
    let dir = test_cases_dir();
    let max_phase = max_phase();
    let files = collect_test_files(&dir);

    assert!(
        !files.is_empty(),
        "no test case files found in {}",
        dir.display()
    );

    eprintln!();
    eprintln!("--- oracle validation (rust cedar vs expected) ---");

    let mut total = 0;
    let mut passed = 0;

    for file in &files {
        let content = fs::read_to_string(file)
            .unwrap_or_else(|e| panic!("failed to read {}: {e}", file.display()));

        let test_file: TestFile = serde_json::from_str(&content)
            .unwrap_or_else(|e| panic!("failed to parse {}: {e}", file.display()));

        if test_file.phase > max_phase {
            continue;
        }

        let label = extract_label(file);

        for tc in &test_file.tests {
            total += 1;

            let policy = CString::new(tc.policy.as_str()).unwrap();
            let request_json =
                CString::new(serde_json::to_string(&tc.request).unwrap())
                    .unwrap();

            let oracle_result = unsafe {
                cedar_ffi_authorize(policy.as_ptr(), request_json.as_ptr())
            };

            let expected = expected_to_int(&tc.expected);

            assert_eq!(
                oracle_result, expected,
                "\n{label} :: {} ... FAILED\n  expected {}, got {}\n  file: {}\n  policy: {}\n  desc: {}",
                tc.name,
                tc.expected,
                result_to_str(oracle_result),
                file.display(),
                tc.policy,
                tc.description,
            );

            eprintln!("  {label} :: {} ... ok", tc.name);
            passed += 1;
        }
    }

    assert!(
        total > 0,
        "no test cases matched phase <= {} under {}",
        max_phase,
        dir.display()
    );

    eprintln!("{passed} passed, {} failed", total - passed);
}

/// C implementation vs oracle comparison test
///
/// Ignored by default; run with: cargo test -- --include-ignored
#[test]
#[ignore]
fn c_vs_oracle() {
    let dir = test_cases_dir();
    let max_phase = max_phase();
    let files = collect_test_files(&dir);

    assert!(
        !files.is_empty(),
        "no test case files found in {}",
        dir.display()
    );

    eprintln!();
    eprintln!("--- c vs oracle (c implementation vs rust cedar) ---");

    let mut total = 0;
    let mut passed = 0;
    let skipped = 0;

    for file in &files {
        let content = fs::read_to_string(file).unwrap();
        let test_file: TestFile = serde_json::from_str(&content).unwrap();

        if test_file.phase > max_phase {
            continue;
        }

        let label = extract_label(file);

        for tc in &test_file.tests {
            total += 1;

            let policy = CString::new(tc.policy.as_str()).unwrap();
            let request_json =
                CString::new(serde_json::to_string(&tc.request).unwrap())
                    .unwrap();

            // oracle result
            let oracle_result = unsafe {
                cedar_ffi_authorize(policy.as_ptr(), request_json.as_ptr())
            };

            // C implementation result
            let c_result = unsafe {
                nxe_cedar_test_evaluate(
                    policy.as_ptr(),
                    request_json.as_ptr(),
                )
            };

            if c_result == -1 && oracle_result == -1 {
                eprintln!("  {label} :: {} ... ok (both error)", tc.name);
                passed += 1;
                continue;
            }
            if c_result == -1 {
                let err = get_c_error();
                panic!(
                    "\n{label} :: {} ... FAILED\n  oracle: {}, c_impl: error ({err})\n  file: {}\n  policy: {}\n  desc: {}",
                    tc.name,
                    result_to_str(oracle_result),
                    file.display(),
                    tc.policy,
                    tc.description,
                );
            }

            assert_eq!(
                c_result, oracle_result,
                "\n{label} :: {} ... FAILED\n  oracle: {}, c_impl: {}\n  file: {}\n  policy: {}\n  desc: {}",
                tc.name,
                result_to_str(oracle_result),
                result_to_str(c_result),
                file.display(),
                tc.policy,
                tc.description,
            );

            eprintln!("  {label} :: {} ... ok", tc.name);
            passed += 1;
        }
    }

    assert!(
        total > 0,
        "no test cases matched phase <= {} under {}",
        max_phase,
        dir.display()
    );

    let failed = total - passed - skipped;
    eprint!("{passed} passed, {failed} failed");
    if skipped > 0 {
        eprint!(", {skipped} skipped");
    }
    eprintln!();
}

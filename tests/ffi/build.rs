use std::env;

fn main() {
    let project_root = env::var("CARGO_MANIFEST_DIR").unwrap();
    let tests_dir = format!("{project_root}/..");
    let nxe_cedar_dir = format!("{tests_dir}/..");
    let src_dir = format!("{nxe_cedar_dir}/src");

    // jansson (pkg-config)
    let jansson = pkg_config::probe_library("jansson")
        .expect("jansson not found: install jansson-devel");

    let mut build = cc::Build::new();

    build
        // nginx stubs
        .file(format!("{tests_dir}/ngx_compat/ngx_stub.c"))
        // nxe-cedar sources
        .file(format!("{src_dir}/nxe_cedar_lexer.c"))
        .file(format!("{src_dir}/nxe_cedar_parser.c"))
        .file(format!("{src_dir}/nxe_cedar_expr.c"))
        .file(format!("{src_dir}/nxe_cedar_eval.c"))
        // C test wrapper
        .file(format!("{tests_dir}/ngx_compat/nxe_cedar_test_wrapper.c"))
        // include paths
        .include(format!("{tests_dir}/ngx_compat"))
        .include(&src_dir)
        // compiler flags
        .flag("-std=c99")
        .flag("-Wall")
        .flag("-Wextra")
        .flag("-Wno-unused-parameter");

    // jansson include paths
    for path in &jansson.include_paths {
        build.include(path);
    }

    build.compile("nxe_cedar_test");

    // jansson link
    for path in &jansson.link_paths {
        println!("cargo:rustc-link-search=native={}", path.display());
    }
    for lib in &jansson.libs {
        println!("cargo:rustc-link-lib={lib}");
    }

    // rebuild triggers
    println!("cargo:rerun-if-changed={tests_dir}/ngx_compat/ngx_stub.c");
    println!("cargo:rerun-if-changed={tests_dir}/ngx_compat/ngx_stub.h");
    println!(
        "cargo:rerun-if-changed={tests_dir}/ngx_compat/nxe_cedar_test_wrapper.c"
    );
    println!(
        "cargo:rerun-if-changed={tests_dir}/ngx_compat/nxe_cedar_test_wrapper.h"
    );
    println!("cargo:rerun-if-changed={src_dir}/nxe_cedar_types.h");
    println!("cargo:rerun-if-changed={src_dir}/nxe_cedar_lexer.h");
    println!("cargo:rerun-if-changed={src_dir}/nxe_cedar_lexer.c");
    println!("cargo:rerun-if-changed={src_dir}/nxe_cedar_parser.h");
    println!("cargo:rerun-if-changed={src_dir}/nxe_cedar_parser.c");
    println!("cargo:rerun-if-changed={src_dir}/nxe_cedar_expr.h");
    println!("cargo:rerun-if-changed={src_dir}/nxe_cedar_expr.c");
    println!("cargo:rerun-if-changed={src_dir}/nxe_cedar_eval.h");
    println!("cargo:rerun-if-changed={src_dir}/nxe_cedar_eval.c");
}

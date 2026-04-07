use std::env;

fn main() {
    let project_root = env::var("CARGO_MANIFEST_DIR").unwrap();
    let tests_dir = format!("{project_root}/..");
    let nxe_cedar_dir = format!("{tests_dir}/..");

    // jansson (pkg-config)
    let jansson = pkg_config::probe_library("jansson")
        .expect("jansson not found: install jansson-devel");

    let mut build = cc::Build::new();

    build
        // nginx スタブ
        .file(format!("{tests_dir}/ngx_compat/ngx_stub.c"))
        // nxe-cedar スタブ（本体実装が揃ったら差し替え）
        .file(format!("{tests_dir}/nxe_cedar_stub.c"))
        // C テストラッパー
        .file(format!("{tests_dir}/ngx_compat/nxe_cedar_test_wrapper.c"))
        // インクルードパス
        .include(format!("{tests_dir}/ngx_compat"))
        .include(&nxe_cedar_dir)
        // コンパイルフラグ
        .flag("-std=c99")
        .flag("-Wall")
        .flag("-Wextra")
        .flag("-Wno-unused-parameter");

    // jansson インクルードパス
    for path in &jansson.include_paths {
        build.include(path);
    }

    build.compile("nxe_cedar_test");

    // jansson リンク
    for path in &jansson.link_paths {
        println!("cargo:rustc-link-search=native={}", path.display());
    }
    for lib in &jansson.libs {
        println!("cargo:rustc-link-lib={lib}");
    }

    // リビルドトリガー
    println!("cargo:rerun-if-changed={tests_dir}/ngx_compat/ngx_stub.c");
    println!("cargo:rerun-if-changed={tests_dir}/ngx_compat/ngx_stub.h");
    println!(
        "cargo:rerun-if-changed={tests_dir}/ngx_compat/nxe_cedar_test_wrapper.c"
    );
    println!(
        "cargo:rerun-if-changed={tests_dir}/ngx_compat/nxe_cedar_test_wrapper.h"
    );
    println!("cargo:rerun-if-changed={tests_dir}/nxe_cedar_stub.c");
    println!("cargo:rerun-if-changed={nxe_cedar_dir}/nxe_cedar_types.h");
}

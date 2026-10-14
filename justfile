code-macos:
    code .vscode/rust-skia-macos.code-workspace

code-macos-gl:
    code .vscode/rust-skia-macos-gl.code-workspace

code-windows:
    code .vscode/rust-skia-windows.code-workspace

# Build skia-bindings for a target and publish the archive to NuwaProject/rust-skia.
publish-binaries target="wasm32-unknown-unknown" features="":
    #!/usr/bin/env bash
    set -euo pipefail

    # Usage:
    #   just publish-binaries
    #   just publish-binaries wasm32-unknown-unknown gl,pdf,textlayout
    #   just publish-binaries x86_64-unknown-linux-gnu
    # The first build of a target downloads skia's third-party dependencies
    # and the WASI SDK; set http(s)_proxy if cargo, git, or gh need it.

    # Absolute: the build script resolves the staging directory against the
    # package root, not the justfile directory.
    stage="{{justfile_directory()}}/target/skia-binaries-staging"
    repo=NuwaProject/rust-skia

    rm -rf "$stage"
    mkdir -p "$stage"

    feature_args=""
    if [ -n "{{features}}" ]; then
        feature_args="--features {{features}}"
    fi

    # Cargo skips an unchanged build script, which would skip the export.
    touch skia-bindings/build.rs

    BUILD_ARTIFACTSTAGINGDIRECTORY="$stage" \
        cargo build -p skia-bindings --release --target {{target}} $feature_args

    tag=$(cat "$stage/skia-binaries/tag.txt")
    key=$(cat "$stage/skia-binaries/key.txt")
    mkdir -p "$stage/$tag"
    tar czf "$stage/$tag/skia-binaries-$key.tar.gz" -C "$stage" skia-binaries

    if gh release view "$tag" -R "$repo" >/dev/null 2>&1; then
        gh release upload "$tag" "$stage/$tag/skia-binaries-$key.tar.gz" --clobber -R "$repo"
    else
        gh release create "$tag" --prerelease -R "$repo" \
            "$stage/$tag/skia-binaries-$key.tar.gz"
    fi

    echo "published: https://github.com/$repo/releases/download/$tag/skia-binaries-$key.tar.gz"

# Publishes binaries for both consumers: linux-amd64 and wasm32-unknown-unknown.
publish-binaries-linux-wasm32:
    just publish-binaries x86_64-unknown-linux-gnu
    just publish-binaries wasm32-unknown-unknown gl,pdf,textlayout

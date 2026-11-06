#!/usr/bin/env bash
# Build, test, ad-hoc sign, and install both LUFS plugins for Apple Silicon.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"
install_only=false
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    cat <<'HELP'
Usage:
  bash scripts/build-sign-install-mac.sh [extra CMake configure arguments]
  bash scripts/build-sign-install-mac.sh --install-only "/path/to/LUFS Reference.vst3" "/path/to/LUFS Compensation.vst3"

Builds and tests both arm64 VST3 plugins, ad-hoc signs them, then installs to:
  ~/Library/Audio/Plug-Ins/VST3/LUFS Reference.vst3
  ~/Library/Audio/Plug-Ins/VST3/LUFS Compensation.vst3

Requires macOS on Apple Silicon (M2 and later supported), Xcode Command Line
Tools, and CMake 3.22+. Install-only mode needs only the two built VST3 bundles.
Optional: LUFS_BUILD_DIR and LUFS_INSTALL_DIR override the directories.
Close your DAW before installing; restart it or rescan plugins afterwards.
HELP
    exit 0
fi
if [[ "${1:-}" == "--install-only" ]]; then
    [[ $# -eq 3 ]] || { echo "Usage: $0 --install-only '/path/to/LUFS Reference.vst3' '/path/to/LUFS Compensation.vst3'" >&2; exit 2; }
    install_only=true
    bundles=("$2" "$3")
fi
[[ "$(uname -s)" == "Darwin" ]] || { echo "This script requires macOS." >&2; exit 1; }
[[ "$(uname -m)" == "arm64" ]] || { echo "Run from a native Apple Silicon terminal (disable Rosetta)." >&2; exit 1; }
for tool in codesign ditto; do
    command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 1; }
done
if [[ "$install_only" == false ]]; then
    for tool in cmake ctest; do
        command -v "$tool" >/dev/null || { echo "Install CMake 3.22+ first (brew install cmake)." >&2; exit 1; }
    done
    build_dir="${LUFS_BUILD_DIR:-$project_dir/build-mac}"
    cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 "$@"
    cmake --build "$build_dir" --config Release --parallel 3
    ctest --test-dir "$build_dir" -C Release --output-on-failure
    bundles=("$build_dir/LUFSReference_artefacts/Release/VST3/LUFS Reference.vst3"
             "$build_dir/LUFSCompensation_artefacts/Release/VST3/LUFS Compensation.vst3")
fi
names=("LUFS Reference" "LUFS Compensation")
for i in 0 1; do
    [[ -f "${bundles[$i]}/Contents/MacOS/${names[$i]}" ]] || {
        echo "Not a ${names[$i]} macOS VST3 bundle: ${bundles[$i]}" >&2; exit 1;
    }
done
install_dir="${LUFS_INSTALL_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}"
mkdir -p "$install_dir"
staging="$(mktemp -d "$install_dir/.LUFS-install.XXXXXX")"
installed=(false false)
success=false
cleanup() {
    local i destination recovery_failed=false
    if [[ "$success" == false ]]; then
        for i in 0 1; do
            destination="$install_dir/${names[$i]}.vst3"
            if [[ "${installed[$i]}" == true ]]; then
                rm -rf "$destination" || recovery_failed=true
            fi
            if [[ -e "$staging/previous-$i.vst3" ]]; then
                if [[ -e "$destination" || -L "$destination" ]]; then
                    recovery_failed=true
                else
                    mv "$staging/previous-$i.vst3" "$destination" || recovery_failed=true
                fi
            fi
        done
    fi
    if [[ "$recovery_failed" == true ]]; then
        echo "Could not fully restore the previous installation. Backups retained at: $staging" >&2
    else
        rm -rf "$staging"
    fi
}
trap cleanup EXIT
# Preserve source bundles and installed copies until BOTH staged copies pass.
for i in 0 1; do
    staged="$staging/${names[$i]}.vst3"
    ditto "${bundles[$i]}" "$staged"
    codesign --force --deep --sign - "$staged"
    codesign --verify --deep --strict "$staged"
done
for i in 0 1; do
    destination="$install_dir/${names[$i]}.vst3"
    if [[ -e "$destination" ]]; then mv "$destination" "$staging/previous-$i.vst3"; fi
    mv "$staging/${names[$i]}.vst3" "$destination"
    installed[$i]=true
done
success=true
printf 'Installed both plugins in: %s\nRestart or rescan plugins in your VST3 DAW.\n' "$install_dir"

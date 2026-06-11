#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
#  compile_shaders.sh
#
#  Compiles all GLSL shaders to SPIR-V using glslc (from shaderc,
#  part of the Vulkan SDK).
#
#  On CachyOS / Arch, install with:
#      sudo pacman -S shaderc
#
#  Usage:
#      ./compile_shaders.sh             # compile all
#      ./compile_shaders.sh voxel       # compile only shaders named voxel.*
#
#  Output: shaders/*.spv  (next to the source files)
#  The executable loads them from ./shaders/ relative to the CWD,
#  so run the game from the project root.
# ─────────────────────────────────────────────────────────────────

set -euo pipefail

SHADER_DIR="$(dirname "$0")/shaders"
GLSLC="glslc"

# Optional filter argument
FILTER="${1:-}"

compile() {
    local src="$1"
    local out="${src}.spv"

    if [[ -n "$FILTER" && "$src" != *"$FILTER"* ]]; then
        return
    fi

    echo "  Compiling: $(basename "$src") → $(basename "$out")"

    # -O    optimise (remove dead code)
    # -g    include debug info (remove for release builds)
    # --target-env=vulkan1.2 matches your VK_API_VERSION_1_2
    "$GLSLC" \
        --target-env=vulkan1.2 \
        -O \
        -o "$out" \
        "$src"
}

echo "━━━ Shader compilation ━━━━━━━━━━━━━━━━━━━━━"

# Vertex shaders
for f in "$SHADER_DIR"/*.vert; do
    [[ -e "$f" ]] && compile "$f"
done

# Fragment shaders
for f in "$SHADER_DIR"/*.frag; do
    [[ -e "$f" ]] && compile "$f"
done

# Compute shaders (for future chunk meshing, light propagation…)
for f in "$SHADER_DIR"/*.comp; do
    [[ -e "$f" ]] && compile "$f"
done

echo "━━━ Done ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

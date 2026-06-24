#!/usr/bin/env bash
# 用 clang-format 按 .clang-format 规则格式化 src/ 下所有 C++ 源码。
#   scripts/format.sh           原地格式化
#   scripts/format.sh --check   仅检查、不修改(CI 用，发现未格式化即非零退出)
set -euo pipefail

cd "$(dirname "$0")/.."

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
if ! command -v "$CLANG_FORMAT" >/dev/null 2>&1; then
    echo "找不到 clang-format,可设置 CLANG_FORMAT 指向可执行文件" >&2
    exit 1
fi

files=()
while IFS= read -r f; do
    files+=("$f")
done < <(find src \( -name '*.cpp' -o -name '*.h' \) | sort)
if [[ ${#files[@]} -eq 0 ]]; then
    echo "src/ 下没有 C++ 源码" >&2
    exit 0
fi

if [[ "${1:-}" == "--check" ]]; then
    "$CLANG_FORMAT" --dry-run --Werror "${files[@]}"
    echo "格式检查通过:${#files[@]} 个文件"
else
    "$CLANG_FORMAT" -i "${files[@]}"
    echo "已格式化 ${#files[@]} 个文件"
fi

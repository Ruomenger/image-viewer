#!/usr/bin/env bash
# 用 clang-tidy 按 .clang-tidy 规则静态检查 src/ 下的 .cpp(头文件经 HeaderFilterRegex 一并检查)。
#   scripts/tidy.sh                 使用默认构建目录 build/default
#   scripts/tidy.sh build/release   指定其他构建目录
#   scripts/tidy.sh --fix           自动修复可修复项(传给 clang-tidy)
#   scripts/tidy.sh --strict        将告警视为错误(CI 用,有告警即非零退出)
# 依赖 compile_commands.json,需先 cmake --preset default 配置过。
set -euo pipefail

cd "$(dirname "$0")/.."

CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"
if ! command -v "$CLANG_TIDY" >/dev/null 2>&1; then
    echo "找不到 clang-tidy,可设置 CLANG_TIDY 指向可执行文件" >&2
    exit 1
fi

extra_args=()
build_dir="build/default"
for arg in "$@"; do
    case "$arg" in
        --fix) extra_args+=("--fix") ;;
        --strict) extra_args+=("--warnings-as-errors=*") ;;
        *) build_dir="$arg" ;;
    esac
done

if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    echo "缺少 $build_dir/compile_commands.json,请先运行:cmake --preset default" >&2
    exit 1
fi

files=()
while IFS= read -r f; do
    files+=("$f")
done < <(find src -name '*.cpp' | sort)
if [[ ${#files[@]} -eq 0 ]]; then
    echo "src/ 下没有 .cpp 翻译单元" >&2
    exit 0
fi

tidy_args=("-p" "$build_dir")

# macOS:compile_commands.json 由 AppleClang 生成,但 clang-tidy 多为 Homebrew LLVM,
# 缺少隐式 SDK 路径会报 'type_traits'/'TargetConditionals.h' not found。显式注入 SDK sysroot。
if command -v xcrun >/dev/null 2>&1; then
    sdk_path="$(xcrun --show-sdk-path 2>/dev/null || true)"
    if [[ -n "$sdk_path" ]]; then
        tidy_args+=("--extra-arg=-isysroot" "--extra-arg=$sdk_path")
    fi
fi

# 注意:bash 3.2(macOS 自带)在 set -u 下展开空数组会报错,用 ${arr[@]+...} 兜底。
"$CLANG_TIDY" "${tidy_args[@]}" ${extra_args[@]+"${extra_args[@]}"} "${files[@]}"
echo "clang-tidy 检查完成:${#files[@]} 个翻译单元"

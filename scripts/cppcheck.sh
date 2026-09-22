#!/usr/bin/env bash
# Static analysis of the first-party sources with cppcheck.
#
# Usage:
#   scripts/cppcheck.sh            # warning, performance, portability checks (parallel)
#   scripts/cppcheck.sh unused     # whole-program unusedFunction pass (single-threaded, slower)
#
# Third-party code (source/xml, source/libs, source/utils/minizip, source/mload/modules) is skipped.
# Some checks are suppressed because they describe the style this codebase and libogc are
# written in rather than a defect, and cppcheck 2.19 onwards reports them in the hundreds:
# dangerousTypeCast (every old-style C cast), uninitMemberVarNoCtor (plain structs with no
# constructor), noCopyConstructor / noOperatorEq / duplInheritedMember, and the two
# performance notes about prefix operators and initialiser lists. uninitMemberVar, which
# reports real classes, stays on. Hits inside the toolchain headers are dropped as well.
# LIBOGC_INC must point at a libogc include directory so ATTRIBUTE_ALIGN and friends parse;
# without it cppcheck silently drops functions from files it could not parse and the
# unused-function list is wrong. Default: C:/devkitPro/libogc/include on Windows.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPPCHECK="${CPPCHECK:-cppcheck}"
if ! command -v "$CPPCHECK" >/dev/null 2>&1; then
	CPPCHECK="/c/Program Files/Cppcheck/cppcheck.exe"
fi
LIBOGC_INC="${LIBOGC_INC:-/c/devkitPro/libogc/include}"

SUPPRESS=(--suppress=dangerousTypeCast --suppress=uninitMemberVarNoCtor
	--suppress=noCopyConstructor --suppress=noOperatorEq --suppress=duplInheritedMember
	--suppress=postfixOperator --suppress=useInitializationList
	"--suppress=*:$LIBOGC_INC/*" "--suppress=*:$ROOT/portlibs/include/*")

COMMON=(-q --suppress=missingInclude --suppress=missingIncludeSystem "${SUPPRESS[@]}"
	--template='{file}:{line}:{severity}:{id}:{message}'
	-DGEKKO -DHW_RVL -I "$ROOT/source" -I "$ROOT/portlibs/include" -I "$LIBOGC_INC"
	-i "$ROOT/source/xml" -i "$ROOT/source/libs" -i "$ROOT/source/utils/minizip" -i "$ROOT/source/mload/modules")

case "${1:-check}" in
	check)  "$CPPCHECK" -j8 --enable=warning,performance,portability --inline-suppr "${COMMON[@]}" "$ROOT/source" ;;
	unused) "$CPPCHECK" -j1 --enable=unusedFunction "${COMMON[@]}" "$ROOT/source" ;;
	*) echo "Usage: $0 [check|unused]" >&2; exit 1 ;;
esac

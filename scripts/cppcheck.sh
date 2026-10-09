#!/usr/bin/env bash
# Static analysis of the first-party sources with cppcheck.
#
# Usage:
#   scripts/cppcheck.sh            # warning, performance, portability checks (parallel)
#   scripts/cppcheck.sh unused     # whole-program unusedFunction pass (single-threaded, slower)
#
# Third-party code is skipped: source/libs and source/mload/modules. pugixml, minizip and
# wolfSSL are no longer in the tree (deps/build.sh builds them). source/xml is GameTDB,
# this project's own reader for a downloaded wiitdb.xml, and source/utils/minizip is this
# project's extractZip() on top of minizip; excluding whole directories hid both once.
# verify-build.sh and CI gate on source/libs as well, so keep the lists the same.
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
	"--suppress=*:$LIBOGC_INC/*")

COMMON=(-q --suppress=missingInclude --suppress=missingIncludeSystem "${SUPPRESS[@]}"
	--template='{file}:{line}:{severity}:{id}:{message}'
	-DGEKKO -DHW_RVL -I "$ROOT/source" -I "$LIBOGC_INC"
	-i "$ROOT/source/libs" -i "$ROOT/source/mload/modules")

case "${1:-check}" in
	check)  "$CPPCHECK" -j8 --enable=warning,performance,portability --inline-suppr "${COMMON[@]}" "$ROOT/source" ;;
	unused) "$CPPCHECK" -j1 --enable=unusedFunction "${COMMON[@]}" "$ROOT/source" ;;
	*) echo "Usage: $0 [check|unused]" >&2; exit 1 ;;
esac

#!/bin/sh
# No tracked file names a path of one workstation (C:/projects/..., /c/tools/..., ...).
# The check is tests/test_workstation_paths.py; this wrapper lets CI's `tests/*.sh` loop run it.
#
# Usage: tests/check-workstation-paths.sh
set -u
here="$(dirname "$0")"
for py in python3 python py; do
	# A name can be on PATH yet be Windows' Store stub, which fails to run.
	if "$py" -c "" >/dev/null 2>&1; then
		exec "$py" "$here/test_workstation_paths.py"
	fi
done
echo "FAIL: needs python3" >&2
exit 1

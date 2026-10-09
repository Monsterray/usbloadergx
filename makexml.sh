#! /bin/bash

# LOADER_VERSION is the version the loader shows, MAJOR.MINOR.PATCH from the
# last release tag (vMAJOR.MINOR.PATCH, for example v4.1.0). A build of a later
# commit adds semver build metadata: 4.1.0+5.g22ba8ad is 5 commits after v4.1.0,
# at commit 22ba8ad, and .dirty marks uncommitted changes. With no such tag (or
# no git) it is 0.0.0+g<commit>. LOADER_REV (version.txt) is the old revision
# number, which the settings files and the updater still compare.
rev_new=$(cat version.txt)
rev_old=$(sed -n 2p ./source/version.h 2>/dev/null | cut -d '"' -f 2)

# Retrieve git info
git_new=$(git rev-parse HEAD 2>/dev/null | head -c 7)
[ -z "$git_new" ] && git_new="0000001"
commit_message=$(git show -s --format="%<(52,trunc)%s" HEAD 2>/dev/null | xargs echo -n)
[ -z "$commit_message" ] && commit_message="unable to get the commit message"
git_old=$(sed -n 3p ./source/version.h 2>/dev/null | cut -d '"' -f 2)

# git describe --long: vX.Y.Z-<commits since>-g<commit>[-dirty]
desc=$(git describe --tags --long --match 'v[0-9]*.[0-9]*.[0-9]*' --abbrev=7 --dirty 2>/dev/null)
if [[ "$desc" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$ ]]; then
	ver_new="${BASH_REMATCH[1]}"
	meta=""
	[ "${BASH_REMATCH[2]}" != "0" ] && meta="${BASH_REMATCH[2]}.g${BASH_REMATCH[3]}"
	[ -n "${BASH_REMATCH[4]}" ] && meta="${meta:+$meta.}dirty"
	[ -n "$meta" ] && ver_new="$ver_new+$meta"
else
	ver_new="0.0.0+g$git_new"
fi
ver_old=$(sed -n 4p ./source/version.h 2>/dev/null | cut -d '"' -f 2)

if [ "$rev_new" != "$rev_old" ] || [ "$git_new" != "$git_old" ] || [ "$ver_new" != "$ver_old" ]; then
	rm -f ./source/version.h 2>/dev/null
	echo "// Don't manually edit this file" >> ./source/version.h
	echo "#define LOADER_REV \"$rev_new\"" >> ./source/version.h
	echo "#define GIT_VER \"$git_new\"" >> ./source/version.h
	echo "#define LOADER_VERSION \"$ver_new\"" >> ./source/version.h
fi

if [ "$git_new" != "$git_old" ]; then
	if [ -z "$git_old" ]; then
		echo "Created version.h and set the commit ID to $git_new ($commit_message)" >&2
	else
		echo "Changed the commit ID from $git_old to $git_new ($commit_message)" >&2
	fi
	
	echo >&2
fi

# The HBC requires this date format for app sorting
rev_date=`date -u +%Y%m%d%H%M%S`
cat <<EOF > ./HBC/meta.xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<app version="1">
	<name> USB Loader GX</name>
	<coder>blackb0x</coder>
	<version>$ver_new</version>
	<release_date>$rev_date</release_date>
	<!-- to enable arguments change disabled_arguments to arguments -->
	<disabled_arguments>
		<arg>--ios=249</arg>
		<arg>--bootios=58</arg>
		<arg>--usbport=0</arg>
		<arg>--sdmode=0</arg>
	</disabled_arguments>
	<ahb_access/>
	<short_description>Load games from a USB or SD card</short_description>
	<long_description>USB Loader GX allows you to install your games to a USB storage device or SD card. You can then boot your games faster, download and use cheats, or apply various patches.

Home:
https://github.com/wiidev/usbloadergx
Support:
https://gbatemp.net/threads/149922</long_description>
</app>
EOF

#!/bin/bash

test -d .git || exit 1;

if [ "$(git status --porcelain --untracked-files=no)" != "" ]; then
	echo "non-committed files"
	exit 1;
fi
branch=$(git name-rev --name-only HEAD)

git checkout cvs
git cvsimport -d:pserver:anonymous@minidlna.cvs.sourceforge.net:/cvsroot/minidlna -o cvs -k -u -m minidlna
git checkout $branch

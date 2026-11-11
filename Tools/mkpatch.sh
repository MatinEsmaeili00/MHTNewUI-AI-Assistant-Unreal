#!/bin/bash
# copies the project's current Source / plugin source / .uproject over the baseline and prints the diff (= the assistant work)
S="$(cd "$(dirname "$0")/.." && pwd)"; PRJ="/Users/mesmae6/Perforce/maclab_digitalhuman_main3/new copy/MHTNewUI"
cd "$S/baseline" && git checkout -q . && git clean -qfd
rsync -a --exclude '.DS_Store' "$PRJ/Source/" Source/
rsync -a "$PRJ/Plugins/MetaHumanAnatomyLayers/Source/" Plugins/MetaHumanAnatomyLayers/Source/
cp "$PRJ/MetaHumanTest.uproject" .
git add -A -N . >/dev/null
git diff "$@"

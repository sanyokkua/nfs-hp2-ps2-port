#!/bin/bash
# upstream/: ZloiKILLER's nfshp2-port-win at the pinned version (from GitHub, or from mirror/ when GitHub no longer has it),
# with patches/*.patch applied on a local branch `mac`. Run again after pulling: it rebuilds the branch from the pin.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
PIN=da838179be46821eb10c306326504320850733fc      # nfshp2-port-win 0.14
URL=https://github.com/ZloiKILLER/nfshp2-port-win.git
BUNDLE=mirror/nfshp2-port-win.bundle
if [ ! -d upstream/.git ]; then
  git clone -q "$URL" upstream 2>/dev/null || git clone -q "$BUNDLE" upstream
fi
git -C upstream cat-file -e "$PIN^{commit}" 2>/dev/null || git -C upstream fetch -q origin || git -C upstream fetch -q "$ROOT/$BUNDLE" '+refs/*:refs/*'
[ -f "$BUNDLE" ] || { mkdir -p mirror; git -C upstream bundle create "$ROOT/$BUNDLE" --all; }   # keep our own copy
git -C upstream checkout -q -f -B mac "$PIN"
git -C upstream -c core.hooksPath=/dev/null am -q "$ROOT"/patches/*.patch
echo "upstream: $(git -C upstream log -1 --format='%h %s') on $(git -C upstream rev-parse --short "$PIN")"

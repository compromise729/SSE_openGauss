#!/bin/bash
set -u
for repo in /opt/og/openGauss-server /opt/og/SSE_openGauss; do
 if test -d "$repo"; then
  echo "REPO=$repo"
  git --git-dir="$repo/.git" --work-tree="$repo" rev-parse HEAD
  git --git-dir="$repo/.git" --work-tree="$repo" status --short --untracked-files=no
  git --git-dir="$repo/.git" --work-tree="$repo" log -4 --oneline
  git --git-dir="$repo/.git" --work-tree="$repo" cat-file -t a3567e4151709fb0c1ffb7e9a8cb36d07d5c257f 2>&1
 fi
done
ps -eo user,pid,args | grep '[g]aussdb'
ls -ld /opt/og/* /home/omm/.sse_opengauss_env
cat /home/omm/.sse_opengauss_env

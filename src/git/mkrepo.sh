#!/bin/sh
# The scratch repository the magit traces start from: fixed names and dates,
# so every hash repeats.  $1 is created; $1.remote.git is its origin.
set -e
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
d=$1; rm -rf "$d" "$d.remote.git"; mkdir -p "$d"; cd "$d"
export GIT_AUTHOR_NAME=Tester GIT_AUTHOR_EMAIL=t@example.com GIT_COMMITTER_NAME=Tester GIT_COMMITTER_EMAIL=t@example.com
export GIT_AUTHOR_DATE='2026-01-01T00:00:00Z' GIT_COMMITTER_DATE='2026-01-01T00:00:00Z'
git init -q -b main --template= .
git config user.name Tester; git config user.email t@example.com
printf 'one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven\ntwelve\n' > a.txt
printf 'alpha\nbeta\n' > b.txt
git add a.txt b.txt; git commit -q -m 'First commit'
git init -q --bare -b main --template= "$d.remote.git"
git remote add origin "$d.remote.git"
git push -q -u origin main
printf 'gamma\n' >> b.txt; git commit -q -am 'Second commit'
sed -i 's/^two$/TWO/; s/^eleven$/ELEVEN/' a.txt
printf 'delta\n' >> b.txt; git add b.txt
printf 'new\n' > c.txt

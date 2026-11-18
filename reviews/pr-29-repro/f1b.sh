sh src/git/mkrepo.sh /tmp/rv/r1
printf 'o\ns\nk <C-j><C-j>x\np\n$ echo precious >> a.txt\ns\nk y\ns\np\n$ git diff --stat; tail -n 2 a.txt\n' | reviews/pr-29-repro/drive /tmp/rv/r1

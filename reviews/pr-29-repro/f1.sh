sh src/git/mkrepo.sh /tmp/rv/r1
printf 'o\ns\nk <C-j><C-j><Tab><C-j>\nk x\np\n$ sed -i s/TWO/two/ a.txt\ns\np\nk y\ns\np\n$ git diff\n' | reviews/pr-29-repro/drive /tmp/rv/r1

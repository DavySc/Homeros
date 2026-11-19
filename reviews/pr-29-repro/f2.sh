mkdir -p /tmp/rv/bin; printf '#!/bin/sh\necho "$*" >> /tmp/rv/gitlog\nexec /usr/bin/git "$@"\n' > /tmp/rv/bin/git; chmod +x /tmp/rv/bin/git
sh src/git/mkrepo.sh /tmp/rv/r1; : > /tmp/rv/gitlog
printf 'o\ns\nk ll\ns\nk q\ns\n$ echo ---- >> /tmp/rv/gitlog\nk <C-j><C-j>s\ns\n' | PATH=/tmp/rv/bin:$PATH reviews/pr-29-repro/drive /tmp/rv/r1
sed 's/.*diff.noPrefix=false //' /tmp/rv/gitlog | cut -c1-100

printf 'not executable\n' > noexec.sh
printf '#!%s/ext/missing-interp\necho hi\n' "$BASE" > badinterp.sh; chmod +x badinterp.sh
printf '#!/bin/sh\necho "args:$#:$1|$2|$3"\n' > args.sh; chmod +x args.sh

printf 'not executable\n' > noexec.sh
printf '#!/nonexistent/interp\necho hi\n' > badinterp.sh; chmod +x badinterp.sh
printf '#!/bin/sh\necho "args:$#:$1|$2|$3"\n' > args.sh; chmod +x args.sh

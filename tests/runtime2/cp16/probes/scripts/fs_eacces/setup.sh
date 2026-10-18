printf 'secret\n' > noread.txt; chmod 000 noread.txt
mkdir ro; printf 'r\n' > ro/in.txt; chmod 555 ro
mkdir nox; printf 'h\n' > nox/hidden.txt; chmod 000 nox

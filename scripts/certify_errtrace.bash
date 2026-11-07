# certify.py runner (owner 2026-11-08 03:27, test-v44-language-foundation): sourced through
# BASH_ENV by every bash script of a gate whose extractor asks for it, on the candidate and on
# every baseline tree alike (the runner's file, the trees' scripts unchanged). When a command
# makes the script exit under `set -e` (errexit), it prints which script and line, the exit
# status and the command, so a smoke script that dies silently still names its failing case.
# Nothing is printed for failures the script handles itself (conditions, ||, no errexit).
# No errtrace option (review of 4ec1aa80): it would make a script's own ERR trap fire inside its
# functions; a failing call is still reported at its top-level call site.
trap '__certify_rc=$?; case $- in *e*) printf "CERTIFY-ERR %s:%s rc=%s cmd=%s\n" "${BASH_SOURCE[0]##*/}" "$LINENO" "$__certify_rc" "$BASH_COMMAND" >&2;; esac' ERR

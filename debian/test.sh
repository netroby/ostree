#!/bin/sh

set -e

export VERBOSE=1

try_tests=5

failed=0
make check || failed=1

if [ "$failed" -gt 0 ]; then
    echo "Test failed! Checking how reproducible it is..."
    for i in $(seq 1 "$(( $try_tests - 1 ))"); do
        if ! make check; then
            failed=$(( $failed + 1 ))
        fi
    done
fi

pkill --full "gpg-agent --homedir /var/tmp/tap-test\\.[^/]+/.*" || :

if pgrep lt-ostree || pgrep --full "gpg-agent --homedir /var/tmp/tap-test."; then \
    echo "WARNING: daemon processes were leaked"
    pgrep gpg-agent | xargs --no-run-if-empty ps ww
    pgrep lt-ostree | xargs --no-run-if-empty ps ww
fi

if [ "$failed" -gt 0 ]; then
    echo "Failed $failed out of $try_tests test runs"
    exit 1
fi

exit 0

# vim:set et sw=4 sts=4:
#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
c++ -std=c++20 -Wall -Wextra -Werror -pedantic tests/timeline_test.cpp -o /tmp/wma-timeline-tests
/tmp/wma-timeline-tests

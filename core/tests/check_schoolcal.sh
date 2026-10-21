#!/bin/sh
# The school calendar's sums: dates moved by school days (in the style they were typed), days with
# no class, a shift's plan, a district .ics file, and next year's calendar.
#   sh core/tests/check_schoolcal.sh     (after sh core/build.sh)
# After a change on purpose: ./chalkboard-core --dump schoolcal tests/schoolcal.json > tests/schoolcal.expected
set -e
cd "$(dirname "$0")/.."
if ./chalkboard-core --dump schoolcal tests/schoolcal.json | cmp -s - tests/schoolcal.expected; then
    echo "school calendar: all as expected"
else
    ./chalkboard-core --dump schoolcal tests/schoolcal.json | diff tests/schoolcal.expected - | head -40
    echo "SCHOOL CALENDAR: DIFFERENT (above)"
    exit 1
fi

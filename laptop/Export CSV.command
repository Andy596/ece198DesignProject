#!/bin/bash
# Double-click on macOS to export all entries to a CSV in the exports folder.
cd "$(dirname "$0")" || exit 1
./export.bash
status=$?
[ $status -eq 0 ] && open exports 2>/dev/null
echo
read -r -n 1 -p "Press any key to close this window..."
exit $status

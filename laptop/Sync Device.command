#!/bin/bash
# Double-click on macOS to sync the Session Log Station into the database.
cd "$(dirname "$0")" || exit 1
./sync.bash
status=$?
echo
read -r -n 1 -p "Press any key to close this window..."
exit $status

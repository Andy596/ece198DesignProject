#!/bin/bash
# Double-click on macOS to export completed entries and current plans to CSV.
cd "$(dirname "$0")" || exit 1
python3 export_dosages.py
status=$?
[ $status -eq 0 ] && open exports 2>/dev/null
echo
read -r -n 1 -p "Press any key to close this window..."
exit $status

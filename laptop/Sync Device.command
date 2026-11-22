#!/bin/bash
# Double-click on macOS, then tap A3 on the Arduino's main menu. Saves the
# Arduino's dosage snapshot into session_log.db (firmware ece198Codes108).
cd "$(dirname "$0")" || exit 1
python3 sync_dosages.py
status=$?
echo
read -r -n 1 -p "Press any key to close this window..."
exit $status

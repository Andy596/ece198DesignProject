#!/usr/bin/env python3
"""Receive a dosage snapshot from the Session Log Station over USB and save it in SQLite.

Run this, then tap A3 on the Arduino's main user menu. The Arduino shows
"USB sync done" only after the snapshot is committed to the database; any
failure answers ERR, so the Arduino keeps its records and you can retry.
Re-syncing the same records never creates duplicates.

Exit codes: 0 saved, 1 nothing saved (retry), 2 saved but the Arduino may not
have heard the ACK (retrying is safe).
"""

import argparse
from glob import glob
import os
from pathlib import Path
import select
import sqlite3
import sys
import termios
import time
import tty

import dosage_db

MAX_LINE = 192
TRANSFER_TIMEOUT = 20
DEFAULT_DB = Path(__file__).resolve().parent / "session_log.db"


def find_port():
    ports = sorted(set(glob("/dev/cu.usbmodem*") + glob("/dev/ttyACM*")))
    if len(ports) != 1:
        detail = ", ".join(ports) if ports else "none found"
        raise ValueError(f"Expected one Arduino USB port ({detail}). Use --port DEVICE.")
    return ports[0]


class SerialConnection:
    def __init__(self, fd):
        self.fd = fd
        self.buffer = bytearray()

    def read_line(self, deadline):
        while True:
            end = self.buffer.find(b"\n")
            if end >= 0:
                if end > MAX_LINE:
                    raise dosage_db.SnapshotError("received an oversized serial line")
                line = bytes(self.buffer[:end]).rstrip(b"\r")
                del self.buffer[:end + 1]
                try:
                    return line.decode("utf-8")
                except UnicodeDecodeError:
                    raise dosage_db.SnapshotError("received bytes that are not text") from None
            if len(self.buffer) > MAX_LINE:
                raise dosage_db.SnapshotError("received an oversized serial line")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise TimeoutError("timed out waiting for the Arduino")
            try:
                data = os.read(self.fd, 4096)
            except BlockingIOError:
                continue
            if not data:
                raise OSError("the Arduino disconnected")
            self.buffer.extend(data)

    def reply(self, status, transfer_id):
        data = f"{status} {transfer_id}\n".encode("ascii")
        deadline = time.monotonic() + 2
        while data:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise TimeoutError("could not send the save status to the Arduino")
            try:
                count = os.write(self.fd, data)
            except BlockingIOError:
                continue
            data = data[count:]


def wait_for_begin(connection, wait_seconds):
    """Skips anything before the next DOSAGE_BEGIN line (stale output, a
    half-read earlier transfer) and returns that line."""
    deadline = time.monotonic() + wait_seconds
    while True:
        line = connection.read_line(deadline)
        if line.startswith("DOSAGE_BEGIN "):
            return line


def receive(connection, db_path, wait_seconds):
    begin = wait_for_begin(connection, wait_seconds)
    transfer_id = begin.split(" ")[1] if begin.count(" ") >= 1 else "0"
    print("Receiving...", flush=True)
    try:
        _, row_count, _, _ = dosage_db.parse_begin(begin)
        if row_count > dosage_db.MAX_ROWS:
            raise dosage_db.SnapshotError(f"too many rows ({row_count})")
        deadline = time.monotonic() + TRANSFER_TIMEOUT
        header = connection.read_line(deadline)
        rows = []
        for _ in range(row_count):
            line = connection.read_line(deadline)
            # A frame line where a row belongs means rows were lost: fail now,
            # while the Arduino is still waiting for an answer.
            if line.startswith(("DOSAGE_END", "DOSAGE_BEGIN")):
                raise dosage_db.SnapshotError(
                    f"expected {row_count} rows, the transfer ended after {len(rows)}")
            rows.append(line)
        end = connection.read_line(deadline)
        snapshot = dosage_db.parse_snapshot(begin, header, rows, end)
        db = dosage_db.connect(str(db_path))
        try:
            result = dosage_db.import_snapshot(db, snapshot)
        finally:
            db.close()
    except (OSError, ValueError, sqlite3.Error) as error:
        # Nothing was committed. ERR tells the Arduino to keep its records.
        try:
            connection.reply("ERR", transfer_id)
        except OSError:
            pass  # The Arduino times out on its own if it has disconnected.
        raise error

    print(f"Saved to {db_path}")
    print(f"  device {snapshot.device_id}, power-up {snapshot.boot}, transfer {snapshot.transfer_id}")
    print(f"  completed entries: {result.new_entries} new, {result.corrected_entries} corrected, "
          f"{result.unchanged_entries} unchanged")
    print(f"  {result.participants} participants and {result.plans} current plans on the device")
    if result.missing_entries:
        print(f"  NOTE: {result.missing_entries} entries saved earlier from this power-up were not in "
              "this snapshot. They are kept on the laptop.")
    try:
        connection.reply("ACK", snapshot.transfer_id)
    except OSError as error:
        print(f"WARNING: saved, but could not tell the Arduino ({error}). "
              "Sync again; nothing will be duplicated.", file=sys.stderr)
        return 2
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="Arduino serial device (detected automatically if omitted)")
    parser.add_argument("--db", type=Path, default=DEFAULT_DB, help=f"SQLite database (default: {DEFAULT_DB})")
    parser.add_argument("--wait", type=int, default=60, help="seconds to wait for the A3 tap (default: 60)")
    args = parser.parse_args()
    if args.wait <= 0:
        parser.error("--wait must be positive")

    try:
        # Create the database before listening, so a bad path fails before the
        # tap. A database that is only busy may be free by the time data arrives;
        # if not, the save fails then and the Arduino is told (ERR).
        try:
            dosage_db.connect(str(args.db), timeout=1).close()
        except sqlite3.OperationalError as error:
            if "locked" not in str(error):
                raise
            print(f"WARNING: {args.db} is in use by another program; will try again when the "
                  "snapshot arrives.", file=sys.stderr)
        port = args.port or find_port()
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            tty.setraw(fd)
            settings = termios.tcgetattr(fd)
            settings[4:6] = [termios.B115200, termios.B115200]
            termios.tcsetattr(fd, termios.TCSANOW, settings)
            # Drop anything sent before we were listening (e.g. a tap while no
            # receiver ran), so only a snapshot sent from now on can be ACKed.
            # tty.setraw() already flushes by default; this keeps it explicit.
            termios.tcflush(fd, termios.TCIFLUSH)
            print(f"Listening on {port}. Tap A3 on the Arduino main menu.", flush=True)
            return receive(SerialConnection(fd), args.db, args.wait)
        finally:
            os.close(fd)
    except dosage_db.IdentityError as error:
        print(f"Sync refused, nothing saved: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError, sqlite3.Error) as error:
        print(f"Sync failed, nothing saved: {error}. The Arduino keeps its records; try again.",
              file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())

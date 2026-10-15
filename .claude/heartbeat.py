"""Heartbeat and checklist helper for .claude/vulkan-backend-plan.md.

usage:
  python .claude/heartbeat.py                 update "Last activity" to now
  python .claude/heartbeat.py check A1 [...]  also tick the listed checklist items
  python .claude/heartbeat.py log "message"   also append a dated line to the Log section
  python .claude/heartbeat.py age             print the minutes since the last activity
"""
import datetime
import pathlib
import re
import sys

PLAN = pathlib.Path(__file__).with_name("vulkan-backend-plan.md")


def main() -> None:
    text = PLAN.read_text(encoding="utf-8")
    now = datetime.datetime.now().astimezone()
    arguments = sys.argv[1:]

    if arguments and arguments[0] == "age":
        stamp = re.search(r"^Last activity: (\S+)$", text, flags=re.M).group(1)
        minutes = (now - datetime.datetime.fromisoformat(stamp)).total_seconds() / 60
        print(f"{minutes:.0f}")
        return

    text = re.sub(r"^Last activity: \S+$", f"Last activity: {now.isoformat(timespec='seconds')}", text, flags=re.M)
    if arguments and arguments[0] == "check":
        for item in arguments[1:]:
            text, count = re.subn(rf"^- \[ \] {re.escape(item)} ", f"- [x] {item} ", text, flags=re.M)
            if count != 1:
                sys.exit(f"item {item} not found unchecked")
    if arguments and arguments[0] == "log":
        text = text.rstrip("\n") + f"\n- {now.date().isoformat()}: {' '.join(arguments[1:])}\n"
    PLAN.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()

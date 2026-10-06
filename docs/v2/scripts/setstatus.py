"""Set the Lectern status of checklist rows: python3 setstatus.py DOC 'id|status|note' ...

The status cell is the first cell after the feature name that starts with
✅ / 🟡 / ⬜ (as extract_items.py reads it). `note` (optional) replaces the
row's last cell; for ✅ rows the priority cell becomes "–".
"""
import re
import sys


def set_status(path, updates):
    lines = open(path, encoding="utf-8").read().split("\n")
    done = set()
    for i, line in enumerate(lines):
        m = re.match(r"\| (\d+(?:\.\d+)+) \|", line)
        if not m or m.group(1) not in updates:
            continue
        status, note = updates[m.group(1)]
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        idx = next((k for k in range(2, len(cells)) if cells[k][:1] in "✅🟡⬜🔨"), None)
        if idx is None:
            continue
        cells[idx] = status
        if status.startswith("✅"):
            for k in range(idx + 1, len(cells)):
                if re.match(r"P[0-3]", cells[k]):
                    cells[k] = "–"
                    break
        if note is not None and len(cells) - 1 > idx:
            cells[-1] = note
        lines[i] = "| " + " | ".join(cells) + " |"
        done.add(m.group(1))
    open(path, "w", encoding="utf-8").write("\n".join(lines))
    missing = set(updates) - done
    if missing:
        print(path, "rows not found:", sorted(missing))
    return done


if __name__ == "__main__":
    doc = sys.argv[1]
    ups = {}
    for arg in sys.argv[2:]:
        parts = arg.split("|", 2)
        ups[parts[0]] = (parts[1], parts[2] if len(parts) > 2 and parts[2] != "" else None)  # empty: keep the note
    print(len(set_status(doc, ups)), "rows updated in", doc)

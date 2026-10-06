#!/usr/bin/env python3
"""Regenerates docs/v2/MASTER_PHASE_PLAN.md from the v2 checklists.

Run from docs/v2:  python3 scripts/build_master_plan.py
Steps: extract every checklist row -> assign each open row to one phase
(assign_phases.py holds the phase catalogue and mapping rules) -> render.
"""
import os, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
v2 = os.path.dirname(here)
os.chdir(v2)
with tempfile.TemporaryDirectory() as tmp:
    items, phased = os.path.join(tmp, "items.json"), os.path.join(tmp, "phased.json")
    for script, args in (("extract_items.py", [items]),
                         ("assign_phases.py", [items, phased]),
                         ("render_plan.py", [phased, os.path.join(v2, "MASTER_PHASE_PLAN.md")])):
        subprocess.run([sys.executable, os.path.join(here, script), *args], check=True,
                       stdout=subprocess.DEVNULL if script == "extract_items.py" else None)

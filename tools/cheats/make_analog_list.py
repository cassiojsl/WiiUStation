#!/usr/bin/env python3
"""Build content/analog_ids.txt: the game IDs that support the DualShock's
analog sticks, from libretro-database's metadat/analog (see make_index.py
for how to check it out)."""
import os, re, sys
db = sys.argv[1]
out = os.path.join(os.path.dirname(__file__), "..", "..", "content", "analog_ids.txt")
dat = open(os.path.join(db, "metadat", "analog", "Sony - PlayStation.dat"), encoding="utf-8").read()
ids = sorted(set(re.findall(r'analog 1\s+rom \( serial "([A-Z]{4}-\d{5})"', dat)))
open(out, "w").write("\n".join(ids) + "\n")
print(len(ids), "->", out)

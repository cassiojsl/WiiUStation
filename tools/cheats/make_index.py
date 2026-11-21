#!/usr/bin/env python3
"""Build content/cheats_index.txt: "<GAME-ID>|<file name in the libretro cheat
database>" so the app can download the cheats of a game by its ID.

Needs a checkout of github.com/libretro/libretro-database with
"metadat/Sony - PlayStation.dat" (game names and serials) and
"cht/Sony - PlayStation/" (the cheat files):

  git clone --depth 1 --filter=blob:none --sparse https://github.com/libretro/libretro-database db
  git -C db sparse-checkout set "cht/Sony - PlayStation" metadat
  python3 tools/cheats/make_index.py db
"""
import os, re, sys, difflib

db = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "..", "content", "cheats_index.txt")
chtdir = os.path.join(db, "cht", "Sony - PlayStation")
dat = open(os.path.join(db, "metadat", "Sony - PlayStation.dat"), encoding="utf-8", errors="replace").read()

DEVICES = ("GameShark", "Game Buster", "Action Replay", "Xploder", "Pro Action Replay", "Code Breaker", "Caetla")
def split_cht(fn):
    """'Name (USA) (Disc 1) (GameShark).cht' -> ('Name (USA) (Disc 1)', 'GameShark')"""
    base = fn[:-4]
    m = re.match(r"^(.*) \(([^()]*)\)$", base)
    if m and any(m.group(2).startswith(d) for d in DEVICES):
        return m.group(1), m.group(2)
    return base, ""

chts = {}
for fn in os.listdir(chtdir):
    if not fn.endswith(".cht"): continue
    name, dev = split_cht(fn)
    # prefer GameShark when a game has several
    if name not in chts or dev.startswith("GameShark"):
        chts[name] = fn[:-4]

def title_region(n):
    m = re.match(r"^(.*?)\s*(\(.*)$", n)
    return (m.group(1), m.group(2)) if m else (n, "")
def norm(t):
    return re.sub(r"[^a-z0-9 ]", "", t.lower().replace("&", "and")).split()
def nums(t):
    return set(re.findall(r"\b\d+\b", t)) | set(w for w in norm(t) if w in ("ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x"))

by_region = {}
for name in chts:
    t, r = title_region(name)
    by_region.setdefault(r, []).append((t, name))

def regions(r):
    m = re.match(r"^\(([^()]*)\)", r)
    return set(x.strip() for x in m.group(1).split(",")) if m else set()

def candidates(gname):
    """the names a cheat file may use for this game"""
    c = [gname]
    d = re.sub(r"\(Disc (\d+) of \d+\)", r"(Disc \1)", gname)
    c.append(d)
    c.append(re.sub(r"\(Disc \d+\)", "(Disc 1)", d))             # later discs: disc 1's codes
    c.append(re.sub(r"\s*\(Disc \d+( of \d+)?\)", "", gname))     # files without a disc tag
    return [x for i, x in enumerate(c) if x not in c[:i]]

index, exact, fuzzy = {}, 0, 0
for m in re.finditer(r'game \(\s*name "([^"]+)"(.*?)\n\)', dat, re.S):
    gname, body = m.group(1), m.group(2)
    serials = set(re.findall(r'serial "([A-Z]{4}-\d{5})', body))
    if not serials: continue
    target = None
    for cand in candidates(gname):
        if cand in chts: target = chts[cand]; exact += 1; break
    if not target:
        t, r = title_region(re.sub(r"\s*\(Disc \d+( of \d+)?\)", "", gname))
        rg = regions(r)
        best, bscore = None, 0
        for rr, lst in by_region.items():
            if not (regions(rr) & rg): continue                # same region
            for ct, cname in lst:
                if nums(ct) != nums(t): continue               # same numbers: never another sequel's
                s = difflib.SequenceMatcher(None, " ".join(norm(t)), " ".join(norm(ct))).ratio()
                # keep extra tags (Disc, Rev, Demo...) out of the race when possible
                if "Demo" in cname or "Sampler" in cname: s -= 0.2
                if s > bscore: best, bscore = cname, s
        if best and bscore >= 0.88:
            target = chts[best]; fuzzy += 1
    if target:
        for s in serials:
            index.setdefault(s, target)

# second source: dat/ps1.idlst (ID "title", no region): the region comes from the ID
REG = {"SLUS": "USA", "SCUS": "USA", "SLES": "Europe", "SCES": "Europe", "SLPS": "Japan",
       "SCPS": "Japan", "SLPM": "Japan", "SCPM": "Japan"}
idl = os.path.join(db, "dat", "ps1.idlst")
extra = 0
if os.path.exists(idl):
    for line in open(idl, encoding="utf-8", errors="replace"):
        m = re.match(r'^([A-Z]{4}-\d{5}) "(.*)"', line.strip())
        if not m or m.group(1) in index: continue
        sid, title = m.groups()
        reg = REG.get(sid[:4])
        if not reg: continue
        best, bscore = None, 0
        for rr, lst in by_region.items():
            if reg not in regions(rr): continue
            for ct, cname in lst:
                if nums(ct) != nums(title): continue
                sc = difflib.SequenceMatcher(None, " ".join(norm(title)), " ".join(norm(ct))).ratio()
                if "Demo" in cname or "Sampler" in cname: sc -= 0.2
                if "(Disc" in cname and "(Disc 1)" not in cname: sc -= 0.05
                if sc > bscore: best, bscore = cname, sc
        if best and bscore >= 0.92:
            index[sid] = chts[best]; extra += 1
print("from ps1.idlst", extra)

with open(out, "w", encoding="utf-8") as f:
    f.write("# GAME-ID|cheat file in github.com/libretro/libretro-database cht/Sony - PlayStation (made by tools/cheats/make_index.py)\n")
    for s in sorted(index):
        f.write("%s|%s\n" % (s, index[s]))
print("ids", len(index), "exact", exact, "fuzzy", fuzzy, "->", out)

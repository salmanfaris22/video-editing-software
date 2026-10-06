import json,sys,collections,re
d=json.load(open(sys.argv[1])); P=collections.OrderedDict(d['phases']); out=d['out']; done=d['done']
REMAP={'2.2':'2.3','2.3':'2.6','2.4':'2.7','2.5':'2.8','2.6':'2.9'}
def rp(k):
    base=k.rstrip('ab'); suf=k[len(base):]
    return REMAP.get(base,base)+suf
out2=collections.defaultdict(list)
for k,v in out.items(): out2[rp(k)].extend(v)
P2=collections.OrderedDict()
for k,v in P.items(): P2[REMAP.get(k,k)]=v
NEW={'2.1':('MCP core','JSON-RPC server, lectern-mcp bridge, local endpoint with token, consent settings, activity log (AI_ASSISTANTS_MCP §2, §5)'),
 '2.2':('All features as MCP tools','~35 tools with schemas and undo labels; from now on no feature ships without its tool (AI_ASSISTANTS_MCP §3)'),
 '2.3':('Agent perception','frames, contact sheets, scopes, loudness, transcript, shot and audio analysis as MCP tools (ROADMAP_PHASES 2.3)'),
 '2.4':('Edit plans','propose → preview → apply as one undo group (ROADMAP_PHASES 2.4)'),
 '2.5':('Cinematic color','cinematic_grade styles built from Phase 1 tools, skin-tone protection, explain_grade (ROADMAP_PHASES 2.5)'),
 '2.10':('Self-review','review_edit checklist: flash frames, jump cuts, clipping, skin tone, loudness, safe areas (ROADMAP_PHASES 2.10)'),
 '2.11':('One-click Pro edit (optional)','In-app assistant with the user\'s model provider, using the same tools (ROADMAP_PHASES 2.11)'),
 '2.12':('Quality benchmark','20 reference recordings, scores per release (ROADMAP_PHASES 2.12)')}
for k,v in NEW.items(): P2[k]=v
P2['2.1']=NEW['2.1']; P2['2.2']=NEW['2.2']
# Claude token model (rough; see the methodology section in the output).
PER_ITEM={'P0':0.6,'P1':0.5,'P2':0.4,'P3':0.8,'-':0.5}   # million tokens processed per checklist item
NEW_WORK={'2.1':5,'2.2':5,'2.3':5,'2.4':2.5,'2.5':5,'2.10':2.5,'2.11':5,'2.12':2.5}  # million, by size S=2.5 M=5
# Size floors from ROADMAP_PHASES.md (S/M/L) for the "a" part of a phase:
# foundation work (e.g. the GPU renderer) is large even with few checklist rows.
SIZE={'S':2.5,'M':5,'L':12}
FLOOR={'1.1':'L','1.2':'M','1.3':'M','1.4':'M','1.5':'M','1.6':'S','1.7':'M','1.8':'M','1.9':'M','1.10':'M',
       '1.11':'S','1.12':'M','1.13':'M','1.14':'M','2.6':'M','2.7':'M','2.8':'M','2.9':'L'}
def est(k,rows):
    base=k.rstrip('ab')
    if not rows: return NEW_WORK.get(base,0)
    e=sum(PER_ITEM.get(i['prio'],0.5) for i in rows)
    if not k.endswith('b') and base in FLOOR: e=max(e,SIZE[FLOOR[base]])
    return e
def fmt(m): return f'{m:.1f}M' if m<10 else f'{m:.0f}M'
DOC={'COLOR':'COLOR_EFFECTS_PARITY','UI':'PRO_INTERFACE_PARITY','DR':'DAVINCI_COLOR_PAGE','AUDIT':'FULL_GAP_AUDIT'}
SHORT={'COLOR':'COLOR','UI':'UI','DR':'DAVINCI','AUDIT':'AUDIT'}
def key(k):
    base=k.rstrip('ab'); a,b=base.split('.'); return (int(a),int(b),k[len(base):])
def stage(k): return int(k.split('.')[0])
STAGES={0:'Stage 0 — Ship, privacy and recorder (parallel track)',1:'Stage 1 — Color grading like DaVinci (ROADMAP Phase 1)',2:'Stage 2 — Deep MCP and the pro-editor agent (ROADMAP Phase 2)',3:'Stage 3 — Effects, compositing, animation, text',4:'Stage 4 — Editing, audio, captions',5:'Stage 5 — Pro workspace UI',6:'Stage 6 — Delivery and collaboration',7:'Stage 7 — Project, performance, platform',8:'Stage 8 — Specialist (P3, on demand)'}
def esc(t): return t.replace('|','/')
def table(rows):
    r=['| Ref | Feature | Now | P |','|---|---|---|---|']
    for i in sorted(rows,key=lambda x:(x['doc'],x['sec'],[int(n) for n in x['id'].split('.')])):
        r.append(f"| {SHORT[i['doc']]} §{i['id']} | {esc(i['name'])} | {i['status']} | {i['prio']} |")
    return '\n'.join(r)
L=[]; count=0; subcount=0; stage_tok={}
phase_keys=sorted(set(list(out2.keys())+[k for k in NEW]),key=key)
toc=[]
cur=None
body=[]
for k in phase_keys:
    st=stage(k)
    if st!=cur:
        cur=st; body.append(f'\n---\n\n## {STAGES[st]}\n'); toc.append(f'\n**{STAGES[st]}**\n')
    base=k.rstrip('ab'); suf=k[len(base):]
    title,goal=P2.get(base,('?','?'))
    tag={'a':'must-have (P0/P1)','b':'power users (P2)','':'new work' if base in NEW else 'specialist (P3)'}[suf]
    rows=out2.get(k,[])
    cnt=(f'{len(rows)} item'+('s' if len(rows)!=1 else '')) if rows else 'no checklist rows'
    e=est(k,rows); stage_tok[st]=stage_tok.get(st,0)+e
    toc.append(f'- **{k}** {title} · {tag} · {cnt} · ~{fmt(e)} tokens')
    body.append(f'### {k} — {title} · {tag}\n')
    body.append(f'Goal: {goal}.  ')
    body.append(f'Claude estimate: ~{fmt(est(k,rows))} tokens processed (range ×0.5–×2; output ≈ 5 %).  ')
    if base in NEW and not rows:
        body.append('No checklist rows — new work defined in ROADMAP_PHASES.md / AI_ASSISTANTS_MCP.md.\n'); continue
    body.append(f'Items: {len(rows)}. User-facing features ship with their MCP tool and a test.\n')
    count+=len(rows)
    groups=collections.OrderedDict()
    for i in sorted(rows,key=lambda x:(x['doc'],x['sec'])):
        groups.setdefault((i['doc'],i['sec'],i['title']),[]).append(i)
    big=[g for g in groups.items() if len(g[1])>=3]
    if len(rows)>14 and len(big)>=2:
        n=0; rest=[]
        for (dd,ss,tt),g in groups.items():
            if len(g)<3: rest.extend(g); continue
            n+=1; subcount+=1
            body.append(f'#### {k}.{n} — {re.sub(r"[*()]","",tt).strip()} ({len(g)})\n')
            body.append(table(g)+'\n')
        if rest:
            n+=1; subcount+=1
            body.append(f'#### {k}.{n} — Other items ({len(rest)})\n')
            body.append(table(rest)+'\n')
    else:
        body.append(table(rows)+'\n')
# done appendix
dn=collections.defaultdict(list)
for i in done: dn[i['doc']].append(i)
app=['\n---\n\n## Appendix — already done (✅)\n']
for dd in ['COLOR','UI','DR','AUDIT']:
    app.append(f'**{DOC[dd]}:** '+', '.join(f"§{i['id']} {esc(i['name'])}" for i in dn[dd])+'\n')
hdr=f'''# Master Phase Plan — every feature, phase by phase

Every open item from the v2 checklists (COLOR_EFFECTS_PARITY,
PRO_INTERFACE_PARITY, DAVINCI_COLOR_PAGE, FULL_GAP_AUDIT) placed into one
small phase. Generated 2026-10-05 from those documents by `scripts/` in this folder
(`python3 scripts/build_master_plan.py`), so
**each of the {count} open items (⬜ missing or 🟡 partly done) appears exactly
once**; the {len(done)} done items (✅) are listed in the appendix.

How it is organised:
- **Stages 0–8** follow the order of work: ship basics in parallel; color
  first (ROADMAP Phase 1); deep MCP and the AI editor second (ROADMAP
  Phase 2); then effects, editing/audio, pro UI, delivery, platform;
  specialist work last.
- Each phase has an **a** part (must-have, P0/P1) and a **b** part (power
  users, P2). Build all **a** phases first, then the **b** phases. Stage 8
  holds every P3 item.
- Large phases are cut into sub-phases (e.g. 1.4a.1, 1.4a.2) by topic.
- The same feature can appear in several checklists (e.g. auto reframe in
  COLOR, AUDIT and the AI list); such rows sit in the same phase — build it
  once and tick every row.
- Phase numbers in Stages 1–2 match ROADMAP_PHASES.md. Phases marked
  "new work" have no checklist rows (MCP, edit plans, self-review, …).
- Ref column: COLOR = COLOR_EFFECTS_PARITY §, UI = PRO_INTERFACE_PARITY §,
  DAVINCI = DAVINCI_COLOR_PAGE §, AUDIT = FULL_GAP_AUDIT §.
- Same rules for every phase: usable when merged, MCP tool + test, golden
  images / QML tests where relevant, undoable, meets the M1 8 GB budget,
  and the checklist rows are updated (⬜ → 🟡 → ✅).

## Order of work

```mermaid
flowchart LR
    S0["Stage 0 ship, privacy, recorder (parallel)"]
    W1["Wave 1: all a phases, P0/P1"]
    W2["Wave 2: all b phases, P2"]
    W3["Wave 3: Stage 8 specialist, P3"]
    S1["Stage 1 color"] --> S2["Stage 2 MCP and agent"] --> S3["Stage 3 effects"] --> S4["Stage 4 editing and audio"] --> S5["Stage 5 pro UI"] --> S6["Stage 6-7 delivery and platform"]
    W1 --> W2 --> W3
    S0 -.-> W1
    S1 -.-> W1
```

Stage 2 starts as soon as phase 1.3 is done (project format stable) and
then runs beside Stage 1.

'''
tot=sum(stage_tok.values())
tokens=['\n## Claude token estimate\n',
 'Rough budget for building each phase with Claude Code (implement, compile, test, fix), counted as **tokens processed**: input (mostly the same project context re-read on every step, which providers bill at a much lower cached rate) plus output. Output tokens are about 5 % of the total.\n',
 'Model: P0 item ≈ 0.6M, P1 ≈ 0.5M, P2 ≈ 0.4M, P3 ≈ 0.8M (specialist work is harder); the **a** part of a Stage 1–2 phase is at least its ROADMAP_PHASES size (S ≈ 2.5M, M ≈ 5M, L ≈ 12M), because foundation work is large even with few checklist rows; "new work" phases by size. Treat every number as ×0.5–×2: it depends on how much code a feature touches, how many test/fix rounds it needs, and the model used. Re-measure after the first phases and update `PER_ITEM` in `scripts/render_plan.py`.\n',
 '| Stage | Tokens processed (est.) | Output tokens (≈ 5 %) |','|---|---|---|']
for st in sorted(stage_tok): tokens.append(f'| {STAGES[st]} | ~{fmt(stage_tok[st])} | ~{fmt(stage_tok[st]*0.05)} |')
tokens.append(f'| **All stages** | **~{fmt(tot)}** (≈ {fmt(tot*0.5)}–{fmt(tot*2)}) | **~{fmt(tot*0.05)}** |')
wave_a=sum(est(k,out2.get(k,[])) for k in phase_keys if k.endswith('a') or (k.rstrip('ab') in NEW and not k.endswith('b')))
tokens.append(f'\nWave 1 only (all **a** phases + new-work phases): ~{fmt(wave_a)} tokens processed.\n')
txt=hdr+'\n'.join(tokens)+'\n## All phases (table of contents)\n'+'\n'.join(toc)+'\n'+'\n'.join(body)+'\n'.join(app)
txt+=f'\n---\n\nTotals: {count} open items in {len([k for k in phase_keys if out2.get(k)])} phases with items ({subcount} sub-phases) plus {len(NEW)} new-work phases; {len(done)} items done.\n'
open(sys.argv[2],'w').write(txt)
print(count,len(phase_keys),subcount)

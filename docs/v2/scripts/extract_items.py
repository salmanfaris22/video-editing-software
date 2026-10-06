import re,json,sys
docs={'COLOR_EFFECTS_PARITY.md':'COLOR','PRO_INTERFACE_PARITY.md':'UI','DAVINCI_COLOR_PAGE.md':'DR','FULL_GAP_AUDIT.md':'AUDIT'}
items=[]
for f,tag in docs.items():
    sec=None; title=''
    for line in open(f):
        m=re.match(r'## (\d+)\. (.*)',line)
        if m: sec=int(m.group(1)); title=m.group(2).strip(); continue
        m=re.match(r'### (\d+\.\d+) (.*)',line)
        if not (sec and re.match(r'\| \d+\.\d+ \|',line)): continue
        cells=[x.strip() for x in line.strip().strip('|').split('|')]
        st=[x for x in cells[2:] if x and x[0] in '✅🟡⬜']
        if not st: continue
        if tag=='AUDIT' and sec==12: continue
        pr=[x for x in cells if re.match(r'P[0-3]',x)]
        items.append(dict(doc=tag,sec=sec,title=title,id=cells[0],name=cells[1],status=st[0][0],prio=(pr[0][:2] if pr else '-'),note=cells[-1] if not re.match(r'P[0-3]|–',cells[-1]) else ''))
json.dump(items,open(sys.argv[1],'w'),ensure_ascii=False,indent=0)
from collections import Counter
print(len(items),Counter(i['status'] for i in items))
print(Counter((i['doc'],i['sec'],i['title'][:30]) for i in items).most_common(80))

import csv
def counts(path):
    rows=list(csv.DictReader(open(path),delimiter='\t'))
    vcols=[c for c in rows[0] if c.startswith('verdict_')]
    pc={c:0 for c in vcols}; fc={c:0 for c in vcols}
    for r in rows:
        for c in vcols:
            v=(r.get(c) or '').strip()
            if v=='PASS': pc[c]+=1
            elif v=='FAIL': fc[c]+=1
    return len(rows), vcols, pc, fc
base='/home/user/src/willeq-wt/kb-b1178-oracle-refs/refactor/B1153/tables/'
for name in ['gate_a','gate_b','gate_c']:
    rows,vcols,pc,fc=counts(base+name+'.tsv')
    tot_p=sum(pc.values()); tot_f=sum(fc.values())
    print(f"=== {name}: {rows} rows, {len(vcols)} verdict cols; BEFORE PASS={tot_p} FAIL={tot_f}")
    for c in vcols:
        print(f"    {c}: P={pc[c]} F={fc[c]}")

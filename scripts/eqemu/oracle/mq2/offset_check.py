import re,sys
hdr,dec=sys.argv[1],sys.argv[2]
funcs=set(); refs=set()
for line in open(dec,errors='replace'):
    m=re.match(r'// FUNC ([0-9a-f]{8})',line)
    if m: funcs.add(int(m.group(1),16))
    for m in re.finditer(r'(?:DAT|PTR|FUN|LAB|s_\w*?|PTR_\w*?)_00([0-9a-f]{6})',line): refs.add(int(m.group(1),16))
tot={'text':[0,0],'data':[0,0]}
miss=[]
for line in open(hdr,errors='replace'):
    m=re.match(r'#define\s+(\w+)\s+0x([0-9A-Fa-f]+)',line)
    if not m: continue
    name,v=m.group(1),int(m.group(2),16)
    if v<0x400000: continue
    if v<0x63f000:
        tot['text'][0]+=1
        if v in funcs: tot['text'][1]+=1
        else: miss.append(('text',name,hex(v)))
    else:
        tot['data'][0]+=1
        if v in refs: tot['data'][1]+=1
        else: miss.append(('data',name,hex(v)))
print(tot); print(len(miss),'not matched'); [print(*x) for x in miss[:60]]

import json, collections
doc = json.load(open("config/spell_effects.json"), object_pairs_hook=collections.OrderedDict)
print("top:", list(doc.keys()))
for k in ("sourceRecords","targetRecords","animRecords"):
    if k in doc:
        d = doc[k]
        print(k, "->", {kk: (type(vv).__name__ if not isinstance(vv,(str,int,float,bool)) else vv) for kk,vv in d.items()})
if "spellOverrides" in doc:
    print("spellOverrides:", {kk: sorted(vv.keys()) for kk,vv in doc["spellOverrides"].items()})
if "sourceRecords" in doc and "records" in doc["sourceRecords"]:
    r = doc["sourceRecords"]["records"]
    print("src record ids:", sorted(r.keys(), key=int))
    print("sample row 0:", json.dumps(r.get("0", {})))
if "targetRecords" in doc and "records" in doc["targetRecords"]:
    print("tgt record ids:", sorted(doc["targetRecords"]["records"].keys(), key=int))
    print("sample tgt row 5:", json.dumps(doc["targetRecords"]["records"].get("5", {})))

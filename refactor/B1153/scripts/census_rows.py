import json
doc = json.load(open("config/spell_effects.json"))
sr = doc["sourceRecords"]["records"]
tr = doc["targetRecords"]["records"]
print("record\tsrcAnchor\ttgtAnchor\ttgtExtra")
for rid in sorted(sr, key=int):
    s, t = sr[rid], tr[rid]
    extra = {k: v for k, v in t.items() if k not in ("anchor", "capture")}
    print(f"{rid}\t{s.get('anchor')}\t{t.get('anchor')}\t{json.dumps(extra) if extra else ''}")
print()
print("animRecords:", json.dumps(doc["animRecords"]["rows"], sort_keys=True))
print()
print("scale:", json.dumps(doc["sourceRecords"]["scale"]))
print("src default:", json.dumps(doc["sourceRecords"]["default"]))
print("src defaultWhy:", doc["sourceRecords"]["defaultWhy"])
print("tgt default:", json.dumps(doc["targetRecords"]["default"]))
print("tgt defaultWhy:", doc["targetRecords"]["defaultWhy"])
print()
for sp, styles in sorted(doc["spellOverrides"].items(), key=lambda kv: int(kv[0])):
    print(f"override {sp}:")
    for st, vals in styles.items():
        print(f"  {st}: {json.dumps(vals)}")

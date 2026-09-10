"""Check catalog parity, ambiguous keys, placeholders and statically used UI keys."""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read_locale(path):
    result, folded = {}, set()
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if not line.strip() or line.startswith(("#", ";")):
            continue
        key, value = line.split("=", 1)
        assert key.casefold() not in folded, f"{path.name}:{number}: ambiguous key {key}"
        folded.add(key.casefold())
        result[key] = json.loads(value)
    return result


en = read_locale(ROOT / "data/locale/en-US.ini")
es = read_locale(ROOT / "data/locale/es-ES.ini")
assert en.keys() == es.keys(), f"Locale key mismatch: {en.keys() ^ es.keys()}"
for key in en:
    assert sorted(re.findall(r"%[1-9]", en[key])) == sorted(re.findall(r"%[1-9]", es[key])), key

catalog = json.loads((ROOT / "data/messages.json").read_text(encoding="utf-8"))
assert len({item["source"] for item in catalog}) == len(catalog), "Duplicate message pattern"
for item in catalog:
    assert en[item["key"]] == item["source"], item["key"]

ui = (ROOT / "src/ui.cpp").read_text(encoding="utf-8")
keys = re.findall(r'bs::tr\("([^"]+)"\)|\bpage\("([^"]+)"\)|\bbutton\([^,\n]+,\s*"([^"]+)"', ui)
missing = {next(k for k in match if k) for match in keys} - en.keys()
assert not missing, f"Missing static UI translations: {missing}"
print(f"Locale parity and placeholders: {len(en)} keys; {len(catalog)} diagnostic templates")

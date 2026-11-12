"""Inventario de tarjetas de ordenar codigo: lectura sin modificar el curriculo."""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
CONTENT=ROOT/"app/src/main/assets/content"


def audit(root: Path=CONTENT):
    findings=[]
    by_locale=Counter()
    by_course=Counter()
    puzzles=0
    cards=0
    for path in sorted(root.glob("**/lesson_*.json")):
        rel=path.relative_to(root)
        if not rel.parts or rel.parts[0] not in ("es","en","fr"):
            continue
        document=json.loads(path.read_text(encoding="utf-8-sig"))
        state=document.get("stateLayout",{})
        current=state.get("current",[]) if isinstance(state,dict) else []
        for p,page in enumerate(document.get("pages",[])):
            for b,block in enumerate(page.get("blocks",[])):
                if block.get("type")!="order_code":
                    continue
                puzzles+=1
                fragments=block.get("fragments",[])
                cards+=len(fragments) if isinstance(fragments,list) else 0
                for i,value in enumerate(fragments if isinstance(fragments,list) else []):
                    if not isinstance(value,str) or ("\n" not in value and "\r" not in value):
                        continue
                    locale=rel.parts[0]
                    course=rel.parts[2] if len(rel.parts)>2 else ""
                    by_locale[locale]+=1
                    by_course[course]+=1
                    identity=(current[p]["blocks"][b]
                              if p<len(current) and b<len(current[p].get("blocks",[]))
                              else block.get("id"))
                    findings.append({"lesson":rel.as_posix(),"page":p+1,"block":b+1,
                                     "identity":identity,"fragment":i,
                                     "physicalLines":len(value.splitlines()),
                                     "refinementRequired":True})
    return {"readOnly":True,"puzzles":puzzles,"cards":cards,
            "multilineCards":len(findings),"byLocale":dict(by_locale),
            "byCourse":dict(by_course),"findings":findings}


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--output-json",type=Path)
    args=parser.parse_args()
    result=audit()
    print(f"Puzles: {result['puzzles']}; tarjetas: {result['cards']}; "
          f"tarjetas con varias lineas: {result['multilineCards']}")
    for issue in result["findings"][:40]:
        print(f"{issue['lesson']} p{issue['page']} b{issue['block']} "
              f"fragmento {issue['fragment']}: {issue['physicalLines']} lineas")
    if args.output_json:
        args.output_json.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",
                                    encoding="utf-8")
    return 0


if __name__=="__main__":
    raise SystemExit(main())

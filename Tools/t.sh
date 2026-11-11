#!/bin/bash
# helpers for driving the editor: t.sh pie | stop | shot <name> | log [pattern] | snap [depth]
S="$(cd "$(dirname "$0")/.." && pwd)"; cd "$S/tools"
SI=SlateInspectorToolset.SlateInspectorToolset
case "$1" in
  pie)  python3 ue.py EditorToolset.EditorAppToolset StartPIE '{"options":{"bSimulate":false,"playMode":"PlayMode_InViewPort","warmupSeconds":4}}' ;;
  stop) python3 ue.py EditorToolset.EditorAppToolset StopPIE '{}' ;;
  shot) python3 ue.py $SI Screenshot '{"ref":""}' "$S/shot_$2.png" ;;
  log)  python3 ue.py EditorToolset.LogsToolset GetLogEntries "{\"category\":\"${3:-LogIHPCAssistant}\",\"pattern\":\"${2:-}\",\"maxEntries\":${4:-25}}" ;;
  snap) python3 ue.py $SI Snapshot "{\"ref\":\"${3:-}\",\"maxDepth\":${2:-40},\"bIncludeSourceLocations\":false}" ;;
  click) python3 ue.py $SI Click "{\"ref\":\"$2\",\"button\":\"${3:-left}\",\"doubleClick\":false,\"modifiers\":[]}" ;;
  type) python3 ue.py $SI Type "{\"ref\":\"$2\",\"text\":\"$3\",\"submit\":true}" ;;
esac
case "$1" in
  conref) # find the editor's console command box (the text box at the bottom of the window)
    "$0" snap 60 > "$S/snap_con.txt" 2>&1
    python3 - "$S/snap_con.txt" <<'PY' > "$S/conref.txt"
import re,sys
best=None
for line in open(sys.argv[1]):
    m=re.search(r'textbox.*\[pos=(\d+),(\d+) size=(\d+),(\d+)\] \[ref=(\w+)\]', line)
    if m and (best is None or int(m.group(2))>best[0]): best=(int(m.group(2)), m.group(5))
print(best[1] if best else "")
PY
    cat "$S/conref.txt" ;;
  con) python3 ue.py $SI Type "{\"ref\":\"$(cat "$S/conref.txt")\",\"text\":\"$2\",\"submit\":true}" >/dev/null ;;
  alog) python3 ue.py EditorToolset.LogsToolset GetLogEntries "{\"category\":\"LogIHPCAssistant\",\"pattern\":\"${2:-}\",\"maxEntries\":${3:-25}}" | python3 -c "
import sys,json,re
d=json.loads(sys.stdin.read())
for e in (d.get('returnValue') or d):
    s=e if isinstance(e,str) else json.dumps(e)
    print(re.sub(r'^\[[^\]]*\]\[[^\]]*\]LogIHPCAssistant: ','',s))" ;;
  crop) sips -c ${4:-1700} ${5:-2900} --cropOffset ${6:-270} ${7:-0} "$S/shot_$2.png" --out "$S/crop_$2.png" >/dev/null; sips -Z ${3:-700} "$S/crop_$2.png" >/dev/null ;;
esac

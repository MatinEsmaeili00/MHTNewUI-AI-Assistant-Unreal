#!/bin/bash
# stop PIE, close the editor, build, relaunch, wait for MCP. Prints the build result.
S="$(cd "$(dirname "$0")/.." && pwd)"; PRJ="/Users/mesmae6/Perforce/maclab_digitalhuman_main3/new copy/MHTNewUI"
"$S/tools/t.sh" stop >/dev/null 2>&1; sleep 1
osascript -e 'tell application "UnrealEditor" to quit' >/dev/null 2>&1
for i in $(seq 1 30); do pgrep -x UnrealEditor >/dev/null || break; sleep 2; done
pgrep -x UnrealEditor >/dev/null && { echo "EDITOR STILL OPEN"; exit 1; }
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" MetaHumanTestEditor Mac Development -Project="$PRJ/MetaHumanTest.uproject" -WaitMutex > "$S/build.log" 2>&1
grep -E "error|Result:|Total execution" "$S/build.log" | head -40
grep -q "Result: Succeeded" "$S/build.log" || exit 2
open -n -a "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app" --args "$PRJ/MetaHumanTest.uproject"
for i in $(seq 1 80); do curl -s -o /dev/null -m 2 http://127.0.0.1:8002/mcp && { echo "editor up"; break; }; sleep 3; done
sleep 4

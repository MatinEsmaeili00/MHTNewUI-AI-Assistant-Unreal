// mouse.swift <x> <y> <holdSeconds> [button: left|right]
// Presses the mouse at (x, y) - pixels of the Unreal editor window's screenshot (2 px per point) - holds, releases.
import Cocoa
let args = CommandLine.arguments
guard args.count >= 4, let px = Double(args[1]), let py = Double(args[2]), let hold = Double(args[3]) else { print("usage"); exit(1) }
print("trusted for accessibility:", AXIsProcessTrusted())
let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as! [[String: Any]]
var best: CGRect? = nil
for w in list where (w[kCGWindowOwnerName as String] as? String) == "UnrealEditor" {
    if let b = w[kCGWindowBounds as String] as? [String: CGFloat] {
        let r = CGRect(x: b["X"]!, y: b["Y"]!, width: b["Width"]!, height: b["Height"]!)
        if best == nil || r.width * r.height > best!.width * best!.height { best = r }
    }
}
guard let win = best else { print("no UnrealEditor window"); exit(2) }
let scale = NSScreen.main?.backingScaleFactor ?? 2
let p = CGPoint(x: win.origin.x + px / scale, y: win.origin.y + py / scale)
print("window", win, "scale", scale, "-> point", p)
func post(_ type: CGEventType) {
    let e = CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: p, mouseButton: .left)
    e?.post(tap: .cghidEventTap)
}
post(.mouseMoved); usleep(150_000)
post(.leftMouseDown); usleep(useconds_t(hold * 1_000_000))
post(.leftMouseUp)
print("done")

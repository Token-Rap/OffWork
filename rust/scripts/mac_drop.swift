// Drag a file URL from a native AppKit drag source onto a screen point, using real pointer events.
import AppKit

let args = CommandLine.arguments
let file = URL(fileURLWithPath: args[1])
let target = CGPoint(x: Double(args[2])!, y: Double(args[3])!)

final class Source: NSView, NSDraggingSource {
    func draggingSession(_ s: NSDraggingSession, sourceOperationMaskFor c: NSDraggingContext) -> NSDragOperation { .copy }
    override func mouseDown(with event: NSEvent) {
        let item = NSDraggingItem(pasteboardWriter: file as NSURL)
        item.setDraggingFrame(bounds, contents: NSImage(size: bounds.size))
        beginDraggingSession(with: [item], event: event, source: self)
    }
    func draggingSession(_ s: NSDraggingSession, endedAt p: NSPoint, operation: NSDragOperation) {
        print("drag ended, operation \(operation.rawValue)")
        NSApp.terminate(nil)
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let window = NSWindow(contentRect: NSRect(x: 200, y: 200, width: 160, height: 100),
                      styleMask: [.titled], backing: .buffered, defer: false)
window.contentView = Source(frame: NSRect(x: 0, y: 0, width: 160, height: 100))
window.makeKeyAndOrderFront(nil)
app.activate(ignoringOtherApps: true)

DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
    // Screen coordinates for CGEvent are top-left based.
    let screenHeight = NSScreen.screens[0].frame.height
    let f = window.frame
    let start = CGPoint(x: f.midX, y: screenHeight - f.midY + 10)
    func post(_ type: CGEventType, _ p: CGPoint) {
        CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: p, mouseButton: .left)?.post(tap: .cghidEventTap)
    }
    post(.mouseMoved, start)
    post(.leftMouseDown, start)
    DispatchQueue.global().async {
        for i in 1...30 {
            let t = Double(i) / 30
            post(.leftMouseDragged, CGPoint(x: start.x + (target.x - start.x) * t, y: start.y + (target.y - start.y) * t))
            usleep(30_000)
        }
        usleep(300_000)
        post(.leftMouseUp, target)
    }
}
DispatchQueue.main.asyncAfter(deadline: .now() + 10) { print("timeout"); exit(2) }
app.run()

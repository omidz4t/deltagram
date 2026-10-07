"""Confirm that the GUI has mapped a real window on the test display."""
import ctypes as c
import time
x = c.CDLL('/test-deps/usr/lib/x86_64-linux-gnu/libX11.so.6')
x.XOpenDisplay.restype = c.c_void_p
x.XDefaultRootWindow.argtypes = [c.c_void_p]
x.XDefaultRootWindow.restype = c.c_ulong
x.XQueryTree.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ulong)), c.POINTER(c.c_uint)]
x.XGetGeometry.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_uint), c.POINTER(c.c_uint), c.POINTER(c.c_uint), c.POINTER(c.c_uint)]
x.XFree.argtypes = [c.c_void_p]
x.XCloseDisplay.argtypes = [c.c_void_p]
display = x.XOpenDisplay(None)
assert display, 'Cannot connect to test display'
root = x.XDefaultRootWindow(display)
# The second connection needs a window-manager equivalent keyboard focus.
for attempt in range(60):
    parent, result_root = c.c_ulong(), c.c_ulong()
    children, count = c.POINTER(c.c_ulong)(), c.c_uint()
    x.XQueryTree(display, root, c.byref(result_root), c.byref(parent), c.byref(children), c.byref(count))
    found = False
    for index in range(count.value):
        px, py = c.c_int(), c.c_int()
        width, height, border, depth = (c.c_uint() for _ in range(4))
        x.XGetGeometry(display, children[index], c.byref(result_root), c.byref(px), c.byref(py), c.byref(width), c.byref(height), c.byref(border), c.byref(depth))
        if width.value >= 500 and height.value >= 300:
            print(f'GUI window: {width.value} x {height.value}')
            found = True
            application_window = children[index]
            break
    if children:
        x.XFree(children)
    if found:
        x.XCloseDisplay(display)
        break
    time.sleep(1)
else:
    raise SystemExit('No application window opened')

# Exercise actual X11 keyboard input on the profile name field, then copy it
# back through the clipboard to assert the exact text, not just window startup.
x.XGetImage.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint, c.c_ulong, c.c_int]
x.XGetImage.restype = c.c_void_p
x.XGetPixel.argtypes = [c.c_void_p, c.c_int, c.c_int]
x.XGetPixel.restype = c.c_ulong
x.XDestroyImage.argtypes = [c.c_void_p]
x.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
x.XKeysymToKeycode.restype = c.c_uint
x.XFlush.argtypes = [c.c_void_p]
x.XInternAtom.argtypes = [c.c_void_p, c.c_char_p, c.c_int]
x.XInternAtom.restype = c.c_ulong
x.XCreateSimpleWindow.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint, c.c_uint, c.c_ulong, c.c_ulong]
x.XCreateSimpleWindow.restype = c.c_ulong
x.XConvertSelection.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_ulong, c.c_ulong, c.c_ulong]
x.XGetWindowProperty.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_long, c.c_long, c.c_int, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_int), c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.c_void_p)]
test = c.CDLL('/test-deps/usr/lib/x86_64-linux-gnu/libXtst.so.6')
test.XTestFakeMotionEvent.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_ulong]
test.XTestFakeButtonEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
test.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
display = x.XOpenDisplay(None)
root = x.XDefaultRootWindow(display)
# The second connection needs a window-manager equivalent keyboard focus.
x.XSetInputFocus.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_ulong]
x.XSetInputFocus(display, application_window, 2, 0)
x.XFlush(display)
time.sleep(2)
image = x.XGetImage(display, root, 0, 0, 1100, 850, c.c_ulong(-1).value, 2)
def screenshot(name):
    import pathlib, struct, zlib
    snap = x.XGetImage(display, root, 0, 0, 1100, 850, c.c_ulong(-1).value, 2)
    rows = bytearray()
    for yy in range(850):
        rows.append(0)
        for xx in range(1100):
            pixel = x.XGetPixel(snap, xx, yy)
            rows.extend(((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255))
    x.XDestroyImage(snap)
    def chunk(kind, data):
        return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))
    png = bytes((137, 80, 78, 71, 13, 10, 26, 10))
    png += chunk(b"IHDR", struct.pack("!IIBBBBB", 1100, 850, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    pathlib.Path("/screens/" + name).write_bytes(png)
screenshot("keyboard-before.png")
x.XDestroyImage(image)
image = x.XGetImage(display, root, 0, 0, 1100, 850, c.c_ulong(-1).value, 2)
# The final tall blue segment at the window's horizontal center is the
# onboarding Create New Profile button.
segments, start = [], None
for row in range(py.value, min(850, py.value + height.value)):
    color = x.XGetPixel(image, px.value + width.value // 2 - 110, row)
    red, green, blue = (color >> 16) & 255, (color >> 8) & 255, color & 255
    if red < 110 and green > 90 and blue > 150:
        if start is None: start = row
    elif start is not None:
        if row - start >= 20: segments.append((start, row))
        start = None
x.XDestroyImage(image)
assert segments, 'Could not find the profile creation button'
button_y = sum(segments[-1]) // 2
print("Window and button geometry:", px.value, py.value, width.value, height.value, button_y, flush=True)
test.XTestFakeMotionEvent(display, -1, px.value + width.value // 2, button_y, 0)
x.XFlush(display)
time.sleep(0.5)
# A missing Xcursor theme falls back to the monochrome X11 font cursor.
# Inspect the cursor that Qt actually installed on the clickable button.
class CursorImage(c.Structure):
    _fields_ = [('x', c.c_short), ('y', c.c_short),
                ('width', c.c_ushort), ('height', c.c_ushort),
                ('xhot', c.c_ushort), ('yhot', c.c_ushort),
                ('serial', c.c_ulong), ('pixels', c.POINTER(c.c_ulong))]
fixes = c.CDLL('/test-deps/usr/lib/x86_64-linux-gnu/libXfixes.so.3')
fixes.XFixesGetCursorImage.argtypes = [c.c_void_p]
fixes.XFixesGetCursorImage.restype = c.POINTER(CursorImage)
cursor = fixes.XFixesGetCursorImage(display)
assert cursor, 'Cannot inspect Qt cursor'
try:
    pixels = cursor.contents.pixels
    assert any(0 < ((pixels[i] >> 24) & 255) < 255
               for i in range(cursor.contents.width * cursor.contents.height)), \
        'Qt fell back to a monochrome cursor instead of the bundled theme'
finally:
    x.XFree(cursor)
print('Themed cursor rendering passed', flush=True)
test.XTestFakeButtonEvent(display, 1, 1, 0)
test.XTestFakeButtonEvent(display, 1, 0, 0)
x.XFlush(display)
time.sleep(1)
test.XTestFakeMotionEvent(display, -1, px.value + width.value // 2, button_y - 150, 0)
test.XTestFakeButtonEvent(display, 1, 1, 0)
test.XTestFakeButtonEvent(display, 1, 0, 0)
x.XFlush(display)
time.sleep(.5)
screenshot("keyboard-profile.png")
expected = 'portablekeyboard123'
for letter in expected:
    code = x.XKeysymToKeycode(display, ord(letter))
    assert code, f'No keyboard mapping for {letter}'
    test.XTestFakeKeyEvent(display, code, 1, 0)
    test.XTestFakeKeyEvent(display, code, 0, 0)
x.XFlush(display)
time.sleep(1)
screenshot("keyboard-typed.png")
control = x.XKeysymToKeycode(display, 0xffe3)
test.XTestFakeKeyEvent(display, control, 1, 0)
for letter in 'ac':
    code = x.XKeysymToKeycode(display, ord(letter))
    test.XTestFakeKeyEvent(display, code, 1, 0)
    test.XTestFakeKeyEvent(display, code, 0, 0)
test.XTestFakeKeyEvent(display, control, 0, 0)
x.XFlush(display)
time.sleep(1)
screenshot("keyboard-after.png")
clipboard = x.XInternAtom(display, b'CLIPBOARD', 0)
utf8 = x.XInternAtom(display, b'UTF8_STRING', 0)
prop = x.XInternAtom(display, b'DELTAGRAM_KEYBOARD_TEST', 0)
requestor = x.XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0)
x.XConvertSelection(display, clipboard, utf8, prop, requestor, 0)
x.XFlush(display)
for _ in range(50):
    kind, count, remaining = c.c_ulong(), c.c_ulong(), c.c_ulong()
    fmt, data = c.c_int(), c.c_void_p()
    x.XGetWindowProperty(display, requestor, prop, 0, 1024, 0, 0, c.byref(kind), c.byref(fmt), c.byref(count), c.byref(remaining), c.byref(data))
    if kind.value:
        actual = c.string_at(data, count.value).decode('utf-8')
        x.XFree(data)
        assert actual == expected, f'Keyboard input mismatch: {actual!r}'
        print(f'Profile name keyboard input passed: {actual}')
        break
    if data: x.XFree(data)
    time.sleep(.1)
else:
    raise SystemExit('Could not copy text from the profile name field')
x.XCloseDisplay(display)

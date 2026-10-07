"""Require an animated canvas rendered by bundled WebKit, offline."""
import ctypes as c
import time
x = c.CDLL('/test-deps/usr/lib/x86_64-linux-gnu/libX11.so.6')
x.XOpenDisplay.restype = c.c_void_p
x.XDefaultRootWindow.argtypes = [c.c_void_p]
x.XDefaultRootWindow.restype = c.c_ulong
x.XQueryTree.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ulong)), c.POINTER(c.c_uint)]
x.XFetchName.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_void_p)]
x.XFree.argtypes = [c.c_void_p]
x.XCloseDisplay.argtypes = [c.c_void_p]
display = x.XOpenDisplay(None)
assert display, 'Cannot connect to WebView test display'
root = x.XDefaultRootWindow(display)
for attempt in range(60):
    parent, result_root = c.c_ulong(), c.c_ulong()
    children, count = c.POINTER(c.c_ulong)(), c.c_uint()
    x.XQueryTree(display, root, c.byref(result_root), c.byref(parent), c.byref(children), c.byref(count))
    found = False
    for index in range(count.value):
        title = c.c_void_p()
        if x.XFetchName(display, children[index], c.byref(title)) and title:
            found |= b'Deltagram WebView JavaScript passed' in c.string_at(title)
            x.XFree(title)
    if children:
        x.XFree(children)
    if found:
        x.XCloseDisplay(display)
        print('Bundled WebKit rendered an animated canvas without GPU drivers.')
        break
    time.sleep(1)
else:
    raise SystemExit('Bundled WebKit did not execute the test page')

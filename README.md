Wing is a library which provides GLib-like API to some Windows API.

The goal of this library is twofold:
 * provide GLib-friendly integration points with Windows specific concepts
 * be a testing ground for API that may be included in GLib/GIO/Gtk+ once
   they are proven to be generally useful

This library does not provide any API and ABI backward compatibility guarantee.
You are expected to either include a copy of the DLL with your application (as
it is usually done by Windows applications with other GLib-based libraries) or,
if the license of your application is compatible, simply include copies of the
modules you need (for instance by using a git submodule).

If the current API does not satisfy your needs or if there are other Windows
specific API that you would find useful, please contact us.


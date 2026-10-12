# OAFExtractor crash fix

Replace `OAFExtractor/main.cpp` in your existing SouthParkFrameWork solution and rebuild **Debug | Win32** (or Release | Win32).

**Root cause:** the previous `extract()` function created a 1 MiB stack buffer. Since Win32 builds typically reserve only a 1 MiB stack, it could stack-overflow on entry, stopping the log immediately after `Mode: all files`. This update allocates the extraction buffer on the heap instead (64 KiB) and flushes archive-open and parser progress to `OAFExtractor.log`.

Drag an OAF file onto the *newly rebuilt* executable. Files extract beside the archive under `ExtractedOAF/<archive name>/`.

No framework/DLL project files changed.

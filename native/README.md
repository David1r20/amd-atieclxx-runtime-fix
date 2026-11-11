# Native portable edition

This single executable combines the Win32 GUI, AMD driver identification, write-rate diagnosis, approved RAM-only patch, and opt-in background watcher. No conventional installer or managed runtime is required.

## Meaning of the interface

**Proteção ativa — serviço funcionando** is displayed only when the AMD service is running, its client process exists, the protection task/monitor is present and the measured AMD process write rate is below the abnormal threshold.

**Proteção indisponível — serviço parado** is displayed if a monitor is registered but the AMD service is stopped. This avoids presenting registration as successful runtime protection.

**Testar sem proteção** is shown only when the service is stopped and no monitor is installed. It deliberately measures the original behavior for about five seconds; the UI explains that these are historical results **without** the fix, then restores the original service state.

**Disco C: total** includes all writers on that disk; it is not solely AMD's disk usage.

## Building

~~~powershell
.\native\Build.ps1
~~~

Requires Visual Studio C++ Build Tools and Windows SDK on the development machine. The executable is compiled with /MT; users do not need those tools.

## User-facing design

The interface avoids technical marketing about implementation languages or dependencies. Its relevant messages concern SSD writes, the AMD service status, compatibility, whether protection is actually active, and how to remove it.

## Compatibility

AMD vendor/signature verification allows diagnostics across different SHA-256 hashes. Memory modification is restricted to the exact supported SHA-256 and verified opcode at RVA 0x21122. Never bypass the version gate.

## Validation performed

- Win32 window created and captured successfully.
- Same AMD process yielded 0 extra write bytes in short verified samples after applying the patch.
- Native watcher restarted automatically for a new AMD process.
- A direct activation test returned with AMD service Running/Auto and task Running.
- The updated UI was compiled and deployed to the installed native watcher, followed by a seven-second zero-write confirmation.

A full reboot, long sessions, other AMD versions and FreeSync/anti-cheat still require validation.

## Source

- main.cpp — Windows-native Win32 GUI, signed AMD identity check, I/O measurements, version-locked patch and scheduled background monitor.
- Build.ps1 — standalone native x64 compiler script.

The old WPF application under SetupGui remains historical, but is not the recommended community download.

# AMD atieclxx Fix — portable Windows utility

**An unofficial, experimental SSD-write diagnostic and narrowly version-locked AMD External Events workaround.**

**New recommended edition: native C++ Win32, a single portable EXE (~272 KB)**. No installer, no .NET dependency, and no manual PowerShell commands are needed by end users. The earlier WPF/.NET GUI is retained in the repository for historical reference, but is no longer the recommended download.

> This project is **private beta** and is not affiliated with AMD. It has not been validated on every driver version or computer. Do not bypass SmartScreen, Defender, or security tools.

## What it shows

The app checks the signed AMD External Events client, its service state, its process write I/O, and total disk write activity on the Windows C: physical disk. These counters have distinct meanings: total disk writes include activity from other apps and do not directly measure NAND wear.

A stopped service is shown as **stopped**, not as an active fix. When protection is active, the interface verifies that the AMD service and client are running and that no excessive process writes were observed in the sampled interval.

### An optional test without protection

If the AMD service is stopped and no monitor is installed, the interface offers **Testar sem proteção** (test without protection). With consent and administrator authorization, it briefly starts the original AMD service, measures its writes for about five seconds, then returns the service to its previous state. These readings are clearly labeled as *historical measurements without protection*, not evidence that the active fix is still copying.

## Compatibility versus diagnosis

**Diagnosis:** works on other AMD-signed versions of atieclxx.exe when their identity and signature are verified. **Fix:** is enabled only for the exact validated SHA-256 and six-byte opcode below.

| Property | Validated build |
|---|---|
| Sample driver | AMD driver 32.0.32015.7029 |
| Client SHA-256 | 046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066 |
| Call-site RVA | 0x21122 |
| Original opcode | FF 15 78 A5 04 00 |
| RAM-only replacement | B8 01 00 00 00 90 |

A different hash **must never receive the patch** without a new code-level investigation and validation.

## Download and use

Download the **AMD-atieclxx-Fix-Native.exe** asset from the most recent private beta GitHub release. Open it normally; no installation wizard is required.

1. **Verificar novamente** — checks the current service and process state, and process/disk write rate.
2. **Testar sem proteção** — appears only when the AMD service is stopped and no protection exists; explicitly warns that it will briefly reproduce the original bug.
3. **Ativar proteção** — appears only when a supported SHA exhibits abnormal writes. After approval, copies this same small EXE to a protected Program Files directory and registers a Windows startup task as SYSTEM. This is an opt-in background monitor, **not a traditional installer**.
4. **Remover proteção** — removes the monitor's task and stops/disables the AMD service so the excessive self-copy loop does not resume unprotected.

The signed AMD executable on disk is never replaced. Only its process memory is changed, and only for an exact, supported binary.

## Build from source

Developers need Visual Studio C++ Build Tools and Windows SDK; end users do not:

~~~powershell
.\native\Build.ps1
~~~

Output: dist-native/AMD-atieclxx-Fix-Native.exe. The Windows-native GUI and background monitor share one executable. Build with static C runtime (/MT).

The existing WPF and .NET code remains under SetupGui and Program.cs as legacy references; the default GitHub Actions workflow now builds the native edition only.

## Tested

- Native GUI opened on Windows and screenshots were inspected.
- Current AMD binary was verified by hash and Authenticode.
- The binary uses under 300 KB on the tested system.
- Native monitor replaced a prior .NET task without changing AMD's signed disk file.
- Restarting the AMD service with the native watcher active resulted in zero AMD process writes over eight seconds.
- Updated UI distinguishes a stopped service from active protection.
- After deploying the UI correction to the active watcher, the service and task remained Running, and the AMD client wrote zero bytes over a seven-second sample.

These are narrow observations, not universal performance claims.

## Limitations and security

- Administrator/UAC elevation is required only for activation/removal and the temporary test.
- The monitor runs as SYSTEM when enabled, so only trusted binaries should be used.
- The portable EXE is not digitally code-signed.
- Complete Windows reboot, other PCs/drivers, FreeSync/game/anti-cheat scenarios, and extended uptime are not yet comprehensively validated.
- A new AMD driver SHA is diagnostic-only, never patched automatically.
- We do not redistribute AMD proprietary executables, ETW traces, or private machine logs.

See [native technical notes](native/README.md), [security](docs/SECURITY.md), and [tests](docs/TESTS.md).

## English summary

A small, portable native Windows executable diagnoses repeated AMD atieclxx.exe disk writes. It can diagnose other verified AMD versions but patches one exact known binary version, in process memory only. The original AMD driver files remain untouched. The app can optionally register a background watcher and remove it again. Private experimental beta, not official AMD software.

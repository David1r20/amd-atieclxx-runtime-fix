// AMD atieclxx.exe self-copy workaround for the EXACT pinned October 2026 driver.
// Never modifies the signed DriverStore file. Performs six-byte in-memory correction.
// No external packages, network, telemetry, auto-update or driver binary redistribution.
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

internal static class Program
{
    const string ExpectedImageSha256 = "046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066";
    const int TargetRva = 0x21122;
    static readonly byte[] Before = [0xFF, 0x15, 0x78, 0xA5, 0x04, 0x00];
    static readonly byte[] After = [0xB8, 0x01, 0x00, 0x00, 0x00, 0x90];
    static readonly string DataDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "AmdAtieclxxRuntimeFix");
    static readonly string StatusFile = Path.Combine(DataDir, "status.json");
    static readonly string LogFile = Path.Combine(DataDir, "events.log");
    static readonly JsonSerializerOptions JsonOpts = new() { WriteIndented = true };

    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr OpenProcess(uint rights, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool CloseHandle(IntPtr process);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool ReadProcessMemory(IntPtr process, IntPtr address, [Out] byte[] buffer, nint size, out nint read);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] bytes, nint size, out nint written);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool VirtualProtectEx(IntPtr process, IntPtr address, nint size, uint newProtect, out uint oldProtect);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool FlushInstructionCache(IntPtr process, IntPtr address, nint size);
    [DllImport("ntdll.dll")]
    static extern int NtSuspendProcess(IntPtr process);
    [DllImport("ntdll.dll")]
    static extern int NtResumeProcess(IntPtr process);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetProcessIoCounters(IntPtr process, out IoCounters counters);
    [StructLayout(LayoutKind.Sequential)]
    struct IoCounters
    {
        public ulong ReadOperationCount, WriteOperationCount, OtherOperationCount;
        public ulong ReadTransferCount, WriteTransferCount, OtherTransferCount;
    }

    static void WriteLog(string eventName, string detail)
    {
        try
        {
            Directory.CreateDirectory(DataDir);
            if (File.Exists(LogFile) && new FileInfo(LogFile).Length > 1_000_000)
                File.Move(LogFile, LogFile + ".old", overwrite: true);
            File.AppendAllText(LogFile, $"{DateTimeOffset.Now:O} {eventName}: {detail}{Environment.NewLine}");
        }
        catch { /* Lack of logging permission must never interrupt cleanup */ }
    }
    static void WriteStatus(object data)
    {
        try
        {
            Directory.CreateDirectory(DataDir);
            string json = JsonSerializer.Serialize(data, JsonOpts);
            File.WriteAllText(StatusFile, json);
        }
        catch (Exception ex) { WriteLog("STATUS_IO_ERROR", ex.Message); }
    }

    static string IdentifyImage(Process p)
    {
        string path = p.MainModule?.FileName ?? throw new InvalidOperationException("Image path unavailable");
        string leaf = Path.GetFileName(path);
        if (!leaf.Equals("atieclxx.exe", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Wrong image basename");
        string sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)));
        if (!sha256.Equals(ExpectedImageSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException($"Unsupported original image SHA256 {sha256} at {path}");
        return path;
    }
    static void RequireBytes(IntPtr h, IntPtr address, byte[] expected)
    {
        byte[] b = new byte[expected.Length];
        if (!ReadProcessMemory(h, address, b, b.Length, out nint read) || read != b.Length)
            throw new InvalidOperationException("ReadProcessMemory failed: " + Marshal.GetLastWin32Error());
        if (!b.AsSpan().SequenceEqual(expected))
            throw new InvalidOperationException($"Unexpected opcode at patch address: {Convert.ToHexString(b)}");
    }
    static byte[] GetBytes(IntPtr h, IntPtr address)
    {
        byte[] b = new byte[6];
        if (!ReadProcessMemory(h, address, b, b.Length, out nint read) || read != b.Length)
            throw new InvalidOperationException("ReadProcessMemory failed: " + Marshal.GetLastWin32Error());
        return b;
    }
    static void Patch(IntPtr handle, IntPtr address)
    {
        bool suspended = false, writable = false;
        uint oldProtect = 0;
        try
        {
            int nt = NtSuspendProcess(handle);
            if (nt != 0) throw new InvalidOperationException($"NtSuspendProcess NTSTATUS 0x{nt:X8}");
            suspended = true;
            RequireBytes(handle, address, Before);
            if (!VirtualProtectEx(handle, address, 6, 0x40, out oldProtect))
                throw new InvalidOperationException("VirtualProtectEx failed: " + Marshal.GetLastWin32Error());
            writable = true;
            if (!WriteProcessMemory(handle, address, After, 6, out nint written) || written != 6)
            {
                // Best effort restore while still suspended, even if OS returned partial write.
                _ = WriteProcessMemory(handle, address, Before, 6, out _);
                _ = FlushInstructionCache(handle, address, 6);
                throw new InvalidOperationException("WriteProcessMemory failed: " + Marshal.GetLastWin32Error());
            }
            _ = FlushInstructionCache(handle, address, 6);
            RequireBytes(handle, address, After);
        }
        finally
        {
            if (writable) _ = VirtualProtectEx(handle, address, 6, oldProtect, out _);
            if (suspended) _ = NtResumeProcess(handle);
        }
    }

    sealed record Result(string State, int Pid, string Image, string Detail, string? Opcode, ulong? WriteBytes);
    static Result CheckOrPatch(Process p, bool patch)
    {
        string image = "";
        IntPtr h = IntPtr.Zero;
        try
        {
            image = IdentifyImage(p);
            // VM_OPERATION, VM_READ, VM_WRITE, QUERY_INFORMATION, SUSPEND_RESUME.
            h = OpenProcess(0x0C38, false, p.Id);
            if (h == IntPtr.Zero)
                throw new InvalidOperationException("OpenProcess failed: " + Marshal.GetLastWin32Error());
            long baseAddress = p.MainModule!.BaseAddress.ToInt64();
            IntPtr address = new(baseAddress + TargetRva);
            byte[] b = GetBytes(h, address);
            if (b.AsSpan().SequenceEqual(After))
                return new("Patched", p.Id, image, "Already patched in this process", Convert.ToHexString(b), ReadWriteBytes(h));
            if (!b.AsSpan().SequenceEqual(Before))
                return new("UnexpectedBytes", p.Id, image, "Refusing to alter unknown code", Convert.ToHexString(b), ReadWriteBytes(h));
            if (!patch)
                return new("Unpatched", p.Id, image, "Original six bytes present", Convert.ToHexString(b), ReadWriteBytes(h));
            Patch(h, address);
            return new("Patched", p.Id, image, "Six bytes changed only in process memory", Convert.ToHexString(GetBytes(h,address)), ReadWriteBytes(h));
        }
        catch (Exception ex) { return new("Error", p.Id, image, ex.Message, null, null); }
        finally { if (h != IntPtr.Zero) CloseHandle(h); }
    }
    static ulong? ReadWriteBytes(IntPtr h) => GetProcessIoCounters(h, out IoCounters io) ? io.WriteTransferCount : null;

    static Result[] InspectAll(bool patch)
    {
        List<Result> results = [];
        foreach (Process p in Process.GetProcessesByName("atieclxx"))
        {
            using(p) { results.Add(CheckOrPatch(p, patch)); }
        }
        return results.ToArray();
    }
    static int PrintStatus()
    {
        Result[] results = InspectAll(patch: false);
        var output = new { Time = DateTimeOffset.Now, ProgramVersion = "0.2.0",
            ExpectedSha256 = ExpectedImageSha256, ProcessCount = results.Length, Processes = results };
        WriteStatus(output);
        Console.WriteLine(JsonSerializer.Serialize(output, JsonOpts));
        return results.Any(x => x.State == "Error" || x.State == "UnexpectedBytes") ? 2 : 0;
    }
    static int RunOnce()
    {
        Result[] results = InspectAll(patch:true);
        foreach (Result r in results) WriteLog("ONCE_" + r.State, $"PID={r.Pid} {r.Detail}");
        var output = new { Time = DateTimeOffset.Now, Results = results };
        WriteStatus(output);
        Console.WriteLine(JsonSerializer.Serialize(output, JsonOpts));
        return results.Length == 0 ? 3 : results.All(x => x.State == "Patched") ? 0 : 2;
    }

    // Fallback for a future, unsupported AMD driver:
    // Do not alter an unknown executable. Only stop the AMD service if this new client
    // is proven to produce sustained high I/O, preventing regression after updates.
    static bool ExcessiveWriteRate(ulong before, ulong after, double seconds) =>
        seconds > 0 && after >= before && ((after - before) / seconds) > 150_000;

    static int Sc(params string[] arguments)
    {
        ProcessStartInfo info = new(Path.Combine(Environment.SystemDirectory, "sc.exe"))
        {
            UseShellExecute = false, CreateNoWindow = true
        };
        foreach (string arg in arguments) info.ArgumentList.Add(arg);
        using Process process = Process.Start(info) ?? throw new InvalidOperationException("Failed to launch sc.exe");
        if (!process.WaitForExit(8000))
        {
            process.Kill();
            throw new TimeoutException("sc.exe timed out");
        }
        return process.ExitCode;
    }
    static void ProtectAgainstUnknownVersion(Process process)
    {
        IntPtr handle = IntPtr.Zero;
        try
        {
            handle = OpenProcess(0x1000, false, process.Id); // PROCESS_QUERY_LIMITED_INFORMATION
            if (handle == IntPtr.Zero || !GetProcessIoCounters(handle, out IoCounters a))
                throw new InvalidOperationException("Cannot measure I/O from unknown driver");
            Stopwatch watch = Stopwatch.StartNew();
            Thread.Sleep(4000);
            if (!GetProcessIoCounters(handle, out IoCounters b))
                throw new InvalidOperationException("Cannot complete I/O measurement");
            watch.Stop();
            double rate = (b.WriteTransferCount >= a.WriteTransferCount)
                ? (b.WriteTransferCount - a.WriteTransferCount) / watch.Elapsed.TotalSeconds
                : 0;
            if (!ExcessiveWriteRate(a.WriteTransferCount, b.WriteTransferCount, watch.Elapsed.TotalSeconds))
            {
                WriteLog("UNKNOWN_DRIVER_LOW_IO", $"PID={process.Id} IOWriteRate={rate:F0} B/s; no intervention");
                return;
            }
            WriteLog("UNKNOWN_DRIVER_EXCESSIVE_IO", $"PID={process.Id} IOWriteRate={rate:F0} B/s; restoring conservative disabled-service mitigation");
            // Prevent automatic restart, then stop service. Never patch unknown machine code.
            int configExit = Sc("config", "AMD External Events Utility", "start=", "disabled");
            int stopExit = Sc("stop", "AMD External Events Utility");
            WriteLog("FAILSAFE_SERVICE_DISABLED", $"sc_config_exit={configExit} sc_stop_exit={stopExit}");
        }
        catch (Exception ex) { WriteLog("UNKNOWN_DRIVER_GUARD_ERROR", ex.Message); }
        finally { if(handle != IntPtr.Zero) CloseHandle(handle); }
    }

    static int Watch()
    {
        using Mutex mutex = new(false, @"Global\AmdAtieclxxRuntimeFix", out bool _);
        bool acquired = false;
        try { acquired = mutex.WaitOne(0); } catch (AbandonedMutexException) { acquired = true; }
        if (!acquired) return 4; // A copy is already monitoring.
        try
        {
            WriteLog("WATCH_STARTED", $"PID={Environment.ProcessId}, version=0.2.0");
            var seen = new Dictionary<int, (DateTime Start, string State, int Attempts)>();
            while (true)
            {
                HashSet<int> alive = [];
                foreach(Process p in Process.GetProcessesByName("atieclxx"))
                {
                    using (p)
                    {
                        try
                        {
                            alive.Add(p.Id);
                            DateTime start = p.StartTime.ToUniversalTime();
                            var found = seen.TryGetValue(p.Id, out var previous) && previous.Start == start;
                            if (found && (previous.State == "Patched" || previous.Attempts >= 6))
                                continue;
                            Result result = CheckOrPatch(p, patch: true);
                            int attempts = found ? previous.Attempts + 1 : 1;
                            seen[p.Id] = (start, result.State, attempts);
                            WriteLog(result.State, $"PID={result.Pid} ATTEMPT={attempts} IMAGE={result.Image} {result.Detail}");
                            if (result.State == "Error" && result.Detail.StartsWith("Unsupported original image SHA256", StringComparison.Ordinal))
                                ProtectAgainstUnknownVersion(p);
                        }
                        catch (Exception ex) { WriteLog("WATCH_ERROR", ex.Message); }
                    }
                }
                foreach(int id in seen.Keys.Except(alive).ToArray())
                    seen.Remove(id);
                Thread.Sleep(300);
            }
        }
        finally { mutex.ReleaseMutex(); }
    }
    static int SelfTest()
    {
        string source = Path.Combine(AppContext.BaseDirectory, "atieclxx_original.exe");
        // In deployed Program Files, source AMD file is never shipped; test local copy in lab.
        if (!File.Exists(source))
            source = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "atieclxx_original.exe"));
        if (!File.Exists(source))
        {
            Console.WriteLine("Self-test requires original binary in lab folder, not in release bundle.");
            return 3;
        }
        if (!ExcessiveWriteRate(0, 800_000, 1.0) || ExcessiveWriteRate(0, 40_000, 1.0))
            throw new InvalidOperationException("Fallback rate threshold self-test failed");
        byte[] b = File.ReadAllBytes(source);
        string hash = Convert.ToHexString(SHA256.HashData(b));
        if(hash != ExpectedImageSha256 || b.Length != 677392) return 2;
        int offset = TargetRva - 0x1000 + 0x400;
        if(!b.AsSpan(offset, Before.Length).SequenceEqual(Before)) return 2;
        Console.WriteLine("Self-test OK: exact image hash, size and opcode");
        return 0;
    }

    static int Main(string[] args)
    {
        if (!OperatingSystem.IsWindows()) return 2;
        string command = args.Length > 0 ? args[0].ToLowerInvariant() : "--status";
        try
        {
            return command switch
            {
                "--status" => PrintStatus(),
                "--once" => RunOnce(),
                "--watch" => Watch(),
                "--self-test" => SelfTest(),
                _ => 2
            };
        }
        catch(Exception ex)
        {
            WriteLog("FATAL", ex.ToString());
            Console.Error.WriteLine(ex);
            return 1;
        }
    }
}
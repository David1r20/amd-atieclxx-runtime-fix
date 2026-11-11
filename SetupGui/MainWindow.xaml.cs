using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace AtieclxxSetup;

public partial class MainWindow : Window
{
    private readonly string? _startupAction;
    private bool _working;
    private bool _installed;
    private bool _compatible;
    private bool _diagnosticEligible;
    private bool _canInstall;

    private static readonly Brush Success = new SolidColorBrush(Color.FromRgb(123, 220, 210));
    private static readonly Brush Warning = new SolidColorBrush(Color.FromRgb(246, 196, 122));
    private static readonly Brush Error = new SolidColorBrush(Color.FromRgb(245, 155, 159));

    private static readonly string[] PowerShellScripts =
        ["Common.ps1", "Check.ps1", "Diagnostic.ps1", "Install.ps1", "Uninstall.ps1", "Verify.ps1"];

    public MainWindow()
    {
        InitializeComponent();
        string[] arguments = Environment.GetCommandLineArgs();
        _startupAction = arguments.Length > 1 ? arguments[1] : null;
        Loaded += async (_, _) =>
        {
            if (_startupAction is "--install" or "--uninstall")
                await ExecuteActionAsync(_startupAction == "--install" ? "Install" : "Uninstall");
            else if (_startupAction == "--diagnose")
                await ExecuteDiagnosticAsync();
            else
                await RefreshAsync();
        };
    }

    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount == 2) WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
        else DragMove();
    }
    private void Close_Click(object sender, RoutedEventArgs e) => Close();
    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private static bool IsAdministrator()
    {
        using WindowsIdentity id = WindowsIdentity.GetCurrent();
        return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);
    }

    private void SetBusy(bool busy, string message = "")
    {
        _working = busy;
        BusyBar.Visibility = busy ? Visibility.Visible : Visibility.Collapsed;
        RefreshButton.IsEnabled = !busy;
        TestWritesButton.IsEnabled = !busy;
        InstallButton.IsEnabled = !busy && _canInstall && _compatible && !_installed;
        RemoveButton.IsEnabled = !busy && _installed;
        if (!string.IsNullOrWhiteSpace(message)) ActivityText.Text = message;
    }

    private void UpdateHeadline(string title, string description, string badge, Brush color)
    {
        StatusTitle.Text = title;
        StatusSubline.Text = description;
        StatusTag.Text = badge;
        StatusTag.Foreground = color;
        StatusSymbol.Foreground = color;
        StatusSymbol.Text = color == Success ? "✓" : color == Error ? "!" : "◉";
    }

    private static string StagingDirectory(bool privileged)
    {
        // Non-privileged detection needs only read-only scripts, in user local app data.
        // Elevated operations place scripts and signed application payload beneath Program Files.
        string root = privileged
            ? Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)
            : Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return Path.Combine(root, "AmdAtieclxxSetup", "Staging-v032");
    }

    private static string EnsureStaging(bool privileged, bool includeMonitor)
    {
        string folder = StagingDirectory(privileged);
        Directory.CreateDirectory(folder);
        Assembly a = Assembly.GetExecutingAssembly();
        foreach (string script in PowerShellScripts)
        {
            string name = "Fix." + script;
            using Stream stream = a.GetManifestResourceStream(name)
                ?? throw new FileNotFoundException($"Recurso ausente: {name}");
            using FileStream output = new(Path.Combine(folder, script), FileMode.Create, FileAccess.Write, FileShare.None);
            stream.CopyTo(output);
        }
        if (privileged && includeMonitor)
        {
            const string payloadName = "AmdAtieclxxRuntimeFix.exe";
            using Stream stream = a.GetManifestResourceStream("Fix." + payloadName)
                ?? throw new FileNotFoundException("Executável de monitor não embutido");
            using FileStream output = new(Path.Combine(folder, payloadName), FileMode.Create, FileAccess.Write, FileShare.None);
            stream.CopyTo(output);
        }
        return folder;
    }

    private sealed record CommandResult(int ExitCode, string Output, string Error);
    private static async Task<CommandResult> RunScriptAsync(string script, bool privileged, int timeoutSeconds)
    {
        string dir = await Task.Run(() => EnsureStaging(privileged, script == "Install"));
        ProcessStartInfo psi = new(Path.Combine(Environment.SystemDirectory, "WindowsPowerShell", "v1.0", "powershell.exe"))
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden,
            WorkingDirectory = dir,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8
        };
        psi.ArgumentList.Add("-NoLogo");
        psi.ArgumentList.Add("-NoProfile");
        psi.ArgumentList.Add("-NonInteractive");
        psi.ArgumentList.Add("-ExecutionPolicy");
        psi.ArgumentList.Add("Bypass");
        psi.ArgumentList.Add("-File");
        psi.ArgumentList.Add(Path.Combine(dir, script + ".ps1"));
        try
        {
            using Process process = new() { StartInfo = psi };
            if (!process.Start()) throw new InvalidOperationException("Não foi possível iniciar a tarefa local.");
            Task<string> stdout = process.StandardOutput.ReadToEndAsync();
            Task<string> stderr = process.StandardError.ReadToEndAsync();
            using CancellationTokenSource cts = new(TimeSpan.FromSeconds(timeoutSeconds));
            try { await process.WaitForExitAsync(cts.Token); }
            catch (OperationCanceledException)
            {
                if (!process.HasExited) process.Kill(entireProcessTree: true);
                throw new TimeoutException("Operação excedeu o limite de segurança.");
            }
            string output = await stdout;
            string error = await stderr;
            return new(process.ExitCode, output.Trim(), error.Trim());
        }
        finally
        {
            // The privileged staging copy of the embedded 67MB monitor is temporary.
            // Do not leave that duplicate on the system SSD after the operation.
            if (privileged)
            {
                try { Directory.Delete(dir, recursive: true); }
                catch { /* A failed cleanup must not conceal the installation result. */ }
            }
        }
    }

    private static JsonDocument? ReadJsonFromOutput(string output)
    {
        // The PowerShell helpers emit one JSON object per operation, on a line.
        foreach (string raw in output.Split('\n').Reverse())
        {
            string line = raw.Trim();
            if (!line.StartsWith('{')) continue;
            try { return JsonDocument.Parse(line); }
            catch(JsonException) { }
        }
        return null;
    }
    private static bool Bool(JsonElement parent, string name) =>
        parent.TryGetProperty(name, out JsonElement value) &&
        value.ValueKind == JsonValueKind.True;
    private static string Text(JsonElement parent, string name, string fallback = "") =>
        parent.TryGetProperty(name, out JsonElement value) ? value.ToString() : fallback;

    private static double Number(JsonElement parent, string key, double fallback=0) =>
        parent.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.Number
            ? v.GetDouble() : fallback;

    private void ShowRates(JsonElement sample, bool historical)
    {
        bool valid = Bool(sample, "Measured");
        double bytes = Number(sample, "RateBps");
        WriteValue.Text = valid ? (bytes / 1_000_000.0).ToString("0.000") : "—";
        WriteValue.Foreground = !valid ? Warning : bytes >= 150000 ? Error : Success;
        WriteNote.Text = historical ? "Teste temporário (5 s)" : valid ? "I/O do atieclxx" : "Processo não está ativo";

        bool diskValid = valid && Bool(sample, "DiskMeasured");
        double diskBytes = Number(sample, "DiskRateBps");
        DiskValue.Text = diskValid ? (diskBytes / 1_000_000.0).ToString("0.000") : "—";
        DiskValue.Foreground = diskValid ? Brushes.WhiteSmoke : Warning;
        DiskNote.Text = historical ? "C: total durante o teste" :
                        diskValid ? "Todas as gravações de C:" : "Sem leitura do disco";
    }

    private async Task RefreshAsync()
    {
        SetBusy(true, "Analisando o driver AMD e as gravações do processo…");
        DriverValue.Text = "Analisando…";
        DriverNote.Text = "Validação por hash";
        WriteValue.Text = "—";
        WriteNote.Text = "Aguardando leitura";
        DiskValue.Text = "—";
        DiskNote.Text = "Aguardando leitura";
        ServiceValue.Text = "Analisando…";
        TestWritesButton.Visibility = Visibility.Collapsed;
        UpdateHeadline("Verificando o sistema…", "Lendo os dados locais sem alterar o serviço AMD.", "ANALISANDO", Warning);
        try
        {
            CommandResult res = await RunScriptAsync("Check", privileged:false, timeoutSeconds:35);
            using JsonDocument? doc = ReadJsonFromOutput(res.Output);
            if (doc == null) throw new InvalidDataException("Diagnóstico sem dados válidos: " + res.Error);
            JsonElement root = doc.RootElement;
            if(root.TryGetProperty("Error", out JsonElement scriptError))
                throw new InvalidOperationException(scriptError.ToString());
            _compatible = Bool(root, "Compatible");
            _diagnosticEligible = Bool(root, "DiagnosticEligible");
            _installed = Bool(root, "ExistingRuntimeFix");
            string state = Text(root, "AMDService", "Desconhecido");
            string startType = Text(root, "AMDStartup");
            DriverValue.Text = _compatible ? "Compatível" : _diagnosticEligible ? "Outro SHA" : "Não verificado";
            DriverValue.Foreground = _compatible ? Success : Warning;
            DriverNote.Text = _compatible ? "Hash do patch validado" : _diagnosticEligible ? "Diagnóstico permitido" : "Identidade não validada";
            ServiceValue.Text = state.Equals("Running", StringComparison.OrdinalIgnoreCase) ? "Em execução"
                              : state.Equals("Stopped", StringComparison.OrdinalIgnoreCase) ? "Parado" : state;
            ServiceNote.Text = state.Equals("Running", StringComparison.OrdinalIgnoreCase)
                              ? "Inicialização: " + startType : "External Events Utility";

            bool hasIo = root.TryGetProperty("ProcessWriteSample", out JsonElement sample) &&
                         Bool(sample, "Measured");
            double rateBps = hasIo ? Number(sample, "RateBps") : 0;
            if(root.TryGetProperty("ProcessWriteSample", out sample) && sample.ValueKind == JsonValueKind.Object)
                ShowRates(sample, historical:false);
            else
            {
                WriteValue.Text = "—"; DiskValue.Text = "—";
                WriteNote.Text = "Processo não está ativo"; DiskNote.Text="Sem leitura do disco";
            }

            _canInstall = _compatible && !_installed && hasIo && rateBps >= 150000;
            if (_installed)
            {
                CommandResult verified = await RunScriptAsync("Verify", false, 25);
                using JsonDocument? verifiedDoc = ReadJsonFromOutput(verified.Output);
                bool patched = false, taskRunning = false;
                if(verifiedDoc != null) {
                    JsonElement v=verifiedDoc.RootElement;
                    if(v.TryGetProperty("PatchStates",out JsonElement states) && states.ValueKind == JsonValueKind.Array)
                        patched = states.EnumerateArray().Any(e => e.ToString() == "Patched");
                    taskRunning = Text(v,"Task") == "Running";
                }
                if (patched && taskRunning)
                {
                    UpdateHeadline("Proteção ativa", "A escrita repetitiva do processo AMD está bloqueada.", "PROTEGIDO", Success);
                    ActivityText.Text="Correção instalada e confirmada. Nenhuma ação necessária.";
                }
                else
                {
                    UpdateHeadline("Proteção instalada, mas inativa",
                        state == "Stopped" ? "O serviço AMD está parado; não há atividade a medir."
                            : "A tarefa ou o patch de memória não foi confirmado.", "VERIFICAR", Warning);
                    ActivityText.Text="O serviço e o monitor devem ser verificados antes de novas alterações.";
                }
            }
            else if (!_diagnosticEligible)
            {
                UpdateHeadline("Identidade do driver não verificada",
                    "Não é seguro iniciar o serviço nem aplicar o patch.", "NÃO VERIFICADO", Warning);
                ActivityText.Text="O executável AMD não passou na validação de assinatura e fornecedor.";
            }
            else if (!hasIo)
            {
                UpdateHeadline(state == "Stopped" ? "Serviço AMD parado" : "Processo AMD não detectado",
                    _compatible ? "Sem processo ativo, não existe uma taxa atual de escrita para medir."
                       : "Outro SHA AMD reconhecido. Podemos medir, mas ainda não aplicar o patch.",
                    "SEM AMOSTRA", Warning);
                TestWritesButton.Visibility=Visibility.Visible;
                ActivityText.Text="Use “Medir gravações” para um teste de 5 segundos com restauração automática do serviço.";
            }
            else if (rateBps >= 150000)
            {
                UpdateHeadline(_compatible ? "Gravação excessiva detectada" : "Escrita excessiva, versão ainda sem patch",
                    _compatible ? "O atieclxx.exe está gerando I/O elevado. Veja a taxa medida abaixo."
                       : "O diagnóstico funciona neste SHA, mas nenhuma correção será aplicada a ele.",
                    "PROBLEMA", Error);
                ActivityText.Text=_compatible ? "A correção está disponível para este executável AMD."
                    : "Versão não suportada para patch. Registre o hash para uma análise específica.";
            }
            else
            {
                UpdateHeadline("Sem gravações excessivas",
                    "Não foi identificada escrita anormal nesta amostra.", "NORMAL", Success);
                ActivityText.Text=_compatible ? "Não recomendamos instalar a correção sem evidências do problema."
                    : "O processo foi medido, mas este SHA ainda não possui patch validado.";
            }
            InstallButton.Content = _installed ? "Já instalado" : "Instalar proteção";
        }
        catch(Exception ex)
        {
            _compatible = false; _diagnosticEligible=false; _installed = false; _canInstall=false;
            DriverValue.Text="Indisponível";
            WriteValue.Text="—"; DiskValue.Text="—"; ServiceValue.Text="—";
            UpdateHeadline("Não foi possível verificar",ex.Message,"ERRO",Error);
            ActivityText.Text="O diagnóstico não alterou o estado do serviço.";
        }
        finally { SetBusy(false); }
    }

    private async Task ExecuteDiagnosticAsync()
    {
        if(_working) return;
        SetBusy(true, "Medindo a escrita com o serviço AMD temporariamente ativo…");
        UpdateHeadline("Medindo gravações…",
            "O serviço será restaurado ao estado anterior ao terminar.", "TESTE", Warning);
        try
        {
            if(!IsAdministrator()) throw new UnauthorizedAccessException("O teste temporário precisa da autorização do Windows.");
            CommandResult command=await RunScriptAsync("Diagnostic",privileged:true,timeoutSeconds:60);
            using JsonDocument? doc=ReadJsonFromOutput(command.Output);
            if(doc == null) throw new InvalidDataException("O teste não retornou um relatório válido: " + command.Error);
            JsonElement result=doc.RootElement;
            if(command.ExitCode != 0 || Text(result,"Result")!="DIAGNOSIS_OK" || !Bool(result,"Restored"))
                throw new InvalidOperationException(Text(result,"Error",Text(result,"RestoreError","Teste não concluído"))
                                                    + "\n" + command.Error);
            if(!result.TryGetProperty("ProcessWriteSample",out JsonElement sample) ||
               !Bool(sample,"Measured"))
                throw new InvalidDataException("O processo não forneceu uma medição válida.");
            double rate=Number(sample,"RateBps");
            double disk=Number(sample,"DiskRateBps");
            await RefreshAsync();  // Read the now-restored service state without starting it.
            ShowRates(sample,historical:true);
            TestWritesButton.Visibility=Visibility.Visible;
            bool excessive = rate >= 150000;
            _canInstall = _compatible && !_installed && excessive;
            InstallButton.IsEnabled = _canInstall && !_working;
            if(excessive)
            {
                UpdateHeadline(_compatible ? "Escrita excessiva confirmada" : "Escrita excessiva em SHA não suportado",
                    _compatible ? "O teste mediu o problema e restaurou o serviço AMD ao estado anterior."
                      : "Problema detectado. Não existe patch validado para esta versão do executável.",
                    "DETECTADO", Error);
                ActivityText.Text=$"Processo: {rate/1e6:0.000} MB/s · Disco C: total: {disk/1e6:0.000} MB/s · Medição de 5 s."
                    + (_compatible ? "" : " · Correção indisponível para este SHA.");
            }
            else
            {
                UpdateHeadline("Sem escrita excessiva no teste",
                    "O serviço foi testado temporariamente e restaurado.", "NORMAL", Success);
                ActivityText.Text="Não recomendamos instalar a correção sem detectar o problema.";
            }
        }
        catch(Exception ex)
        {
            UpdateHeadline("Teste não concluído",ex.Message,"ERRO",Error);
            ActivityText.Text="Verifique o estado do serviço AMD. A reversão é obrigatória.";
        }
        finally { SetBusy(false); }
    }

    private void BeginElevatedAction(string argument)
    {
        try
        {
            if (IsAdministrator())
            {
                if (argument == "--diagnose") _ = ExecuteDiagnosticAsync();
                else _ = ExecuteActionAsync(argument == "--install" ? "Install" : "Uninstall");
                return;
            }
            ProcessStartInfo psi = new(Environment.ProcessPath ?? throw new InvalidOperationException("Caminho do instalador indisponível"))
            {
                UseShellExecute = true,
                Verb = "runas",
                Arguments = argument,
                WorkingDirectory = AppContext.BaseDirectory
            };
            using Process? process = Process.Start(psi);
            if (process != null) Close();
        }
        catch (System.ComponentModel.Win32Exception ex) when (ex.NativeErrorCode == 1223)
        {
            ActivityText.Text = "A autorização do Windows foi cancelada. Nenhuma alteração realizada.";
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Não foi possível elevar a permissão",
                MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private async Task ExecuteActionAsync(string action)
    {
        if (_working) return;
        SetBusy(true, action == "Install" ? "Instalando a proteção e verificando o resultado…" : "Removendo a proteção de forma reversível…");
        UpdateHeadline(action == "Install" ? "Aplicando a correção…" : "Removendo a correção…",
            "A operação pode alterar temporariamente o estado do serviço AMD.", "EM ANDAMENTO", Warning);
        try
        {
            if (!IsAdministrator()) throw new UnauthorizedAccessException("Esta operação exige direitos de administrador.");
            // The privileged installer stages all scripts and the payload in Program Files.
            CommandResult res = await RunScriptAsync(action, privileged:true, timeoutSeconds:110);
            using JsonDocument? doc = ReadJsonFromOutput(res.Output);
            string result = doc != null ? Text(doc.RootElement, "Result", "Desconhecido") : "Desconhecido";
            if (res.ExitCode != 0 || (action == "Install" && result != "INSTALLED_AND_TESTED") ||
                (action == "Uninstall" && result != "REMOVED"))
                throw new InvalidOperationException((doc != null ? Text(doc.RootElement, "Reason", result) : res.Error) + "\n" + res.Error);
            ActivityText.Text = action == "Install" ? "Correção instalada e validada." : "Proteção removida; serviço AMD colocado em modo seguro.";
            await RefreshAsync();
        }
        catch (Exception ex)
        {
            UpdateHeadline("Operação não concluída", ex.Message, "ERRO", Error);
            ActivityText.Text = "Verifique o status do serviço antes de tentar novamente.";
            SetBusy(false);
        }
        finally
        {
            SetBusy(false);
        }
    }

    private void Install_Click(object sender, RoutedEventArgs e)
    {
        if (!_compatible || _installed || _working) return;
        MessageBoxResult answer = MessageBox.Show(
            "O monitor precisará executar com privilégios de administrador e alterar seis bytes somente na memória do processo AMD.\n\n" +
            "Este é um projeto experimental. FreeSync, anti-cheat e reinicialização completa ainda exigem validação.\n\nContinuar?",
            "Confirmar instalação", MessageBoxButton.YesNo, MessageBoxImage.Warning);
        if (answer == MessageBoxResult.Yes) BeginElevatedAction("--install");
    }

    private void Remove_Click(object sender, RoutedEventArgs e)
    {
        if (!_installed || _working) return;
        MessageBoxResult answer = MessageBox.Show(
            "O monitor será removido da inicialização e o serviço AMD será desativado para evitar que as gravações retornem.\n\nContinuar?",
            "Confirmar desinstalação", MessageBoxButton.YesNo, MessageBoxImage.Question);
        if (answer == MessageBoxResult.Yes) BeginElevatedAction("--uninstall");
    }

    private void TestWrites_Click(object sender, RoutedEventArgs e)
    {
        if(_installed || !_diagnosticEligible || _working) return;
        MessageBoxResult answer=MessageBox.Show(
            "O serviço AMD está parado. Para medir a falha, o programa o iniciará por aproximadamente 5 segundos e registrará as gravações do processo e do disco C:.\n\n" +
            "Durante o teste, ocorrerão gravações reais no SSD. Ao terminar, o serviço voltará ao estado anterior, inclusive se estava desativado.\n\n" +
            "O Windows solicitará permissão de administrador. Continuar?",
            "Teste temporário de gravação",MessageBoxButton.YesNo,MessageBoxImage.Question);
        if(answer == MessageBoxResult.Yes) BeginElevatedAction("--diagnose");
    }

    private async void Refresh_Click(object sender, RoutedEventArgs e) => await RefreshAsync();
}
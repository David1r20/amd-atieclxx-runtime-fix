# Arquitetura técnica

## Fluxo principal

~~~mermaid
flowchart TD
    A["Windows inicia"] --> B["Tarefa SYSTEM no agendador"]
    B --> C["Monitor .NET 8 sem console"]
    C --> D{"atieclxx.exe encontrado?"}
    D -->|Não| C
    D -->|Sim| E["Identidade por SHA-256"]
    E --> F{"Binário conhecido?"}
    F -->|Sim| G["Leitura dos bytes em RVA 0x21122"]
    G --> H{"Bytes originais?"}
    H -->|Sim| I["Suspender processo"]
    I --> J["Escrever 6 bytes na RAM"]
    J --> K["FlushInstructionCache e retomar"]
    K --> C
    H -->|Já patchado| C
    H -->|Desconhecidos| L["Registrar; não alterar"]
    L --> C
    F -->|Não| M["Medir I/O por 4 segundos"]
    M --> N{"Taxa superior a 150 KB/s?"}
    N -->|Não| O["Manter serviço ativo"]
    N -->|Sim| P["Tentar desativar e parar serviço"]
~~~

## Localização da rotina

O binário original importa CopyFileA de KERNEL32.dll. Há dez chamadas diretas identificadas, mas a chamada ligada à autocópia é a da rotina marcada por CopyEEUClientToUninstallFolder.

A instrução original está em RVA 0x21122, offset de arquivo 0x20522, e consiste em:

    FF 15 78 A5 04 00

Isso corresponde à chamada indireta para a IAT CopyFileA. Ela é substituída **somente na memória do processo** por:

    B8 01 00 00 00 90

Essa sequência corresponde a mov eax,1 seguido de nop: simula sucesso da operação e não realiza a cópia. As outras nove referências diretas a CopyFileA não são alteradas. Essa escolha é específica da versão validada.

Com ASLR, o endereço real é MainModule.BaseAddress + 0x21122. Exemplo observado em laboratório: 0x7FF6189C1122. Não fixar endereços absolutos em outras execuções.

## APIs de memória

O monitor utiliza as seguintes APIs do Windows:

- OpenProcess, ReadProcessMemory e WriteProcessMemory;
- NtSuspendProcess / NtResumeProcess durante a substituição;
- VirtualProtectEx para permitir alteração momentânea na região de código;
- FlushInstructionCache para coerência da execução;
- GetProcessIoCounters para medir I/O em compilações desconhecidas.

Se os bytes ou o SHA-256 não corresponderem ao esperado, não ocorre patch. O watchdog registra o evento e evita tentar as mesmas operações indefinidamente.

## Inicialização e persistência

O Install.ps1 instala o binário próprio em Program Files e cria a tarefa AmdAtieclxxRuntimeFix em SYSTEM, disparada no boot. O monitor pesquisa novos processos a cada 300 ms, mas escreve em disco apenas ao ocorrer evento ou mudança de estado. Ele não usa um processo PowerShell agendado a cada alguns minutos.

O instalador inicia o monitor, reativa o serviço AMD e valida que um cliente real foi patchado antes de declarar sucesso. Se falhar, tenta voltar ao serviço desativado e remover a tarefa.

O Uninstall.ps1 remove a tarefa, para o serviço e devolve seu tipo de inicialização ao estado salvo na instalação.

## Falhas e riscos

O monitor não inicia o serviço por conta própria quando o driver não é conhecido. Em compilação desconhecida com I/O excessivo, tenta retornar ao modo conservador com serviço desativado, documentado no SECURITY.md.

Essa salvaguarda, assim como a execução após boot completo, ainda requer teste integral. O programa não substitui a necessidade de atualizar o driver AMD quando sair correção oficial.

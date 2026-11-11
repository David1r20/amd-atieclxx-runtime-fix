# Arquitetura técnica

## Componentes

A versão atual é um executável C++ nativo para Windows x64. O arquivo [`native/main.cpp`](../native/main.cpp) contém a interface, o diagnóstico, o patch em memória e o monitor de processos.

O programa interage com dois componentes da AMD:

- `atiesrxx.exe`: executável associado ao serviço **AMD External Events Utility**.
- `atieclxx.exe`: processo cliente do serviço; no binário investigado, era responsável por copiar repetidamente o próprio arquivo para `systemprofile\AppData\Local\AMD\atieclxx.exe`.

## Causa identificada

O `atieclxx.exe` importa a função `CopyFileA`, da API do Windows. Na versão validada, a chamada da rotina de autocópia (`CopyEEUClientToUninstallFolder`, identificada durante a análise do binário) está na instrução de RVA `0x21122`, correspondente ao offset `0x20522` no arquivo PE analisado.

| Código | Instrução | Efeito |
|---|---|---|
| `FF 15 78 A5 04 00` | Chamada indireta via tabela de importações | Executa `CopyFileA` |
| `B8 01 00 00 00 90` | `mov eax, 1; nop` | Simula sucesso da chamada sem copiar o arquivo |

A alteração tem exatamente seis bytes e não substitui o executável em disco. Outras chamadas `CopyFileA` no mesmo binário permanecem inalteradas por esse patch.

## Como o patch é aplicado

1. Consulta o caminho de execução do serviço AMD e localiza o cliente `atieclxx.exe` correspondente.
2. Valida o fornecedor, a assinatura digital e o SHA-256 do executável.
3. Localiza o processo `atieclxx.exe` e a base de seu módulo carregado.
4. Calcula o endereço virtual: **base do módulo + `0x21122`**. Isso considera o ASLR do Windows.
5. Lê os bytes no endereço. Se já corresponderem ao patch, não escreve novamente; se forem inesperados, recusa a alteração.
6. Suspende temporariamente o processo por `NtSuspendProcess`, altera a proteção da página por `VirtualProtectEx`, escreve os seis bytes com `WriteProcessMemory` e chama `FlushInstructionCache`.
7. Restaura a proteção da página e retoma o processo com `NtResumeProcess`.

As operações são realizadas exclusivamente na memória do processo. Se o hash do arquivo não for o esperado, o monitor **não aplica o patch**.

## Diagnóstico de gravações

A interface mostra duas medições independentes:

- **Processo AMD:** diferença do contador de `WriteTransferCount` obtido pela API `GetProcessIoCounters`, dividida pelo intervalo da amostra.
- **Disco C: total:** contador `PhysicalDisk(*)\Disk Write Bytes/sec` obtido via PDH para a instância associada a `C:`.

O primeiro é I/O lógico atribuído ao processo. O segundo inclui gravações de todos os processos no disco e **não** identifica exclusivamente escritas da AMD nem o desgaste efetivo da NAND.

O diagnóstico verifica outros executáveis AMD com identidade e assinatura válidas, independentemente do hash usado para o patch. A liberação de diagnóstico **não autoriza** modificar versões desconhecidas.

## Monitor em segundo plano

Ao ativar a correção, o aplicativo:

1. Copia seu executável para `C:\Program Files\AMDAtieclxxNativeFix\AMDAtieclxxFix.exe`.
2. Registra a tarefa do Windows `AMD-atieclxx-NativeFix` para execução no início do sistema, com a conta `SYSTEM`.
3. Executa a tarefa com o argumento `--watch`.
4. Inicia o serviço AMD, aplica o patch ao processo cliente e faz uma medição de I/O para confirmar o resultado.

O monitor consulta novos processos aproximadamente a cada 300 ms e reaplica o patch quando identifica um cliente compatível. Ele não modifica o arquivo original do driver.

Se a ativação falhar, o aplicativo tenta encerrar e remover a tarefa e parar/desativar o serviço AMD para não deixar a autocópia rodando sem proteção.

## Remoção e diagnóstico temporário

**Remover proteção** interrompe e exclui a tarefa nativa, para o serviço AMD e define sua inicialização como desativada. A cópia do executável feita em `Program Files` permanece no disco.

**Testar sem proteção** é uma opção independente. Quando não há monitor ativo e o serviço está parado, pode iniciar o serviço por aproximadamente cinco segundos, medir o problema original e tentar restaurar o estado anterior. Não é obrigatório para ativar a correção.

## Código-fonte

- [Implementação C++](../native/main.cpp)
- [Compilação Windows x64](../native/Build.ps1)
- [Resultados dos testes](TESTS.md)
- [Segurança e recuperação](SECURITY.md)


## Confirmação entre interface e monitor

A aplicação da alteração em RAM ocorre no monitor registrado como `SYSTEM`. Ao encontrar um novo processo `atieclxx.exe` compatível, o monitor verifica os bytes do código e grava uma confirmação vinculada ao PID no arquivo `C:\ProgramData\AMDAtieclxxNativeFix\watch_state.txt`.

A ativação iniciada pela interface aguarda esse sinal do monitor, valida a taxa de escrita do processo e verifica que o serviço e a tarefa continuam em execução. A interface **não precisa** abrir o processo AMD para modificar sua memória. Se o monitor não confirmar a correção, a ativação falha e restaura o estado seguro de serviço desativado.

O arquivo de confirmação é um mecanismo local de coordenação; ele não substitui a validação do hash, dos opcodes nem dos contadores de I/O.

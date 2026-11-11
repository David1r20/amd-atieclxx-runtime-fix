# Implementação nativa

O aplicativo é implementado em C++ com Win32/GDI e APIs do sistema Windows. O mesmo executável fornece a interface gráfica e o monitor que atua em segundo plano.

## Estrutura

- [`main.cpp`](main.cpp): identificação do cliente AMD, medições de I/O, correção em memória, monitor e interface.
- [`Build.ps1`](Build.ps1): compilação da versão portátil para Windows x64.

## Funcionamento

**Identificação do cliente:** o programa consulta o serviço `AMD External Events Utility`, localiza `atiesrxx.exe` e o `atieclxx.exe` correspondente, confere a assinatura Authenticode, os metadados AMD e o hash SHA-256. Executáveis AMD com outros hashes podem ser analisados, mas não modificados.

**Medição:** `GetProcessIoCounters` calcula o volume de escrita atribuído ao processo durante o intervalo. O PDH consulta a taxa de escrita total do disco físico que contém `C:`. Esses indicadores não representam a mesma coisa nem medem diretamente o desgaste da NAND.

**Correção:** o programa encontra a base carregada do módulo `atieclxx.exe` e soma o RVA `0x21122`. Somente se o hash e os seis bytes originais coincidirem com os valores validados, suspende o processo com `NtSuspendProcess`, modifica a página com `VirtualProtectEx`, escreve os novos bytes por `WriteProcessMemory`, limpa o cache de instruções com `FlushInstructionCache` e retoma a execução com `NtResumeProcess`. Não altera o arquivo no DriverStore.

**Monitor automático:** quando o usuário ativa a proteção, o executável é copiado para `Program Files` e registrado em uma tarefa de inicialização do Windows, executada como `SYSTEM` com o argumento `--watch`. O monitor procura novas instâncias do processo e reaplica o patch nos binários suportados.

**Verificação da ativação:** depois de iniciar o monitor e o serviço AMD, o programa confere se o processo foi corrigido e mede seu I/O. Se falhar, tenta excluir a tarefa de monitoramento e desativar o serviço AMD para evitar gravações recorrentes.

**Teste sem proteção:** disponível apenas quando não há monitor e o serviço AMD está parado. Mediante confirmação, ativa o serviço por aproximadamente cinco segundos, registra as taxas e tenta devolver o serviço ao estado anterior.

**Remoção:** encerra e exclui a tarefa de monitoramento; interrompe e desativa o serviço AMD. O executável previamente copiado para `Program Files` não é apagado automaticamente.

## Compilação

Na raiz do repositório:

```powershell
.\native\Build.ps1
```

O ambiente de desenvolvimento precisa do Visual Studio C++ Build Tools e Windows SDK. A compilação utiliza `/MT` para vinculação estática do runtime C.

Saída: `dist-native\AMD-atieclxx-Fix-Native.exe`. O executável validado ocupa aproximadamente 274 KB.

## Documentação

- [Visão geral e instruções de uso](../README.md)
- [Arquitetura e tratamento do patch](../docs/ARCHITECTURE.md)
- [Medições e testes](../docs/TESTS.md)
- [Segurança e recuperação](../docs/SECURITY.md)


## Diagnóstico de falhas de ativação

O executável registra em `C:\ProgramData\AMDAtieclxxNativeFix\activation.log` o estágio de ativação e um código de falha. O aplicativo apresenta um erro específico quando o monitor não inicia, o serviço AMD não fica pronto, a escrita em RAM falha ou a redução de I/O não é confirmada.

A cópia do próprio executável é ignorada quando a origem já é o caminho instalado em `Program Files`. A rotina foi testada em ativação direta a partir desse caminho.


## Confirmação do patch pelo monitor SYSTEM

A interface elevada não tenta mais escrever diretamente na memória do cliente AMD. O monitor que executa sob `SYSTEM` confirma o patch depois de verificar os bytes do processo e registra `PID, estado, erro e PID do monitor` em `C:\ProgramData\AMDAtieclxxNativeFix\watch_state.txt`.

Na ativação, o aplicativo aguarda a confirmação do PID atual. Somente depois mede o I/O do processo e confirma o estado do serviço e da tarefa. Falhas provocam reversão conservadora. Isso evita que restrições de acesso da interface administrativa interrompam uma correção aplicada corretamente pelo monitor SYSTEM.

Validação: ativação com serviço inicialmente desativado, zero bytes de escrita em dez segundos, reinício do serviço com novo PID confirmado e zero bytes de escrita em mais oito segundos.

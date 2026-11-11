# AMD atieclxx Fix

**Ferramenta para diagnosticar e corrigir gravações repetitivas no SSD causadas pelo processo AMD External Events (`atieclxx.exe`) no Windows.**

Em determinadas versões do driver Radeon, o `atieclxx.exe` copia repetidamente seu próprio executável para uma pasta de perfil do sistema. Identificamos a operação responsável e desenvolvemos uma correção que impede essa cópia **sem substituir os arquivos originais da AMD**.

O aplicativo possui interface gráfica, é portátil e reúne diagnóstico, ativação, verificação e remoção da proteção.

## O problema

O processo `atieclxx.exe` faz parte do serviço **AMD External Events Utility**. No ambiente analisado, ele executava continuamente a seguinte operação:

1. Lia seu próprio executável na pasta `DriverStore` do Windows.
2. Copiava o arquivo para:

   ```text
   C:\Windows\System32\config\systemprofile\AppData\Local\AMD\atieclxx.exe
   ```

3. Repetia a cópia aproximadamente uma vez por segundo, mesmo quando o conteúdo dos arquivos era idêntico.

A cópia tinha **677.392 bytes**, e as gravações eram detectáveis por rastreamento de I/O do Windows (ETW). O processo gerava cerca de **0,67 MB/s** de escrita contínua nos testes. Se mantida por 24 horas, essa taxa equivale a aproximadamente **58 GB de escrita lógica por dia**; é uma projeção, não uma medição de desgaste do SSD.

## A solução

Localizamos no código de `atieclxx.exe` uma chamada específica à função `CopyFileA` associada à autocópia. A ferramenta substitui **seis bytes dessa chamada apenas na memória RAM do processo**, fazendo a rotina continuar sem executar a cópia redundante.

Com isso:

- O serviço **AMD External Events Utility pode permanecer em execução**.
- As demais chamadas de cópia do executável não são alteradas por esse patch.
- O arquivo da AMD no `DriverStore` permanece intacto e com sua assinatura original.
- Um monitor reaplica a correção quando o `atieclxx.exe` é iniciado novamente.

A alteração **não é aplicada indiscriminadamente**: antes, o programa verifica a identidade do executável por assinatura e SHA-256, além dos bytes exatos no endereço a ser modificado.

### Detalhe técnico da alteração

Na versão validada, a instrução está no endereço relativo (**RVA**) `0x21122`:

| | Bytes de máquina | Efeito |
|---|---|---|
| Original | `FF 15 78 A5 04 00` | Chamada indireta a `CopyFileA` |
| Corrigido em RAM | `B8 01 00 00 00 90` | Retorna sucesso sem efetuar essa cópia |

O endereço absoluto é calculado a partir da base do módulo carregado, considerando o ASLR do Windows. A rotina de aplicação suspende temporariamente o processo, confirma os bytes originais, altera a proteção da página, escreve a instrução, atualiza o cache de instruções e retoma a execução.

O aplicativo e o monitor foram implementados em **C++ nativo, usando APIs do Windows**, em um único executável de aproximadamente **274 KB**.

## Resultados

Avaliamos a mesma instância de `atieclxx.exe` antes, durante e após a correção, restaurando os bytes originais para confirmar o efeito:

| Condição | Duração | Escrita do processo |
|---|---:|---:|
| Sem correção | 7,04 s | 0,6735 MB/s |
| Com correção em memória | 9,04 s | **0 MB/s** |
| Correção revertida | 5,04 s | 0,6718 MB/s |

O retorno das gravações após a reversão é uma evidência de que a chamada identificada causava o comportamento observado. Também confirmamos gravações físicas no arquivo de destino por ETW.

Com o monitor nativo ativo, reiniciamos o serviço AMD e verificamos que o novo processo foi corrigido automaticamente: **zero bytes adicionais de escrita do processo em uma amostra de oito segundos**.

Essas medições foram feitas em um ambiente específico e não representam garantia para todas as versões do driver. Veja [Testes e evidências](docs/TESTS.md).

## Como usar

1. Acesse a página de [Releases](https://github.com/David1r20/amd-atieclxx-runtime-fix/releases) e baixe `AMD-atieclxx-Fix-Native.exe`.
2. Abra o executável no Windows.
3. Consulte o estado do serviço, a compatibilidade e as taxas de gravação mostradas na interface.
4. Se o driver for compatível, clique em **Ativar proteção**. O teste sem proteção **não é obrigatório**.
5. Confirme a solicitação de administrador do Windows. O aplicativo iniciará o monitor, aplicará a correção e verificará o resultado.
6. Use **Verificar novamente** para consultar o estado atualizado. Para desativar, clique em **Remover proteção**.

A ativação copia o próprio aplicativo para uma pasta protegida em `Program Files` e cria uma tarefa de inicialização do Windows executada como `SYSTEM`. Essa tarefa mantém o monitor em segundo plano e aplica o patch aos novos processos compatíveis. Não é necessário executar manualmente o programa a cada inicialização.

**Remover proteção** encerra e exclui a tarefa de monitoramento, interrompe o serviço AMD e define sua inicialização como desativada para impedir que as cópias retornem sem o patch. A remoção não apaga automaticamente o executável copiado para `Program Files`.

### Entendendo as informações da interface

| Informação | Significado |
|---|---|
| **Driver AMD** | Indica se o executável foi reconhecido e se há patch validado para seu SHA-256 |
| **Processo AMD** | Taxa de escrita de I/O atribuída ao `atieclxx.exe` |
| **Disco C: total** | Taxa de escrita de todos os processos no disco do Windows que contém `C:` |
| **Serviço AMD** | Estado atual do AMD External Events Utility |
| **Testar sem proteção** | Teste opcional de aproximadamente cinco segundos para observar a falha original |

Se o serviço estiver parado, não há taxa atual do `atieclxx.exe` para medir. O teste opcional inicia o serviço temporariamente **sem o patch**, mede o comportamento e restaura o estado anterior. Os números desse teste não indicam que a proteção ativa esteja gravando no SSD.

Os contadores de escrita do processo e do disco não são equivalentes à medição direta do desgaste físico (TBW/NAND) do SSD.

## Compatibilidade

A identificação e o diagnóstico podem funcionar em **outros SHA-256 de executáveis AMD com assinatura válida**, mas a correção em memória só é permitida na versão cujo código foi analisado e validado.

| Propriedade | Versão atualmente suportada para correção |
|---|---|
| Driver usado na validação | `32.0.32015.7029` |
| GPU do ambiente de teste | AMD Radeon RX 9070 XT |
| SHA-256 de `atieclxx.exe` | `046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066` |
| Arquitetura | Windows x64 |

O modelo da GPU não é o critério de aplicação do patch. **Um executável com outro hash pode ser diagnosticado, mas não será modificado.** Cada nova versão exige análise e testes próprios.

## Compilar a partir do código-fonte

Para compilar a versão nativa, são necessários Visual Studio C++ Build Tools e Windows SDK:

```powershell
.\native\Build.ps1
```

Saída prevista: `dist-native\AMD-atieclxx-Fix-Native.exe`.

O código principal está em [`native/main.cpp`](native/main.cpp), incluindo interface Win32, identificação do driver, medição de I/O, alteração de memória e monitor de inicialização. O script de compilação está em [`native/Build.ps1`](native/Build.ps1).

## Limitações e cuidados

- A correção é **experimental e independente da AMD**.
- O executável distribuído ainda **não possui assinatura digital própria**.
- A ativação exige privilégios administrativos e cria uma tarefa com conta `SYSTEM`.
- Alterar código na memória de outro processo pode causar incompatibilidade com soluções de segurança, anti-cheat ou funcionalidades do driver.
- Ainda faltam testes abrangentes após reinicialização completa do Windows, uso prolongado, FreeSync e outros computadores.
- Se a ativação não passar nas verificações, a rotina tenta remover a tarefa e manter o serviço AMD parado/desativado por segurança. Isso pode afetar funções dependentes desse serviço.
- O aplicativo não inclui nem redistribui executáveis proprietários da AMD.

Para detalhes de implementação e recuperação, consulte [Arquitetura técnica](docs/ARCHITECTURE.md) e [Segurança](docs/SECURITY.md).


### Se a ativação falhar

O aplicativo desfaz a tentativa e indica **qual etapa falhou**, com um código de diagnóstico. O registro técnico fica em:

`C:\ProgramData\AMDAtieclxxNativeFix\activation.log`

Esse arquivo registra a etapa da ativação e eventuais códigos do Windows; não substitui a verificação do estado real do serviço. Se a proteção não for confirmada, o programa tenta interromper e desativar o serviço AMD para evitar novas gravações sem correção.

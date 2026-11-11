# Testes e evidências

Os resultados abaixo são medições realizadas durante o desenvolvimento, em uma máquina com **AMD Radeon RX 9070 XT**, driver `32.0.32015.7029` e Windows build 29698.

SHA-256 do `atieclxx.exe` testado:

```text
046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066
```

## Identificação da gravação repetitiva

O `atieclxx.exe` lia seu executável e sobrescrevia uma cópia em:

```text
C:\Windows\System32\config\systemprofile\AppData\Local\AMD\atieclxx.exe
```

O arquivo tinha 677.392 bytes e o mesmo SHA-256 do executável de origem. O comportamento se repetia aproximadamente uma vez por segundo. O rastreamento ETW identificou gravações físicas para esse arquivo, além das operações lógicas do processo.

## Teste de causalidade: original → corrigido → original

A mesma instância do processo, sem reiniciá-la, foi medida em três condições:

| Condição | Intervalo | Taxa de escrita do `atieclxx.exe` |
|---|---:|---:|
| Chamada original de `CopyFileA` | 7,04 s | 0,6735 MB/s |
| Chamada substituída em RAM | 9,04 s | **0 MB/s** |
| Bytes originais restaurados | 5,04 s | 0,6718 MB/s |

O retorno da taxa anterior após a restauração do código é evidência de que a chamada localizada estava diretamente relacionada às gravações recorrentes.

## Monitor automático nativo

Com a versão C++ e a tarefa de monitoramento habilitadas:

- O serviço AMD permaneceu em execução.
- O reinício manual do serviço produziu uma nova instância de `atieclxx.exe`, corrigida automaticamente pelo monitor.
- O novo processo registrou **0 bytes adicionais de escrita** em oito segundos.
- Depois de uma atualização do próprio monitor nativo, o serviço e a tarefa continuaram em execução e foi registrada outra amostra com **0 bytes de escrita em sete segundos**.

Essas amostras demonstram o funcionamento observado no ambiente de teste, mas não substituem uma avaliação de horas ou dias.

## Diagnóstico com o serviço desativado

A rotina de diagnóstico temporário foi testada com o serviço AMD inicialmente parado/desativado. O programa o iniciou, mediu a atividade durante aproximadamente cinco segundos e restaurou o estado anterior.

Em uma amostra da interface:

| Contador | Taxa |
|---|---:|
| I/O de escrita do processo AMD | 0,670 MB/s |
| Escrita total do disco C: | 0,795 MB/s |

A escrita total do disco inclui outros processos. Sua diferença em relação à escrita atribuída ao `atieclxx.exe` não constitui uma medida de sobrecarga exclusivamente do driver.

## Compatibilidade e interface

- Foi verificada a rejeição do patch quando o SHA-256 não corresponde ao executável validado.
- O diagnóstico com um SHA-256 diferente foi testado por **simulação de hash**, mantendo o executável AMD real assinado. A compatibilidade com outro binário AMD real ainda precisa de testes.
- A interface foi aberta e capturada no Windows com a proteção ativa e com o serviço parado.
- Foi validada a habilitação de **Ativar proteção** para um driver compatível mesmo quando não há amostra prévia de escrita. **Testar sem proteção** permanece opcional.

## Limites dessas evidências

- Contadores de escrita do processo representam I/O lógico; ETW e contadores do disco fornecem observações distintas. Nenhum deles, isoladamente, mede o desgaste de células NAND.
- Os testes foram feitos em uma configuração específica. Não foi demonstrada compatibilidade universal.
- Não foram concluídos testes amplos de reinicialização completa do Windows, sessões prolongadas, suspensão/hibernação, FreeSync, jogos com anti-cheat e alterações futuras do driver.
- Rastreamentos ETW brutos e dados locais não são publicados por poderem conter caminhos e identificadores do computador.

Os detalhes da implementação estão em [Arquitetura técnica](ARCHITECTURE.md).

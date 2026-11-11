# Evidências e protocolo de teste

Plataforma: RX 9070 XT, Windows build 29698, atieclxx.exe SHA-256 046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066.

## Diagnóstico físico de gravação

ETW registrou 5,44 MB de gravações físicas em curto período para o caminho systemprofile\AppData\Local\AMD\atieclxx.exe. O processo copiava a si mesmo aproximadamente uma vez por segundo. Arquivo de origem e cópia possuem 677.392 bytes e hash idêntico.

## A-B-A no mesmo processo

PID 25084, comparando original, patch na memória, restauração dos seis bytes originais:

| Fase | Duração | I/O escrita do processo |
|---|---:|---:|
| Original A | 7,04 segundos | 0,6735 MB/s |
| Modificado B | 9,04 segundos | 0 MB/s |
| Original A restaurado | 5,04 segundos | 0,6718 MB/s |

O retorno das escritas após restaurar a instrução é evidência causal da função no loop medido.

## Aplicativo .NET 8

Aplicação standalone --once: original 0,6713 MB/s; com patch 0 MB/s por sete segundos. Hash do binário AMD de sistema permaneceu intacto.

Monitor --watch iniciado antes do serviço AMD: detectou cliente, aplicou patch e contabilizou zero bytes adicionais de escrita por sete segundos.

Instalação por tarefa SYSTEM: serviço AMD Running/Auto, processo Patched, zero bytes adicionais em sete segundos.

Reinício de serviço AMD (sem reiniciar PC): novo PID 27304, patch reaplicado na primeira tentativa, zero bytes adicionais em oito segundos.

Remoção: tarefa excluída, serviço AMD restaurado para Stopped/Disabled; reinstalação v0.2.0 validada com zero bytes em sete segundos.

Recursos: working set do monitor de cerca de 23 MB e memória privada de cerca de 9,7 MB em medição pontual.

## Testes pendentes

- Reinicialização completa do Windows e verificação do agendador no boot;
- Uptime prolongado e suspensão/hibernação;
- Atualização real do driver AMD e proteção fallback (teste numérico aprovado, não integração em driver diferente);
- FreeSync, hotkeys, frame pacing, jogos e anti-cheat;
- Consumo de CPU/RAM sob múltiplas cargas;
- Telemetria SMART de durabilidade do SSD.

Contadores de I/O por processo não são sinônimos de TBW físico. ETW corroborou o efeito original no disco; os demais testes mediram sobretudo o processo. Capturas ETW locais contêm caminhos privados: não publicar dados brutos.


## Testes adicionais após preparação do pacote

- Reinício manual da tarefa do agendador e em seguida do serviço AMD: PASS; novo processo PID 19944, patch reconhecido, 0 bytes de I/O adicionais em seis segundos.
- Teste reproduzível tests/Test-ServiceRestart.ps1 -AllowServiceRestart: PASS; novo processo PID 27836, código patchado, 0 bytes de I/O adicionais em sete segundos, tarefa Running.
- App v0.2.0 (SHA256 do monitor ADB0D7220C030D7FEAB7283A7C296463DCC36018635B219A1BDA6F8E2CC9E060) instalado e ativo, driver AMD original imutado.


## Distribuição portátil (11/11/2026)

- Publicação dotnet --self-contained true, single-file, win-x64: 67.540.600 bytes; --status executou corretamente no PC de validação.
- O hash definitivo do ZIP sera informado separadamente nos metadados do release, evitando alterar o proprio ZIP ao atualiza-lo.
- Comando de diagnóstico identifica automaticamente a pasta do binário do serviço, valida assinatura e hash e mede I/O durante 3 segundos.
- Controle negativo com hash incorreto: compatible=false, recusa no-op.
- Controle de instalação duplicada: refused, sem alterar serviço/processo/task no PC.
- Integridade: 18 arquivos hash correto, 0 erros de parsing PowerShell, 0 binários AMD proprietários incluídos.
- Lacuna: instalação do **ZIP portátil** em outra máquina ainda não foi validada de ponta a ponta; boot completo/FreeSync/anti-cheat idem.


## Testes da interface beta 0.3.1 (11/11/2026)

**Regressão 1 — serviço AMD parado**: sem processo atieclxx.exe não há amostra. A GUI agora indica Serviço AMD parado, em vez de representar erroneamente zero MB/s ou sugerir que a falha não existe.

**Regressão 2 — teste temporário e reversível**: executou com hash compatível, serviço inicialmente Stopped/Disabled e tarefa ausente; mediu 5,06 s e I/O do processo 1,472 MB/s, total físico do disco C: 2,080 MB/s, e confirmou serviço Stopped/Disabled e processo encerrado ao término.

**Regressão 3 — GUI com teste**: executável único iniciado com diagnóstico, capturado visualmente após a medição: PROCESSO AMD 0,670 MB/s, DISCO C: TOTAL 0,795 MB/s, serviço Parado, patch não instalado. O script restaurou o estado original. Variações entre amostras são esperadas.

**Regressão 4 — cor do botão**: botão Verificar novamente foi acionado via automação da janela e capturado durante análise. O estilo customizado manteve o fundo grafite, sem botão branco do tema do Windows.

O relatório de I/O de processo é contabilidade lógica, não medida de desgaste da NAND. O contador do disco representa todas as gravações do disco C:, não somente aquelas atribuíveis ao atieclxx.exe.

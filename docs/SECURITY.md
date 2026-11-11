# Segurança, riscos e recuperação

## O que é alterado

A tarefa agendada executa o monitor em C:\Program Files\AmdAtieclxxRuntimeFix sob SYSTEM. Ele verifica hash e bytes do processo AMD antes de escrever somente seis bytes **na RAM** de atieclxx.exe. Nenhum binário do DriverStore é substituído; não injeta DLL e não desativa a assinatura digital do Windows.

## Riscos conhecidos

- Um monitor com SYSTEM é altamente privilegiado. O diretório de instalação precisa impedir modificações por usuários comuns.
- Interferência na memória de processos pode ser detectada por antivírus e anti-cheat ou causar instabilidade.
- A correção foi medida quanto ao I/O, mas recursos AMD como FreeSync e frame pacing não receberam teste completo.
- Se um novo driver AMD produzir mais de 150 KB/s por quatro segundos, a v0.2.0 tenta desativar o serviço como fallback; isso pode interferir nas funções do cliente.
- Versões desconhecidas nunca recebem os seis bytes de patch sem uma nova análise técnica.
- Não há conexão de rede nem telemetria externa.

## Remoção normal

Abra PowerShell como administrador e execute:

~~~powershell
& 'C:\Program Files\AmdAtieclxxRuntimeFix\Uninstall.ps1'
~~~

A tarefa é removida e o serviço volta ao estado salvo no momento de instalação. No PC testado, o estado era Stopped/Disabled.

## Recuperação emergencial

~~~powershell
Stop-ScheduledTask -TaskName 'AmdAtieclxxRuntimeFix' -ErrorAction SilentlyContinue
Unregister-ScheduledTask -TaskName 'AmdAtieclxxRuntimeFix' -Confirm:$false
Stop-Service -Name 'AMD External Events Utility' -ErrorAction SilentlyContinue
Set-Service -Name 'AMD External Events Utility' -StartupType Disabled
~~~

Isso suspende as funções do serviço AMD, mas mantém o SSD protegido. Nenhuma limpeza ou alteração de arquivos do DriverStore é necessária.

## Limitações de divulgação

Não publicar executáveis AMD, originais ou alterados, dumps de ETW, caminhos particulares, dados do computador ou análises binárias integrais contendo material proprietário. O repositório deverá conter apenas código de monitor próprio, scripts e documentação.

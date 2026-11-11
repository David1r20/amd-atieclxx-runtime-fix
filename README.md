# AMD atieclxx Runtime Fix

**Correção experimental, não oficial, para gravações repetitivas no SSD causadas pelo cliente AMD External Events (atieclxx.exe).**

O projeto oferece um instalador gráfico único para Windows. Ele diagnostica a taxa de escrita de diferentes versões assinadas do cliente AMD e, **somente nas versões cujo código foi validado**, aplica uma correção de seis bytes na memória do processo. Os arquivos originais assinados da AMD permanecem inalterados.

> **Estado:** beta 0.3.2, repositório inicialmente privado para revisão e testes. Não é produto da AMD, nem possui suporte oficial. Não é uma correção universal de drivers Radeon.

## Como funciona

1. Detecta o serviço AMD External Events Utility e identifica atieclxx.exe no diretório correspondente.
2. Confere a assinatura Authenticode e as informações do fornecedor AMD.
3. Mede a taxa de escrita do processo e, quando disponível, a taxa total de gravação do disco do Windows que contém C:.
4. Se o serviço estiver parado, oferece diagnóstico temporário: mediante consentimento e UAC, inicia o serviço por aproximadamente cinco segundos, mede e restaura seu estado.
5. Apenas quando o SHA-256 exato é reconhecido, permite instalar um monitor que altera uma chamada CopyFileA **na RAM** do processo.
6. A tarefa SYSTEM em segundo plano reaplica a correção quando o cliente AMD reinicia. O usuário pode verificar e desinstalar pela interface.

**Importante:** a escrita por processo é um contador de I/O lógico; a escrita do disco C: inclui outros processos. Nenhum deles, isoladamente, mede o desgaste NAND/TBW físico do SSD.

## Compatibilidade

| Verificação | Outros SHA-256 AMD | SHA-256 validado |
|---|---|---|
| Reconhecer AMD assinado | Sim | Sim |
| Medir escrita com serviço ativo | Sim | Sim |
| Medir serviço parado (teste temporário) | Sim, com UAC | Sim, com UAC |
| Corrigir o código do processo | **Não** | Sim, se houver gravação anormal |
| Atualizar versões automaticamente | Não | Não |

O único binário atualmente validado para o patch tem SHA-256:

    046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066

Identificado no laboratório com driver AMD 32.0.32015.7029, na Radeon RX 9070 XT. O patch é por identidade do executável, não por modelo de placa. Outros SHA-256 podem ser diagnosticados, mas exigem análise antes de receber correção. Nenhum binário de driver AMD está neste repositório.

## Resultados medidos

Durante uma comparação A–B–A no mesmo processo, a taxa de escrita do atieclxx.exe caiu de aproximadamente **0,6735 MB/s para 0 MB/s** após o patch e retornou a **0,6718 MB/s** ao restaurar a instrução. A medição foi acompanhada por ETW e contadores do Windows.

O teste temporário do instalador gráfico registrou, em outra amostra, **0,670 MB/s de escrita do processo** e **0,795 MB/s de gravações totais no disco C:**. O serviço voltou ao estado Stopped/Disabled. As taxas dependem da carga e não são garantias universais.

Consulte docs/TESTS.md.

## Compilar o aplicativo

Requisitos para desenvolvedores: Windows x64, PowerShell 5.1, SDK .NET 8.

    .\scripts\build.ps1

O script compila o monitor e incorpora seu executável self-contained ao instalador WPF. Saída: dist\AMD-atieclxx-Fix-Setup.exe.

**O usuário final não precisa instalar .NET nem compilar nada.** O instalador inclui os componentes necessários. O Windows solicita UAC somente em operações administrativas.

Estrutura:

    Program.cs                          Monitor e patch em memória
    AmdAtieclxxRuntimeFix.csproj        Projeto .NET do monitor
    SetupGui/                            Interface gráfica WPF
    Distribution/                        Scripts internos auditáveis
    scripts/build.ps1                    Build reproduzível
    tests/Test-ServiceRestart.ps1        Regressão opt-in
    docs/                                Arquitetura, segurança, evidências
    .github/workflows/build.yml          CI Windows

## Uso

Abra o instalador gráfico (quando um release privado for disponibilizado) e clique em **Verificar novamente**. Se o AMD estiver parado, pode clicar em **Medir gravações** e autorizar o teste temporário. Se detectar o problema em um SHA-256 suportado, oferecerá **Instalar proteção**. A desinstalação é feita na mesma janela.

Não desligue Defender, SmartScreen, Secure Boot nem assinatura de drivers. O instalador ainda não tem assinatura digital própria e poderá produzir avisos; isso não deve ser contornado desativando a segurança do Windows.

## Segurança e limitações

- O monitor opera sob SYSTEM para alterar a memória de um processo AMD validado.
- O arquivo assinado da AMD e DriverStore **não são substituídos**.
- Builds desconhecidos são **somente diagnosticados**. Após atualização do driver, o monitor possui um fallback que pode desativar o serviço se detectar I/O elevado no novo binário. Consulte docs/SECURITY.md.
- Validações após boot completo, uso prolongado, FreeSync, hotkeys e anti-cheat em PCs diferentes permanecem pendentes.
- A instalação inicial do Setup gráfico em outro PC ainda não foi validada integralmente.
- Não redistribuímos executáveis, dumps, logs privados ou disassembly integral da AMD.

## Publicação

Este repositório começou como **privado** para revisão técnica. Não há versão pública ou release oficial neste estágio. A licença deverá ser escolhida antes de uma eventual publicação pública.

## English summary

Unofficial Windows utility for diagnosing AMD atieclxx.exe repetitive SSD writes. Diagnosis works on other validly signed AMD client versions, but the six-byte in-memory fix is strictly version-locked using SHA-256 and opcode validation. A self-contained WPF installer detects, measures, installs, verifies and removes the monitor without replacing signed AMD driver files. Experimental, privileged, not supported by AMD and not yet validated across machines.

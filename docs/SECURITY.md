# Segurança e recuperação

O AMD atieclxx Fix modifica **apenas a memória RAM do processo `atieclxx.exe`** para impedir uma operação de cópia repetitiva. O executável assinado da AMD no disco não é alterado.

## Permissões utilizadas

O diagnóstico normal consulta informações do serviço, assinatura digital, hash e contadores de I/O.

A ativação da proteção exige autorização administrativa porque precisa:

- Copiar o executável da ferramenta para `C:\Program Files\AMDAtieclxxNativeFix\AMDAtieclxxFix.exe`.
- Criar a tarefa `AMD-atieclxx-NativeFix` no Agendador de Tarefas do Windows.
- Executar o monitor em segundo plano com a conta `SYSTEM`.
- Ler e alterar os seis bytes validados no processo AMD em execução.

O teste temporário do serviço parado e a remoção da proteção também exigem privilégios de administrador.

## Restrições da correção

- O patch está limitado ao SHA-256 `046C13260AEA96D5B432A40A407F2D4B94D5333B9BDA2AA6E1B381C15D03B066`.
- Os seis bytes esperados são verificados no processo antes de qualquer alteração.
- Um hash desconhecido **não recebe patch**, mesmo que o diagnóstico identifique gravação excessiva.
- O programa não substitui executáveis ou arquivos de driver AMD em disco.
- O aplicativo não implementa envio de telemetria pela rede.

A validação de assinatura e fornecedor para diagnóstico não equivale à validação do código necessário para uma nova versão de patch.

## Riscos e limitações

- Modificar a memória de outro processo pode ser detectado por antivírus ou anti-cheat e pode provocar instabilidade em determinados ambientes.
- O monitor tem privilégios elevados; execute somente binários de origem confiável e proteja a pasta de instalação contra escrita por usuários comuns.
- O executável de distribuição ainda não possui assinatura digital própria. Não é necessário desativar Defender, SmartScreen, Secure Boot ou validação de drivers para utilizá-lo.
- Uma falha na ativação pode deixar o serviço AMD parado/desativado como medida de proteção contra as gravações originais.
- Desativar o serviço AMD pode afetar recursos que dependam dele. O impacto em FreeSync, atalhos, jogos e outros recursos ainda não foi avaliado de maneira abrangente.
- Uma atualização do driver pode alterar o SHA-256 e impedir a aplicação do patch. Isso é uma restrição deliberada de compatibilidade.

## Remover proteção pela interface

Abra o aplicativo e selecione **Remover proteção**. A operação encerra e exclui a tarefa de monitoramento, para o serviço AMD e define sua inicialização como desativada para evitar o retorno da autocópia sem patch.

A cópia do executável instalada em `Program Files` **não é excluída automaticamente**.

## Recuperação manual

Caso a interface não consiga remover a tarefa, execute em um terminal administrativo:

```powershell
schtasks.exe /End /TN "AMD-atieclxx-NativeFix"
schtasks.exe /Delete /TN "AMD-atieclxx-NativeFix" /F
sc.exe stop "AMD External Events Utility"
sc.exe config "AMD External Events Utility" start= disabled
```

Esses comandos mantêm o serviço AMD desativado. A reativação manual do serviço sem o monitor pode fazer as gravações repetitivas voltarem.

## Escopo da publicação

O repositório inclui somente o código da ferramenta e a documentação. Não devem ser enviados arquivos proprietários da AMD, executáveis alterados, dumps de memória, rastreamentos ETW com dados locais ou credenciais.

Este é um projeto experimental independente, sem vínculo ou suporte oficial da AMD.

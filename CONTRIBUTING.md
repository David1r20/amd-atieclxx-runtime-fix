# Contribuições / Contributions

Este repositório está inicialmente **privado** e em beta. A publicação pública, a licença e a política de contribuições ainda não foram definidas.

## Suporte a outros drivers AMD

Não adicione hashes automaticamente sem demonstrar que os opcodes, o callsite e o comportamento são equivalentes. Para um novo binário, registre:

1. SHA-256 e versão do executável AMD assinado;
2. comprovação independente do loop de escrita (I/O por processo e, quando possível, ETW);
3. evidência do callsite específico, com contexto de código suficiente;
4. teste A–B–A do patch em RAM;
5. checagem de que o serviço permanece operacional e não interfere em outros recursos.

Não envie binários AMD, dumps de memória, ETW com dados pessoais ou credenciais.

## Código

- O monitor deve falhar com segurança em opcodes desconhecidos.
- O diagnóstico de versões não validadas deve permanecer separado da escrita na memória.
- A interface deve funcionar sem SDK .NET no PC do usuário final.
- Instalação e desinstalação devem ser reversíveis e pedir autorização explícita.

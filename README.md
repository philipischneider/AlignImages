# Align Images

Ferramenta desktop em C++ para alinhamento iterativo de pilhas de imagens 2D do Visible Human Project e de outras modalidades relacionadas.

O projeto foi desenhado para Windows e hoje já cobre:

- carregamento de duas pilhas completas
- `Stack A` como referência e `Stack B` como pilha móvel
- timeline dupla com deslocamento visual entre stacks
- viewers de referência, móvel e preview
- modos de preview `blend`, `checkerboard`, `difference` e `multiply`
- zoom e pan por mouse nos viewers
- alinhamento automático inicial
- landmarks manuais
- análise de convergência
- batch assíncrono com progresso e cancelamento
- salvamento e reabertura de sessão em JSON
- exportação nas direções móvel -> referência e referência -> móvel

## Estrutura

- [TECH_SPEC.md](TECH_SPEC.md): especificação técnica consolidada
- [MVP_DESIGN.md](MVP_DESIGN.md): desenho do MVP e roadmap
- `src/`: código-fonte principal
- `third_party/`: dependências vendorizadas

## Stack técnica

- C++20
- CMake
- Dear ImGui
- GLFW
- OpenGL
- OpenCV
- nlohmann/json

## Build no Windows

O projeto usa CMake e foi testado com build fora da pasta do OneDrive para evitar problemas de lock e permissões.

Exemplo:

```powershell
cmake -S . -B C:\temp\AlignImagesBuild -DOpenCV_DIR=C:/vcpkg/installed/x64-windows/share/opencv4
cmake --build C:\temp\AlignImagesBuild --config Debug
```

Executável esperado:

```text
C:\temp\AlignImagesBuild\Debug\AlignImages.exe
```

## Estado atual

O software já está em fase testável, com foco atual em:

- estabilização de UI e workflow
- melhoria de performance do motor de registro
- ampliação dos métodos automáticos de alinhamento

## Próximos passos naturais

- otimização do motor de registro
- estratégias automáticas adicionais
- processamento assíncrono para mais operações pesadas
- acabamento de usabilidade na timeline e nos viewers

# Align Images

Ferramenta desktop em C++ para alinhamento iterativo de pilhas de imagens 2D do Visible Human Project e de outras modalidades relacionadas.

O projeto foi desenhado para Windows e hoje já cobre:

- carregamento de duas pilhas completas
- `Stack A` como referência e `Stack B` como pilha móvel
- timeline unificada com as duas pilhas em um único painel rolável
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

O projeto usa CMake com presets definidos em `CMakePresets.json`. O diretório de build fica sempre em `build/` dentro da raiz do repositório.

### Configurar e compilar (primeira vez)

```powershell
cmake --preset default          # configura Release em build/
cmake --build --preset release  # compila Release
```

Para Debug:

```powershell
cmake --preset debug            # configura Debug em build/ (mesmo dir)
cmake --build --preset debug    # compila Debug
```

### Compilar incrementalmente (já configurado)

```powershell
cmake --build build --config Release
cmake --build build --config Debug
```

Executáveis gerados:

```text
build\Release\AlignImages.exe
build\Debug\AlignImages.exe
```

## Estado atual

O software já está em fase testável. Melhorias recentes:

**Motor de registro (abril 2026)**

- dados da imagem de referência pré-computados uma única vez por chamada (gradiente Sobel, área da máscara), eliminando ~79 recálculos redundantes por iteração
- pirâmide de resolução: nível coarse opera em 1/4, médio em 1/2 e fino em resolução completa
- early exit quando o score de máscara é negligível, evitando cálculo de gradiente desnecessário
- avaliação dos 80 candidatos por iteração em paralelo (`std::execution::par_unseq`)
- `cv::moments()` calculado uma única vez em `ComputeMaskStats` (era calculado duas vezes)
- structuring elements morfológicos estáticos (`static const`), criados apenas na primeira chamada

**Timeline (abril 2026)**

- Stack A e Stack B unificados em um único painel com uma barra de rolagem horizontal compartilhada, eliminando a necessidade de rolar verticalmente para ver a segunda pilha

## Próximos passos naturais

- estratégias automáticas adicionais de alinhamento
- processamento assíncrono para mais operações pesadas
- acabamento de usabilidade nos viewers

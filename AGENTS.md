# AGENTS.md — Align Images

## Escopo

Estas instruções valem para toda a árvore `Align_Images/` e complementam o
`AGENTS.md` da raiz. Este projeto é o aplicativo desktop C++ usado para alinhar
duas pilhas de imagens 2D.

Antes de alterar o código, leia:

- `README.md` para uso e compilação;
- `TECH_SPEC.md` para arquitetura e contratos;
- `../HISTORICO_TECNICO.md` para o histórico do Visible Human Project.

## Estado atual

- Aplicativo: `AlignImages`, versão de projeto `0.1.0`.
- Executável Release mais recente conhecido:
  `build/Release/AlignImages.exe`, compilado em 12/05/2026 às 16:34.
- O executável `build/CMakeFiles/.../CompilerIdCXX.exe` não é o aplicativo.
- Branch atual: `main`.
- Último commit observado: `654286b`, de 12/05/2026, com interpolação entre
  cortes-âncora e exportação de preview animado.

Há alterações locais não commitadas que pertencem ao usuário e devem ser
preservadas:

- `src/registration/LandmarkRegistration.cpp`;
- `src/registration/TransformInterpolator.cpp`;
- `src/ui/MainWindow.cpp`;
- `IMPLEMENTATION_PLAN.md` não rastreado;
- `scripts/` não rastreado.

Essas alterações tornam landmarks explícitos os únicos âncoras de interpolação,
limpam `isInterpolated` após novo ajuste manual e melhoram as mensagens da UI.
Não reverta, substitua ou formate esses arquivos de modo destrutivo.

## Stack e build

- Windows;
- C++20;
- CMake 3.21+;
- MSVC;
- OpenCV;
- OpenGL;
- GLFW;
- Dear ImGui vendorizado em `third_party/imgui`;
- nlohmann/json;
- dependências instaladas preferencialmente por vcpkg em `C:/vcpkg`.

Configuração e compilação Release:

```powershell
cmake --preset default
cmake --build --preset release
```

Compilação incremental:

```powershell
cmake --build build --config Release
```

Debug:

```powershell
cmake --preset debug
cmake --build --preset debug
```

Os presets Release e Debug compartilham o diretório `build/`. Após mudanças,
confirme a data de `build/Release/AlignImages.exe` e não considere apenas a data
dos arquivos gerados pelo CMake.

## Arquitetura

```text
src/
  app/           ciclo do aplicativo e contexto
  core/          tipos básicos, matriz 2D, DPI e diálogos Windows
  data/          sessão, pilhas, pareamento e resultados de registro
  export/        aplicação das transformações e escrita de imagens
  io/            varredura, carregamento e serialização JSON
  registration/ registro automático, landmarks, convergência e interpolação
  timeline/      timeline unificada das duas pilhas
  ui/            janela e fluxo principal
  viewer/        visualização, textura, zoom e pan
```

Mantenha a lógica de domínio fora de `MainWindow.cpp` sempre que possível.
Operações de registro pertencem a `registration/`, persistência a `io/` e
exportação a `export/`.

## Convenções de alinhamento

- Stack A é a pilha fixa/de referência.
- Stack B é a pilha móvel.
- `fixedIndex` identifica o corte da referência.
- `movingIndex` identifica o corte móvel pareado.
- `forward_matrix_3x3` transforma **móvel → referência**.
- `inverse_matrix_3x3` transforma **referência → móvel**.
- As matrizes são armazenadas em ordem de linha, com nove números.
- `ExportAlignedMovingToFixed` usa `forward` diretamente com
  `cv::warpAffine`, sem `WARP_INVERSE_MAP`.
- `ExportAlignedFixedToMoving` usa `inverse` diretamente.
- O tamanho da imagem exportada é o tamanho da pilha de destino.

Não troque o sentido das matrizes nem acrescente `WARP_INVERSE_MAP` sem rever
todos os pontos de preview, exportação, serialização e scripts consumidores.

Para imagens contínuas, a exportação do aplicativo usa `cv::INTER_LINEAR`. Se
o consumidor aplicar as matrizes a máscaras binárias, ele deve usar
nearest-neighbor fora do aplicativo para não criar rótulos intermediários.

## Sessões JSON

`SessionSerializer` preserva:

- projeto, versão e fase do workflow;
- diretórios, metadados e cortes das duas pilhas;
- offsets e pares fixo/móvel;
- transformação direta e inversa;
- tipo de transformação (`similarity` ou `affine`);
- score, convergência, priors e outliers;
- landmarks e erro RMS manual;
- iterações, histórico e log de operações;
- preferências de UI e de registro.

Ao alterar o formato:

1. mantenha leitura compatível com campos ausentes usando valores padrão;
2. atualize `Save` e `Load` juntos;
3. preserve sessões existentes;
4. teste uma rodada salvar → carregar → salvar;
5. não altere silenciosamente o significado de campos existentes.

A sessão relevante ao trabalho com os cortes inferiores da CT está fora deste
repositório:

```text
C:\Users\amont\OneDrive\Mestrado PPGTIC\Arquivos de Desenvolvimento\
Head_DG\TC_tratado_ALinhado\session_autosave.json
```

Trate-a como dado do usuário: não sobrescreva. Use uma cópia para testes.

## Landmarks e interpolação

- Um ajuste por landmarks deve marcar `isManual = true` e
  `isInterpolated = false`.
- No estado local atual, somente registros manuais com landmarks explícitos e
  não interpolados funcionam como âncoras.
- Resultados automáticos e resultados propagados não são âncoras.
- A interpolação precisa de pelo menos duas âncoras válidas.
- Não sobrescreva cortes com landmarks explícitos.
- Ao interpolar, mantenha consistência entre matrizes direta/inversa,
  `transformType`, flags e histórico.

## Particularidade do conjunto Visible Human

Na CT utilizada neste projeto há uma mudança de campo de visão entre os cortes
235 e 236. O aplicativo pode representar matrizes específicas por corte, mas
isso não torna qualquer sequência de matrizes intercambiável com o alinhamento
físico DICOM.

Ao trabalhar nessa região:

- confira obrigatoriamente 234, 235, 236, 237, 240, 250, 260 e 264;
- procure continuidade de escala, posição e anatomia entre 235 e 236;
- valide contra a pilha fixa Digital, não apenas contra outra imagem derivada
  pela mesma transformação;
- preserve a sessão original e exporte para diretório novo.

Correspondência usada no projeto principal:

```text
c_vm1006 -> Head005
c_vm1265 -> Head264
Head = c_vm - 1001
```

## Regras para mudanças

1. Inspecione `git status` e `git diff` antes de editar.
2. Preserve mudanças locais e arquivos não rastreados do usuário.
3. Faça alterações pequenas e coerentes com a arquitetura existente.
4. Não edite `third_party/` salvo se a tarefa exigir atualização explícita da
   dependência.
5. Não apague `build/` nem reconfigure dependências globalmente sem necessidade.
6. Não sobrescreva sessões, pilhas de entrada ou exportações aceitas.
7. Novas operações devem ser canceláveis se forem pesadas e devem registrar
   resultado no histórico/auditoria quando modificarem alinhamentos.
8. Mensagens de UI devem informar o motivo concreto de falha e o número de
   pares/âncoras afetados quando aplicável.
9. Preserve compatibilidade com caminhos Windows e nomes contendo espaços.
10. Não afirme que o alinhamento está correto apenas porque o build passou.

## Verificação mínima

Após alterações no código:

1. compile Release;
2. confirme que `build/Release/AlignImages.exe` foi atualizado;
3. abra ou gere uma sessão de teste, nunca a sessão original;
4. teste carregamento das duas pilhas e pareamento;
5. teste preview móvel → referência;
6. se afetar landmarks, teste criação, recalculo e persistência;
7. se afetar interpolação, teste duas âncoras e proteção dos cortes-âncora;
8. se afetar exportação, compare visualmente a saída com o preview;
9. se afetar serialização, faça round-trip do JSON;
10. revise `git diff` para garantir que nenhuma mudança do usuário foi perdida.

Não há suíte automatizada registrada no CMake atual. Na ausência de testes,
build Release mais verificação funcional direcionada são obrigatórios.

## Documentação

Atualize `README.md` quando mudar comandos ou comportamento visível e
`TECH_SPEC.md` quando mudar arquitetura, formato de sessão ou semântica das
transformações. Documente decisões específicas do conjunto VHP no histórico da
raiz, não apenas em comentários no código.

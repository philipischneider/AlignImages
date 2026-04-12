# MVP Design - Ferramenta de Alinhamento de Pilhas

## 1. Objetivo do MVP

Entregar uma primeira versao funcional do aplicativo desktop em C++ para Windows capaz de:

- carregar duas pilhas de imagens
- exibir as pilhas em uma timeline dupla
- ajustar o offset global entre elas
- visualizar pares candidatos em viewers sincronizados
- aplicar alinhamento automatico inicial entre pares validos
- salvar sessao e transformacoes em JSON
- permitir inspecao e refinamento basico

O MVP nao precisa resolver todos os casos de uso finais, mas deve estabelecer uma base solida, extensivel e rapida.

## 2. Escopo do MVP

### 2.1 Incluido

- aplicacao desktop Windows
- C++20 + CMake
- Dear ImGui + GLFW + OpenGL
- OpenCV
- nlohmann/json
- carregamento de duas pilhas por diretorio
- timeline dupla com offset global
- viewers com zoom e pan
- modos de preview:
  - blend
  - checkerboard
  - difference
  - multiply
- DPI automatico do Windows com override manual
- persistencia de sessao em JSON
- alinhamento automatico inicial por pares validos
- tipos de transformacao:
  - rigid
  - similarity
- preset inicial:
  - CT x fotografia digital com fundo azul

### 2.2 Fora do MVP inicial

- deformacao nao rigida
- pareamento automatico avancado por Z real
- suporte multi-usuario
- plugins de algoritmo
- landmarks completos com editor refinado
- exportacao em todos os formatos finais

Observacao: o MVP pode deixar ganchos para essas funcoes, mas nao precisa conclui-las.

## 3. Dependencias

## 3.1 Dependencias principais

- Dear ImGui
- GLFW
- OpenGL
- OpenCV
- nlohmann/json

## 3.2 Dependencias opcionais futuras

- fmt
- spdlog
- GoogleTest

## 3.3 Politica de dependencia

Priorizar:

- bibliotecas amplamente usadas
- integracao simples em CMake
- baixo numero de DLLs e complexidade de distribuicao

## 4. Estrutura do Repositorio

Estrutura recomendada:

```text
/
  CMakeLists.txt
  README.md
  docs/
    TECH_SPEC.md
    MVP_DESIGN.md
  third_party/
  src/
    main.cpp
    app/
      Application.h
      Application.cpp
      AppContext.h
    core/
      Types.h
      Result.h
      Transform2D.h
      MathUtils.h
      DpiUtilsWin.h
      DpiUtilsWin.cpp
    data/
      SliceRecord.h
      StackModel.h
      PairingModel.h
      RegistrationResult.h
      SessionModel.h
    io/
      ImageLoader.h
      ImageLoader.cpp
      DirectoryScanner.h
      DirectoryScanner.cpp
      SessionSerializer.h
      SessionSerializer.cpp
      ThumbnailCache.h
      ThumbnailCache.cpp
    preprocess/
      PreprocessPreset.h
      PreprocessPreset.cpp
      BlueMaskExtractor.h
      BlueMaskExtractor.cpp
      CtPhotoPreprocessor.h
      CtPhotoPreprocessor.cpp
    registration/
      RegistrationEngine.h
      RegistrationEngine.cpp
      RegistrationPreset.h
      RegistrationPreset.cpp
      SimilarityTransformSolver.h
      SimilarityTransformSolver.cpp
      MetricCalculator.h
      MetricCalculator.cpp
      ConvergenceState.h
    viewer/
      ImageTexture.h
      ImageTexture.cpp
      ViewportState.h
      ImageCompositor.h
      ImageCompositor.cpp
      ViewerPanel.h
      ViewerPanel.cpp
    timeline/
      TimelinePanel.h
      TimelinePanel.cpp
      TimelineLayout.h
    ui/
      MainWindow.h
      MainWindow.cpp
      Panels.h
      Panels.cpp
      Theme.h
    jobs/
      JobSystem.h
      JobSystem.cpp
    export/
      ExportController.h
      ExportController.cpp
  assets/
    fonts/
  sessions/
```

## 5. Arquitetura do MVP

## 5.1 Visao geral

O aplicativo deve ser dividido em cinco camadas:

- `app`: ciclo de vida, estado global e orquestracao
- `data`: modelos persistiveis de sessao, pilhas e resultados
- `io`: leitura de arquivos e serializacao
- `registration`: pre-processamento e alinhamento
- `ui/viewer/timeline`: renderizacao e interacao

## 5.2 Regras arquiteturais

- UI nao faz IO diretamente
- UI nao executa alinhamento pesado no thread principal
- modulos de registro nao dependem de ImGui
- serializacao deve operar sobre modelos de dados, nao sobre widgets
- viewers e timeline leem estado do modelo central

## 6. Modelo de Dados do MVP

## 6.1 SliceRecord

Campos minimos:

- `int stackIndex`
- `std::string filePath`
- `std::string fileName`
- `int width`
- `int height`
- `bool hasThumbnail`
- `enum SliceStatus`

## 6.2 StackModel

Campos minimos:

- `std::string id`
- `std::string name`
- `std::string modality`
- `std::string directory`
- `std::vector<SliceRecord> slices`

## 6.3 PairingModel

Campos minimos:

- `std::string fixedStackId`
- `std::string movingStackId`
- `int globalOffset`
- `std::vector<PairRecord> pairs`

`PairRecord`:

- `int fixedIndex`
- `int movingIndex`
- `bool valid`
- `enum PairStatus`

## 6.4 RegistrationResult

Campos minimos:

- `int fixedIndex`
- `int movingIndex`
- `Transform2D forward`
- `Transform2D inverse`
- `std::string transformType`
- `double score`
- `bool converged`
- `std::vector<IterationRecord> iterations`

## 6.5 SessionModel

Campos minimos:

- `StackModel stackA`
- `StackModel stackB`
- `PairingModel pairing`
- `std::vector<RegistrationResult> registrations`
- `UiPreferences uiPreferences`
- `ProjectPreferences projectPreferences`

## 7. Contrato Inicial do JSON

JSON de sessao do MVP:

```json
{
  "project": {
    "name": "session_001",
    "version": "0.1.0"
  },
  "ui": {
    "dpi_mode": "auto",
    "dpi_override": 1.5,
    "preview_mode": "blend",
    "blend_alpha": 0.5,
    "checker_size": 32
  },
  "stacks": [
    {
      "id": "stack_a",
      "name": "CT",
      "modality": "ct",
      "directory": "C:/data/ct",
      "slices": [
        {
          "index": 0,
          "file_path": "C:/data/ct/0001.png",
          "width": 512,
          "height": 512
        }
      ]
    },
    {
      "id": "stack_b",
      "name": "PHOTO",
      "modality": "photo",
      "directory": "C:/data/photo",
      "slices": [
        {
          "index": 0,
          "file_path": "C:/data/photo/0001.png",
          "width": 2048,
          "height": 1536
        }
      ]
    }
  ],
  "pairing": {
    "fixed_stack_id": "stack_b",
    "moving_stack_id": "stack_a",
    "global_offset": 12,
    "pairs": [
      {
        "fixed_index": 12,
        "moving_index": 0,
        "valid": true,
        "status": "candidate"
      }
    ]
  },
  "registrations": [
    {
      "fixed_index": 12,
      "moving_index": 0,
      "transform_type": "similarity",
      "forward_matrix_3x3": [1,0,0,0,1,0,0,0,1],
      "inverse_matrix_3x3": [1,0,0,0,1,0,0,0,1],
      "score": 0.0,
      "converged": false,
      "iterations": []
    }
  ]
}
```

## 8. Fluxo da Interface do MVP

## 8.1 Janela principal

Layout sugerido:

```text
+----------------------------------------------------------------------------------+
| Menu / Toolbar                                                                   |
+----------------------+------------------------------------------+----------------+
| Project / Settings   | Reference Viewer | Moving Viewer         | Diagnostics    |
|                      |------------------------------------------|                |
| Stack controls       | Preview Viewer                            | Registration   |
| Pairing controls     |                                          | details        |
| Method controls      |                                          |                |
+----------------------+------------------------------------------+----------------+
| Timeline A                                                                      |
| Timeline B                                                                      |
+----------------------------------------------------------------------------------+
| Status bar                                                                       |
+----------------------------------------------------------------------------------+
```

## 8.2 Painel esquerdo

Secoes:

- carregar pilha A
- carregar pilha B
- escolher modalidade de cada pilha
- escolher quem e fixa e movel
- ajustar offset global
- escolher preset de alinhamento
- iniciar alinhamento em lote
- salvar e abrir sessao

## 8.3 Viewers

Tres viewers:

- `Reference Viewer`
- `Moving Viewer`
- `Preview Viewer`

Funcoes:

- scroll para zoom
- arraste com botao do meio ou direito para pan
- duplo clique para fit-to-view
- checkbox para sincronizar viewport
- exibicao de informacoes do pixel e coordenadas

## 8.4 Preview modes

### Blend

- imagem fixa ao fundo
- movel transformada por cima
- alpha controlavel

### Checkerboard

- alternancia por quadrados
- tamanho ajustavel

### Difference

- diferenca absoluta entre imagens pre-processadas ou normalizadas

### Multiply

- composicao multiplicativa para destacar sobreposicoes

## 8.5 Timeline dupla

Cada timeline deve mostrar:

- indice do slice
- miniatura opcional
- cor/status do slice
- relacao de pareamento com a outra timeline

Interacoes:

- clique para selecionar slice
- drag horizontal para navegar
- arraste de controle de offset para deslocar pilhas
- teclas para avancar/recuar

## 9. Estrategia de Registro Inicial `CT x Fotografia`

## 9.1 Objetivo

Fornecer um alinhamento automatico inicial robusto o suficiente para a primeira iteracao do software.

## 9.2 Hipoteses praticas

- a fotografia ja foi recortada e preparada
- o fundo azul e dominante fora do corpo
- ha diferenca de modalidade, portanto intensidade bruta nao e confiavel sozinha
- a anatomia geral e a silhueta do corpo carregam mais informacao para inicializacao

## 9.3 Pre-processamento inicial

Para CT:

- converter para `float`
- normalizar faixa de intensidade
- opcionalmente gerar imagem de gradiente
- extrair mascara corporal aproximada por threshold + morfologia

Para fotografia:

- converter para HSV ou Lab
- detectar fundo azul uniforme
- gerar mascara de exclusao do fundo
- obter mascara corporal principal
- opcionalmente converter a grayscale normalizado
- extrair bordas e gradiente

## 9.4 Sequencia de alinhamento do MVP

Pipeline recomendado:

1. gerar mascaras do corpo para fixa e movel
2. alinhar centros de massa como inicializacao
3. executar busca coarse-to-fine por:
   - translacao
   - rotacao
   - escala
4. avaliar score por:
   - sobreposicao de mascaras
   - correlacao de gradiente
5. selecionar melhor transformacao similarity
6. opcionalmente refinar rigid se configurado pelo usuario

## 9.5 Tipo de transformacao do MVP

Padrao:

- `similarity`

Opcional:

- `rigid`

Affine fica previsto na arquitetura, mas pode entrar na iteracao seguinte do desenvolvimento.

## 9.6 Score inicial do MVP

Score hibrido sugerido:

`score = w1 * mask_overlap + w2 * gradient_correlation`

Pesos iniciais:

- `w1 = 0.6`
- `w2 = 0.4`

Esses valores podem virar parametros expostos mais adiante.

## 9.7 Convergencia no MVP

Considerar convergencia quando:

- delta da transformacao for menor que um limiar
- melhoria do score for menor que epsilon
- numero maximo de iteracoes for atingido

Campos minimos a registrar:

- iteracao
- score
- tx
- ty
- theta
- scale
- converged

## 10. Execucao em Lote

## 10.1 Fluxo

1. usuario define fixa, movel e offset
2. sistema gera pares validos
3. sistema enfileira jobs de registro
4. UI acompanha progresso sem travar
5. resultados sao armazenados por par

## 10.2 Politica de concorrencia

- thread principal reservada para UI
- pool simples de workers para:
  - thumbnails
  - pre-processamento
  - registro

## 10.3 Requisitos de UX

- progresso global e por slice
- possibilidade de cancelar
- possibilidade de rerodar apenas slices selecionados

## 11. DPI no Windows

## 11.1 Comportamento esperado

Na inicializacao:

- detectar DPI do monitor ativo
- obter fator de escala efetivo do Windows
- configurar escala global da UI

## 11.2 Implementacao sugerida

Modulo:

- `core/DpiUtilsWin.*`

Responsabilidades:

- ativar DPI awareness do processo
- consultar monitor atual
- ler escala efetiva
- fornecer valor para ImGui

## 11.3 Override manual

Controles:

- `Auto`
- presets de escala
- slider

Persistencia:

- salvar preferencia no JSON de sessao

## 12. Wireframe Funcional

## 12.1 Toolbar superior

Controles:

- `Open Stack A`
- `Open Stack B`
- `Save Session`
- `Load Session`
- `Run Batch`
- `Cancel`

## 12.2 Project panel

Controles:

- nome da sessao
- modalidade da pilha A
- modalidade da pilha B
- fixa x movel
- offset global
- range de slices ativos

## 12.3 Registration panel

Controles:

- preset de alinhamento
- transform type
- max iterations
- coarse levels
- preview mode
- alpha
- checker size

## 12.4 Diagnostics panel

Informacoes:

- slice atual
- par atual
- score
- converged
- parametros da transformacao
- status do job

## 13. Plano de Classes Iniciais

## 13.1 Application

Responsabilidades:

- boot do app
- ciclo principal
- instanciacao dos paineis
- integracao com sessao

## 13.2 SessionController

Responsabilidades:

- carregar/salvar sessao
- expor modelos para UI
- coordenar operacoes de alto nivel

## 13.3 TimelinePanel

Responsabilidades:

- desenhar timelines
- exibir estado visual das pilhas
- permitir ajuste do offset
- trocar slice corrente

## 13.4 ViewerPanel

Responsabilidades:

- desenhar textura
- controlar zoom/pan
- aplicar composicao visual

## 13.5 RegistrationEngine

Responsabilidades:

- receber par e preset
- rodar pre-processamento
- executar alinhamento
- produzir `RegistrationResult`

## 13.6 SessionSerializer

Responsabilidades:

- converter modelos para JSON
- ler JSON para modelos

## 13.7 ThumbnailCache

Responsabilidades:

- gerar e guardar thumbnails em memoria
- evitar recarregamento desnecessario

## 14. Ordem Recomendada de Implementacao

## Sprint 1

- CMake base
- janela ImGui funcional
- deteccao de DPI
- tema inicial e fonte

## Sprint 2

- carregamento de duas pilhas
- modelo `StackModel`
- viewers basicos
- timeline dupla simples

## Sprint 3

- offset global
- selecao de pares validos
- persistencia JSON
- thumbnails em background

## Sprint 4

- pre-processamento CT x fotografia
- preview modes completos
- registration engine similarity

## Sprint 5

- batch processing
- diagnosticos
- historico de iteracoes
- rerun de slices

## 15. Critérios de Aceite do MVP

O MVP sera considerado valido quando:

- carregar duas pilhas reais do usuario
- mostrar as duas em timelines separadas
- permitir ajustar offset entre elas
- selecionar um par valido com base nesse offset
- visualizar fixa, movel e preview
- aplicar alinhamento automatico inicial em um par
- aplicar alinhamento automatico em lote para multiplos pares
- salvar e reabrir a sessao em JSON
- funcionar corretamente em monitor 4K com escala do Windows em 150%

## 16. Proximo Documento Apos o MVP Design

Depois deste documento, o artefato mais util sera um plano de implementacao do repositorio contendo:

- `CMakeLists.txt` inicial
- bootstrap do app
- integracao das dependencias
- primeiros headers e fontes
- backlog tecnico por arquivo

Esse proximo passo ja pode ser convertido diretamente em codigo.

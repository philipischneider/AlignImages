# Especificacao Tecnica - Ferramenta de Alinhamento de Pilhas de Imagens

## 1. Objetivo

Desenvolver uma ferramenta desktop Windows, escrita em C++, para alinhamento iterativo e reversivel entre duas pilhas de imagens 2D de modalidades distintas, com foco inicial em imagens do Visible Human Project.

O caso inicial de uso e o alinhamento de uma pilha de tomografia computadorizada com uma pilha de fotografias digitais recortadas, com fundo azul uniforme e regioes vazadas para exclusao de elementos externos ao corpo humano.

O software deve evoluir para suportar outros cenarios, como:

- ressonancia magnetica x fotografia digital
- scans fotograficos de filme 70mm x fotografia digital
- mascaras de segmentacao x imagens anatomicas
- outros pares de modalidades 2D desde que possam ser tratados por um pipeline de registro

## 2. Objetivos Funcionais

O sistema deve permitir:

- carregar duas pilhas completas de imagens
- exibir ambas em timeline, com navegacao visual por slice
- ajustar o offset global entre pilhas quando nao houver correspondencia 1:1
- identificar a faixa de sobreposicao util entre as pilhas
- alinhar pares de imagens automaticamente
- refinar iterativamente os alinhamentos
- detectar convergencia por slice e por conjunto
- executar um segundo passe de alinhamento usando medias ou tendencias convergentes como restricao
- realizar alinhamento manual por pontos correspondentes
- salvar sessoes, transformacoes, iteracoes, landmarks e diagnosticos em JSON
- exportar alinhamento em ambas as direcoes: movel para fixa e fixa para movel

## 3. Premissas e Restricoes

- plataforma alvo exclusiva: Windows
- foco em bom desempenho e responsividade
- interface com poucas dependencias
- preferencia por Dear ImGui para UI
- suporte a monitor 4K com escala DPI do Windows, incluindo deteccao automatica e override manual
- processamento de pilhas inteiras, nao apenas de pares isolados

## 4. Stack Tecnologica Recomendada

### 4.1 Linguagem e Build

- C++20
- CMake
- MSVC no Windows

### 4.2 Bibliotecas Principais

- Dear ImGui: interface
- GLFW: janela e integracao com monitor/DPI
- OpenGL: renderizacao da interface e visualizacao de imagens
- OpenCV: IO de imagens, pre-processamento, warping, metricas e otimizacoes iniciais
- nlohmann/json: persistencia da sessao em JSON

### 4.3 Motivos da Escolha

- Dear ImGui oferece uma UI simples, rapida e facil de iterar
- GLFW e OpenGL mantem a base enxuta
- OpenCV reduz o custo de implementar leitura, transformacoes e operacoes de imagem
- JSON favorece reprodutibilidade, depuracao e interoperabilidade

## 5. Casos de Uso Principais

### 5.1 Alinhamento de duas pilhas com offset entre sequencias

O usuario carrega:

- pilha A: tomografia
- pilha B: fotografia digital

Como nem todas as imagens possuem correspondencia 1:1, o usuario ajusta um offset global na timeline para definir a relacao inicial entre as pilhas. O sistema usa essa configuracao para determinar pares candidatos e slices sem correspondente.

### 5.2 Refinamento iterativo em lote

O usuario roda um primeiro alinhamento automatico para todos os pares validos. O sistema armazena transformacoes e scores. Em seguida, calcula tendencias convergentes ao longo da pilha e executa um segundo passe restrito por esse comportamento medio.

### 5.3 Correcao manual de slices problematicos

Para slices com baixa confianca ou falha de convergencia, o usuario marca pontos correspondentes entre as duas imagens. O sistema recalcula a transformacao e pode usar esse resultado como solucao final ou como inicializacao para novo refinamento automatico.

### 5.4 Exportacao reversivel

O usuario escolhe se deseja:

- reamostrar a imagem movel no espaco da imagem fixa
- reamostrar a imagem fixa no espaco da imagem movel
- exportar ambas as direcoes

## 6. Requisitos de Interface

## 6.1 Layout principal

Layout recomendado em quatro areas:

- painel lateral esquerdo: projeto, pilhas, presets, metodos, parametros
- area central superior: viewers principais
- area central inferior: timeline dupla
- painel lateral direito: diagnosticos, iteracoes, landmarks, exportacao

## 6.2 Viewers principais

Devem existir pelo menos tres paines:

- imagem de referencia
- imagem movel
- preview do alinhamento

O preview deve suportar:

- blend simples
- checkerboard
- difference
- multiply

Controles adicionais:

- opacidade do blend
- tamanho dos quadrados do checkerboard
- fit to view
- zoom
- pan
- reset de viewport
- sincronizacao opcional de viewport entre paineis

## 6.3 Timeline dupla

A timeline deve ser inspirada no modulo de animacao do Krita, adaptada para sequencias de imagens medicas/fotograficas.

Requisitos:

- uma faixa horizontal para cada pilha
- cada slice representado como uma celula navegavel
- miniaturas opcionais ou indicadores compactos
- destaque visual para:
  - slice atual
  - slices pareados
  - slices sem correspondente
  - slices alinhados
  - slices suspeitos
  - slices ajustados manualmente
- controle visual do offset global entre pilhas
- arraste horizontal para deslocar uma pilha em relacao a outra
- navegação por teclado e mouse
- selecao direta de um slice para inspeção

## 6.4 Controle de DPI e escala da UI

O programa deve:

- detectar a escala DPI efetiva do monitor no Windows
- aplicar automaticamente o fator de escala na UI do ImGui
- permitir override manual pelo usuario

Controles recomendados:

- modo `Auto`
- presets `100%`, `125%`, `150%`, `175%`, `200%`
- slider fino de escala opcional

Importante: separar claramente:

- escala da interface
- zoom da imagem

## 7. Modelo Conceitual

## 7.1 Entidades principais

### Stack

Representa uma pilha de imagens.

Campos principais:

- id
- nome
- modalidade
- diretorio de origem
- lista ordenada de slices
- parametros de pre-processamento
- metadados globais

### Slice

Representa uma imagem individual da pilha.

Campos principais:

- indice na pilha
- caminho do arquivo
- nome do arquivo
- dimensoes
- metadados opcionais
- thumbnail opcional
- status de pareamento
- status de alinhamento

### Pairing

Representa a associacao entre um slice da pilha A e um slice da pilha B.

Campos principais:

- indice da pilha A
- indice da pilha B
- tipo de correspondencia
- offset global aplicado
- status

### RegistrationResult

Representa o resultado do alinhamento entre duas imagens.

Campos principais:

- transformacao forward
- transformacao inverse
- tipo da transformacao
- score final
- status de convergencia
- historico de iteracoes
- landmarks manuais
- observacoes

### Session

Representa o estado completo do projeto aberto.

Campos principais:

- stacks carregadas
- configuracao de pairing
- resultados por par
- preferencias de UI
- parametros do pipeline
- historico da sessao

## 8. Pairing entre Pilhas

Este modulo nao deve assumir correspondencia 1:1.

### 8.1 Conceitos

- `offset_global`: deslocamento entre os indices das duas pilhas
- `janela_de_sobreposicao`: faixa de indices em que pode haver pares validos
- `slices_sem_correspondencia`: slices fora da intersecao ou invalidados manualmente

### 8.2 Estrategia inicial

Formula base para pareamento candidato:

`indice_B = indice_A + offset_global`

Depois dessa associacao inicial, o usuario pode:

- ajustar o offset manualmente
- invalidar pares
- promover pares alternativos em casos especiais

### 8.3 Evolucao futura

No futuro, o pairing pode incorporar:

- espacamento Z
- metadados de posicao
- correspondencia por similaridade de forma
- deteccao semi-automatica do offset ideal

## 9. Pipeline de Alinhamento

## 9.1 Etapa 1 - Ingestao e padronizacao

Para cada pilha:

- carregar arquivos
- ordenar slices
- gerar miniaturas
- detectar dimensoes
- aplicar pre-processamento configurado por modalidade

## 9.2 Etapa 2 - Pre-processamento por modalidade

O sistema deve ser extensivel por preset de modalidade.

Exemplo de presets:

- CT
- fotografia digital
- RM
- scan fotografico
- mascara de segmentacao

Operacoes possiveis:

- conversao para grayscale
- normalizacao de intensidade
- equalizacao
- deteccao e remocao do fundo azul
- obtencao de mascara do corpo
- extracao de bordas
- gradiente
- distancia ao contorno

## 9.3 Etapa 3 - Alinhamento automatico inicial

Tipos de transformacao suportados no MVP:

- rigid
- similarity
- affine

Metricas suportadas no MVP:

- mutual information
- correlacao em gradiente
- distancia entre contornos
- sobreposicao de mascaras

Estrategia recomendada:

- coarse-to-fine
- piramide de resolucao
- inicializacao neutra ou baseada em alinhamento anterior

## 9.4 Etapa 4 - Refinamento iterativo

Para cada par:

- executar iteracoes sucessivas
- usar a transformacao anterior como semente
- medir melhoria do score
- registrar historico completo

Criticos:

- limite maximo de iteracoes
- criterio de parada por estabilizacao
- deteccao de falha

## 9.5 Etapa 5 - Analise de convergencia entre slices

Apos o primeiro passe em lote:

- coletar parametros de transformacao por slice
- analisar tendencia ao longo da pilha
- detectar outliers
- estimar medias, medianas ou curvas suavizadas

Opcoes recomendadas:

- media movel
- mediana movel robusta
- spline 1D por parametro

## 9.6 Etapa 6 - Segundo passe com restricao

No segundo alinhamento:

- usar a estimativa convergente como prior suave
- permitir desvio controlado por slice
- reduzir saltos incoerentes entre imagens vizinhas

Objetivo:

- aumentar consistencia global
- reduzir outliers
- melhorar estabilidade do conjunto

## 9.7 Etapa 7 - Alinhamento manual por landmarks

O usuario marca pontos correspondentes entre fixa e movel.

Funcoes necessarias:

- adicionar, mover e remover pontos
- exibir indices e pares de pontos
- calcular erro de reprojecao
- estimar transformacao por landmarks
- usar landmarks como resultado final ou como inicializacao do automatico

## 10. Reversibilidade do Alinhamento

O sistema deve tratar explicitamente:

- imagem fixa
- imagem movel
- transformacao forward: movel para fixa
- transformacao inverse: fixa para movel

Na exportacao, o usuario deve poder escolher:

- aplicar `movel -> fixa`
- aplicar `fixa -> movel`
- exportar apenas matrizes e parametros sem reamostragem

Isso torna o software geral o suficiente para diferentes cenarios de uso.

## 11. Modelo de Dados JSON

Exemplo de estrutura inicial:

```json
{
  "project": {
    "name": "visible_human_alignment",
    "version": "0.1.0"
  },
  "ui": {
    "dpi_mode": "auto",
    "dpi_override": 1.5,
    "preview_mode": "checkerboard",
    "checker_size": 32
  },
  "stacks": [
    {
      "id": "ct",
      "name": "CT",
      "modality": "ct",
      "directory": "C:/data/ct",
      "slice_count": 260
    },
    {
      "id": "photo",
      "name": "DigitalPhoto",
      "modality": "photo",
      "directory": "C:/data/photo",
      "slice_count": 300
    }
  ],
  "pairing": {
    "global_offset": 12,
    "valid_pairs": [
      { "a": 0, "b": 12, "status": "candidate" },
      { "a": 1, "b": 13, "status": "aligned" }
    ]
  },
  "registrations": [
    {
      "pair": { "a": 1, "b": 13 },
      "fixed_stack_id": "photo",
      "moving_stack_id": "ct",
      "transform_type": "similarity",
      "forward": {
        "matrix_3x3": [1, 0, 0, 0, 1, 0, 0, 0, 1]
      },
      "inverse": {
        "matrix_3x3": [1, 0, 0, 0, 1, 0, 0, 0, 1]
      },
      "iterations": [
        {
          "index": 0,
          "score": 0.42,
          "delta": 0.11,
          "converged": false
        }
      ],
      "manual_landmarks": [],
      "quality": {
        "score": 0.87,
        "flag": "ok"
      }
    }
  ]
}
```

## 12. Estrutura de Modulos C++

Estrutura sugerida:

```text
src/
  app/
  core/
  data/
  io/
  preprocess/
  registration/
  viewer/
  timeline/
  ui/
  export/
```

### 12.1 app

- inicializacao
- ciclo principal
- gerenciamento de sessao
- integracao dos modulos

### 12.2 core

- tipos comuns
- transformacoes
- enums
- utilitarios matematicos

### 12.3 data

- Stack
- Slice
- Pairing
- Session
- RegistrationResult

### 12.4 io

- carregamento de imagens
- scans de diretorio
- thumbnails
- leitura e escrita de JSON

### 12.5 preprocess

- pipelines por modalidade
- mascara azul
- bordas
- normalizacao
- extracao de representacoes auxiliares

### 12.6 registration

- metricas
- otimizadores
- modelos de transformacao
- iteracao
- convergencia
- restricao inter-slices
- landmarks

### 12.7 viewer

- texturas GPU
- viewport
- zoom e pan
- composicao de preview

### 12.8 timeline

- renderizacao das trilhas
- controle de offset
- selecao de slices
- status visual dos pares

### 12.9 ui

- janelas e paineis ImGui
- preferencias
- dialogs
- controles de parametros

### 12.10 export

- exportacao de imagens reamostradas
- exportacao de matrizes
- relatorios de qualidade

## 13. Estrutura de Classes Sugerida

Classes iniciais recomendadas:

- `Application`
- `SessionController`
- `StackModel`
- `SliceRecord`
- `PairingModel`
- `RegistrationEngine`
- `PreprocessPipeline`
- `TransformModel`
- `ConvergenceAnalyzer`
- `LandmarkEditor`
- `ImageViewerPanel`
- `TimelinePanel`
- `ExportController`

## 14. Fluxo de Uso do Usuario

### 14.1 Criacao da sessao

1. carregar pilha A
2. carregar pilha B
3. escolher modalidade de cada pilha
4. revisar ordenacao e contagem

### 14.2 Ajuste do pareamento global

1. abrir timeline dupla
2. deslocar uma pilha em relacao a outra
3. observar faixa de sobreposicao
4. confirmar offset global inicial

### 14.3 Primeiro passe automatico

1. escolher fixa e movel
2. escolher preset de alinhamento
3. executar em lote
4. inspecionar slices aprovados e suspeitos

### 14.4 Refinamento

1. analisar convergencia
2. gerar restricoes medias
3. executar segundo passe
4. comparar antes e depois

### 14.5 Ajuste manual

1. selecionar slice problematico
2. marcar landmarks
3. recalcular transformacao
4. aceitar como definitivo ou reexecutar refinamento local

### 14.6 Exportacao

1. escolher direcao do alinhamento
2. escolher faixa de slices
3. exportar imagens ou apenas transformacoes

## 15. Requisitos de Desempenho

Objetivos iniciais:

- abertura responsiva de pilhas grandes
- thumbnails geradas de forma assicrona
- troca fluida entre slices
- preview em tempo real com zoom e pan
- execucao em lote sem travar a interface

Diretrizes:

- manter imagens completas em CPU quando necessario
- manter texturas reduzidas para preview em GPU
- usar jobs em background para pre-processamento e alinhamento
- desacoplar UI de tarefas longas

## 16. Requisitos de Qualidade e Auditoria

Cada resultado deve manter:

- score final
- score por iteracao
- parametros de transformacao
- criterio de convergencia
- modo de obtencao
  - automatico
  - manual
  - hibrido
- data e versao do algoritmo

Isso e importante para reproducao e revisao posterior.

## 17. Roadmap de Implementacao

## Fase 1 - Fundacao do app

- configurar CMake
- integrar ImGui, GLFW, OpenGL
- integrar OpenCV e JSON
- criar janela principal
- implementar DPI automatico e override manual

## Fase 2 - Viewer e timeline

- carregar duas pilhas
- gerar thumbnails
- criar painel de timeline dupla
- implementar offset global entre pilhas
- implementar viewers com zoom e pan
- adicionar preview blend, checkerboard, difference e multiply

## Fase 3 - Modelo de sessao

- persistencia JSON
- stacks, slices, pairing e preferencias
- reabertura de sessao

## Fase 4 - Alinhamento automatico MVP

- pre-processamento por preset
- rigid e similarity
- metricas iniciais
- registro por pares validos
- historico de iteracoes

## Fase 5 - Convergencia e segundo passe

- analise global de parametros
- deteccao de outliers
- restricoes entre slices
- refinamento em lote

## Fase 6 - Ferramentas manuais

- landmarks
- erro de reprojecao
- combinacao manual + automatico

## Fase 7 - Exportacao e polimento

- exportacao forward e inverse
- filtros por faixa de slices
- relatorio de qualidade
- otimizacao de usabilidade

## 18. Riscos Tecnicos

- registro multimodal pode falhar quando baseado apenas em intensidade
- o fundo azul e regioes vazadas exigem mascara robusta
- diferencas fortes de escala e orientacao podem exigir inicializacao melhor
- datasets distintos podem demandar presets especificos
- transformacoes excessivamente livres podem gerar sobreajuste

Mitigacao:

- comecar por contorno, gradiente e mascaras
- manter landmarks como rota de seguranca
- preservar historico completo e score por etapa

## 19. Decisoes Ja Consolidadas

- foco em C++ e Windows
- uso de Dear ImGui
- tratamento nativo de pilhas inteiras
- timeline dupla com offset entre sequencias
- alinhamento reversivel
- persistencia em JSON
- zoom/pan nos viewers
- suporte a DPI automatico com override manual
- base extensivel para multiplas modalidades

## 20. Proximo Passo Recomendado

O proximo artefato deve ser um documento de design do MVP com:

- lista fechada de dependencias
- estrutura de diretorios do repositorio
- classes e headers iniciais
- wireframe da interface
- contrato inicial do JSON
- primeira estrategia de registro para `CT x fotografia`

Esse documento servira como base direta para iniciar a implementacao.

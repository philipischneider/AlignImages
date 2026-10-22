# Laboratório de alinhamento deformável

## Aplicativo interativo de revisão

Abra `ReviewResults.cmd` com duplo clique, ou execute:

```powershell
python experimental/deformable/viewer.py
python experimental/deformable/viewer.py --results "out/deformable/dicom_full_photo_pilot_002"
```

A janela carrega os resultados processados e oferece:

- Três painéis: referência, móvel original e preview móvel → referência.
- Blend com opacidade, checkerboard de 4 a 128 px, difference e multiply.
- Fit, 1:1 e Reset por painel; roda para zoom, botão central para pan e
  duplo clique central para ajustar. Sincronização opcional entre painéis.
- Contornos Canny independentes, com referência verde e móvel magenta no preview.
- Alternância de alinhamento aplicado e seleção das hipóteses calculadas.
- Timeline de duas faixas com miniaturas carregadas por visibilidade, seletor,
  Anterior/Próximo, setas do teclado e reprodução com fps ajustável (Espaço).
- Informação mútua com a mesma normalização e histograma parcial de 32 bins
  usados no viewer C++; valor calculado na janela padrão, independente do
  ajuste visual de contraste. Dice exibido é o do resultado original, não
  recalculado ao selecionar outra hipótese.
- Janela/nível de DICOM com presets tecidos, pulmão e osso; PNGs permitem
  somente ajuste de exibição 0–255, sem simular HU ou recuperar dados perdidos.
- Máscaras ósseas quando referenciadas pelo resultado, diagnóstico recolhível
  e redimensionável, escala de interface 100–200% e suporte ao DPI do Qt.

`Abrir pasta` permite escolher uma pasta de resultados ou sua raiz; `Abrir JSON`
aceita `result.json` ou o `manifest.json` produzido pelo preparador. Entradas,
resultados e matrizes são lidos sem alteração. A seleção de hipótese só muda
a visualização: não recalcula, aprova nem salva um novo alinhamento. Os modos
de edição de landmarks/Transpose do AlignImages não são operações de revisão
e não são executados por este visualizador.

Para conjuntos de PNG, não se usa automaticamente o DICOM de origem: o campo
de visão e a geometria precisam corresponder à imagem usada no processamento.
Resultados contendo uma fonte DICOM monocromática de quadro único podem ser
visualizados com sua própria janela/nível. Máscaras precisam compartilhar a
grade da imagem correspondente.

Teste visual fora da tela:

```powershell
$env:QT_QPA_PLATFORM = 'offscreen'
python experimental/deformable/viewer.py --smoke-screenshot "out/deformable/viewer.png"
```

Remova essa variável antes de iniciar uma janela visível. Requer PySide6,
incluído no `requirements.txt`. Nenhuma dependência C++ do app é alterada.

## Fotografias originais e máscaras restauradas

`restore_photo_space.py` usa o recorte comum `(699,183,650,850)` aplicado à
sequência anatomicamente alinhada e reposiciona cada máscara `slice_NNNN.png`
no canvas da fotografia original `1001+N.png`, de 2048×1216. Usa a mesma translação inteira em todos os cortes,
sem redimensionamento. Confere limites, preservação dos rótulos, contagem de
foreground e round-trip exato do recorte; calcula correlação de gradientes
com a fotografia tratada para verificar a correspondência.

O conjunto corrigido em `out/deformable/original_photo_space_002` contém:

- `masks/`: máscaras no espaço original, mantendo todos os pixels anotados.
- `annotation_support/`: área do recorte com informação de anotação. Fora dela,
  valor zero da máscara significa ausência de anotação, não ausência de osso.
- `photo_stack.json`: referências às fotografias originais (sem copiá-las ou
  modificá-las), recorte comum, hashes, verificações e avisos.
- `overlays/`: exemplos com máscara magenta e retângulo do antigo recorte amarelo.

As coordenadas diferentes da tabela histórica são estimativas de busca de
template e NÃO representam recortes aplicados diferentes. O usuário confirmou
recorte único; comparação das seis exceções confirmou que a posição comum
produz correlação de gradientes acima de 0,9988. A tabela é usada para nomes
e auditoria, não para deslocar as máscaras. Correlação de gradientes abaixo
de 0,99 (ou indefinida) sinaliza revisão. `--crop` permite informar outro
recorte comum explicitamente. A versão `_001` é superada para uso geral.
Esse reposicionamento não reconstrói a anatomia que foi cortada da máscara.

`run_dicom_pilot.py --photo-pattern "{source_number}.png"` usa as fotografias
inteiras. `--reference-masks` associa as máscaras restauradas apenas para
revisão no visualizador; elas não entram na otimização enquanto não houver
uma máscara óssea correspondente na CT.

## Leitura e processamento DICOM

O processamento atual usa os arquivos `.dcm` originais, não os PNGs derivados.
O visualizador abre por padrão `out/deformable/dicom_full_photo_pilot_002`, com os oito
cortes do piloto e fotografias originais inteiras. O título identifica a origem DICOM; os presets atuam sobre
os valores reescalonados da fonte.

```powershell
python experimental/deformable/run_global.py --fixed "C:\fotos\Head236.png" --moving "D:\serie\arquivo.dcm" --ct-spacing-mm 0.527344 --allow-vertical-reflection --output "out\deformable\dicom_par_001"
```

`dicom_ct.py` decodifica pixels originais, aplica slope/intercept, exclui padding
e valida espaçamento, posição e orientação. A máscara corporal padrão usa HU
acima de -400 (ajustável por `--ct-body-threshold-hu`), fechamento em mm e maior
componente preenchido. Deve ser revisada: mesa, materiais e artefatos podem
exigir uma máscara fornecida pelo usuário.

Para o registro, as máscaras são reamostradas em grade isotrópica no plano;
o mapeamento dessa grade é composto com a transformação estimada. A matriz
final transforma **pixels nativos do DICOM → pixels da fotografia**, incluindo
as hipóteses alternativas; não é uma matriz em coordenadas paciente.

Geometria, hash DICOM, grade e composição ficam em `result.json`.
`aligned_ct_hu.npz` preserva a CT alinhada em float32 e uma máscara de suporte;
`aligned.png` é somente a prévia de janela 40/400. O registro usa máscaras HU,
independentemente da janela escolhida para visualizar. Ainda não há grade
comum em coordenadas paciente para toda a série, calibração física da foto,
continuidade Z ou deformação local.

O executor `run_dicom_pilot.py` lê a tabela CSV de proveniência (campos
`aligned_mask_index` e `source_file`) e busca os pares correspondentes sem
consultar matrizes de sessão. Exemplos anteriores em PNG ficam preservados
para comparação e podem ser abertos explicitamente no visualizador.

Módulo Python independente do aplicativo C++. Primeira etapa implementada:
auditoria da sessão e revisão do alinhamento afim existente. Não calcula ainda
deformações, máscaras ósseas de origem ou continuidade longitudinal.

## Alinhamento global autônomo

`run_global.py` estima uma transformação afim diretamente de um par novo,
sem aceitar sessão, matriz inicial ou landmarks:

```powershell
python experimental/deformable/run_global.py --fixed "C:\fotos\Head236.png" --moving "C:\CT\1237.png" --allow-vertical-reflection --output "out\deformable\auto_001"
```

O usuário fornece um par correspondente. Esta versão não descobre o offset.
Extrai candidatos de máscaras corporais (rejeição de azul na foto e Otsu no
PNG da CT, maior componente preenchido); sempre conferir essas máscaras.
`--fixed-body` e `--moving-body` permitem fornecer máscaras revisadas.

`--fixed-bone` e `--moving-bone` habilitam orientação por ossos, exigindo ambas
as máscaras nas respectivas grades originais. Não extrai osso por HU de PNGs
e não usa a máscara revisada da foto como se fosse uma máscara fonte da CT.

O motor reduz as máscaras para uma grade de trabalho, testa 24 ângulos, estima
escala/centros por área e mantém hipóteses distintas para otimização Powell.
O objetivo combina distância simétrica entre contornos e restrições leves
sobre anisotropia e shear. Retorna matriz forward móvel → fixa em pixels
originais e inversa. `--allow-vertical-reflection` autoriza explicitamente
pesquisar orientações espelhadas; sem a opção o determinante fica positivo.

`result.json` guarda entradas/hashes, parâmetros, hipóteses, convergência e
avisos; `review.html` mostra o candidato. Ctrl+C registra cancelamento no JSON.
Não há aprovação automática: Dice corporal alto não valida anatomia interna.
Não há ainda deformação local, otimização longitudinal, calibração física ou
normalização explícita de FOV. Cada corte é independente; ambiguidades de
orientação são esperadas quando se usa apenas o contorno externo.

Para reunir execuções em uma página:

```powershell
python experimental/deformable/review_global_runs.py --runs "out/deformable/autonomous_head*_002" --output "out/deformable/autonomous_pilot_002"
```

Testes do motor: `python -m unittest discover -s experimental/deformable -p test_global_registration.py`.
Cobrem recuperação de rotação grande, escala e translação em grades distintas,
reflexão explícita com orientação por ossos e rejeição de máscaras inválidas.

Requer Python 3.10+ e as dependências de `requirements.txt`.

```powershell
python experimental/deformable/prepare.py --session "C:\caminho\session_autosave.json" --masks "E:\Test_Muscle\Osso_Candidato_1" --output "out\deformable\baseline_001"
```

Abra `review.html` no diretório gerado para navegar pelos exemplos. O comando
rejeita diretórios de saída existentes e saídas dentro das árvores de entrada.
Ctrl+C cancela; o manifesto parcial registra o estado. Cada execução preserva
os dados originais e grava parâmetros, hash da sessão e hashes dos exemplos.

A visualização `Modo salvo na sessão` reproduz blend, checkerboard, difference
ou multiply conforme as preferências do JSON. A opção de sobreposição 50% é
uma comparação padronizada e pode diferir do modo usado no app.

## Diagnóstico de sessões e exportações

`compare_sessions.py` compara duas sessões sem otimizar alinhamentos e verifica
a reprodução da sessão candidata contra exportações existentes do aplicativo:

```powershell
python experimental/deformable/compare_sessions.py --original "C:\sessao_antiga.json" --candidate "C:\sessao_manual.json" --exports "C:\moving_to_fixed" --output "out\deformable\comparacao_001"
```

Gera `comparison.html` com os exemplos lado a lado e `comparison.json` com
erros de pixels por corte. `--export-pattern` tem padrão `CT_{index:03d}.png`,
indexado pela pilha fixa. Exportações podem pertencer a outra versão de sessão;
diferença de pixels não é automaticamente erro no leitor. Igualdade de pixels
confirma reprodução daquela exportação, não qualidade anatômica. Os títulos
abril/maio do relatório correspondem às sessões do diagnóstico VHP atual.

Por padrão, renderiza índices fixos 5, 100, 234, 235, 236, 237, 240, 250, 260 e
264. `--indices 5,100` muda os exemplos. Todos os pares explícitos são auditados
quanto a arquivos e transformação; dimensões e conteúdo das imagens são
conferidos nos exemplos renderizados. O offset é informativo e não reaplicado.

As imagens usam a matriz forward móvel → fixa, interpolação linear e tamanho
da imagem fixa, como o viewer C++. Rotação é aplicada antes dos flips. O leitor
aceita sessões antigas (`pairing`) e atuais (`pairings`); em sessões atuais,
`--pairing-id` permite selecionar outro pareamento. A matriz forward salva já
inclui ajustes manuais; eles não são compostos novamente.

A associação `slice_NNNN` → índice fixo N é uma hipótese visível no relatório,
não uma confirmação anatômica. `--mask-pattern` permite outra nomenclatura.
Máscara vazia é registrada, não interpretada automaticamente como erro.

Opções de geometria:

- `--dicom-dir "D:\serie"`: inventário de tags geométricas sem pixels nem dados
  de identificação pessoal; não infere correspondência por ordem de arquivos.
- `--source-map "E:\...\frozen_ct_c_vm1006_1265_map.csv"`: liga o número do PNG
  da CT à origem DICOM usando a tabela de proveniência existente. Essa tabela
  ainda deve ser validada contra a conversão PNG; não prova orientação igual.

O manifesto distingue erro de entrada, par inválido, arquivo validado e exemplo
renderizado. Código de saída 0 significa preparação concluída para revisão,
2 significa erro/incompletude e 130 cancelamento. Nenhum desses estados afirma
que o alinhamento anatômico está correto. Metadados físicos são registrados;
normalização de FOV e regularização em milímetros ainda não são executadas.

Próxima etapa: conferir as sobreposições, confirmar indexação das máscaras,
orientação PNG/DICOM e segmentação CT de origem. Depois implementar o motor
residual e o acoplamento entre cortes em coordenadas físicas.

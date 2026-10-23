# Refinamento regional e continuidade

O motor regional foi implementado em `local_registration.py` e `run_local.py`.
Complementa o registro global; as afirmações de ausência de deformação no
README histórico referem-se ao preparador e aos executores globais.

```powershell
python experimental/deformable/run_local.py --baseline "out/deformable/local_baseline_001/dicom_pilot.json" --annotations "out/deformable/original_photo_space_002/photo_stack.json" --output "out/deformable/regional_pilot_001" --device cuda
```

Sem CUDA, omita a opção de dispositivo para usar CPU. PyTorch calcula derivadas
dos deslocamentos da malha; não há rede neural nem treinamento de modelo.

## Controles interativos

Novas execucoes salvam o campo real ao fim de cada etapa (melhor checkpoint
elegivel daquela etapa, escolhido pelo otimizador). O seletor
`Etapa da deformacao` no diagnostico alterna entre 7x7, 13x13, 25x25 (ou os
tamanhos configurados) e `Final (apos criterios)`, sem mudar corte ou zoom.
Cada etapa inclui contornos, malha, HU reamostrado e metricas proprios. Sao
snapshots ANTES dos criterios finais: podem piorar regioes ou apresentar
distorcao; nao sao resultados aprovados. O resultado final continua usando
os mesmos criterios de seguranca/ganho e pode diferir da ultima etapa bruta.
Arquivos antigos sem snapshots continuam legiveis, com seletor desabilitado;
e necessario reprocessar para gerar os intermediarios, nao reconstrui-los
a partir da malha final. Cada corte guarda `stage_NN_field.npz` e a lista
`local_deformation.stages` com hashes, parametros e metricas; as malhas de
controle por etapa ficam na raiz do experimento. Nenhum novo result.json
por etapa e criado, evitando duplicar cortes na timeline.

Na aba `Reprocessamento / malha`, informe os nos por eixo separados por virgula
(por exemplo `7,13` ou `7,13,25`) e a resolucao maxima de processamento em pixels.
Os tamanhos devem ser crescentes, entre 3 e 65; a resolucao aceita 64 a 2048.
Resolucao maior exige mais memoria e tempo; malha mais fina nao garante melhor
alinhamento. Os controles nao alteram instantaneamente a imagem visualizada.

`Reprocessar` executa toda a sequencia do baseline global selecionado em processo
separado, com pesos corpo/osso 1:1. A aba carrega baseline e anotacoes da auditoria
do resultado, quando disponiveis; entradas antigas podem ser escolhidas manualmente.
Iteracoes e CPU/CUDA tambem sao configuraveis. O log mostra o andamento. Cada teste
cria `out/deformable/interactive_<data>_<id>` e abre o novo conjunto ao concluir.
`Cancelar` solicita parada cooperativa na preparacao, em cada iteracao ou entre
exportacoes, preservando auditoria e resultado anterior; nao abre saida incompleta.
Ao fechar durante uma execucao, solicita cancelamento e pede aguardar antes de fechar.

CLI equivalente: acrescente `--nodes 7 13 25 --working-size 512` a `run_local.py`.
O visualizador abre por padrao `regional_stages_001`, com pesos iguais e etapas 7/13/25.
Refinamento adaptativo manual e protecao do incremento local estao implementados;
selecao automatica de regioes ainda nao esta implementada.

## Resolucao adaptativa manual

Na aba `Reprocessamento / malha`, habilite `Regioes adaptativas (manual)`.
Escolha `Nos por regiao`, clique `Marcar refino` e arraste com o botao esquerdo
na fotografia ou no preview. O retangulo amarelo recebe uma malha local adicional.
`Marcar protecao` cria um retangulo verde; `Parar` devolve o mouse a navegacao.
As coordenadas sao pixels da fotografia original, respeitando zoom/pan. Cada
regiao vale somente para aquela fotografia, nao se propaga automaticamente em Z.
Remova marcacoes pela lista. As definicoes sao lembradas nas preferencias locais.

Ao reprocessar, o motor calcula as malhas gerais e depois as congela. Otimiza
malhas bicubicas locais (3..33 nos por eixo) dentro dos retangulos de refino;
nao aumenta a quantidade de nos da imagem inteira. A malha local deve ter
espacamento menor que a ultima malha geral, e a resolucao de processamento deve
permitir amostra-la; caso contrario, o executor informa o motivo e pede uma
regiao menor, mais nos locais ou maior resolucao de processamento.

O incremento inverso local e somado ao residual geral no mesmo dominio da foto.
Tem suporte limitado ao retangulo (recortado pela ROI corporal), taper sin^2
nas bordas, amplitude de ate 8px por patch e protecao com transicao suave de16px.
O incremento e exatamente zero dentro da protecao e fora das areas de refino.
Sobreposicoes podem somar incrementos; o limite total e o Jacobiano continuam
avaliados em resolucao completa. A perda continua incluindo corpo/osso1:1,
suavidade, rigidez ossea e continuidade fisica entre cortes, sem deslocamento Z.
Cortes nao marcados nao recebem incremento, embora a fase geral continue conjunta.

IMPORTANTE: protecao congela apenas o incremento ADAPTATIVO. Ela nao congela a
transformacao global nem impede a fase geral de produzir outra deformacao. A
referencia protegida e o resultado geral da mesma execucao, nao necessariamente
o resultado que estava aberto quando o retangulo foi desenhado.

Na selecao final, a componente geral passa primeiro pelos criterios existentes.
Depois o incremento adaptativo e reduzido ou rejeitado em separado, tendo esse
resultado geral como base; fallback preserva essa base. Assim, os criterios nao
reescalam a area protegida por causa de uma tentativa local. Continua exigindo
ganho osseo global >=0,001 e perda corporal <=0,015 em relacao a base, alem de
Jacobiano/amplitude. Melhorias locais pequenas podem ser rejeitadas por esses
criterios agregados; nao e garantia de melhora anatomica em todas as regioes.

O seletor `Etapa da deformacao` inclui uma etapa `adaptativa` ANTES dos criterios
finais. O campo salvo ja combina a malha geral e os patches; a grade visualizada
mostra tambem os nos locais. Resultados guardam `adaptive_regions`,
`base_step_scale`, `adaptive_step_scale` e os campos intermediarios. As malhas
de controle incluem arrays `patch_N`; regioes/indices constam na auditoria.

CLI: `--regions arquivo.json`, formato `{"regions":[{"photo_path":"caminho absoluto",
"mode":"refine","rect":[900,500,240,240],"nodes":13}]}`. Use `mode: protect`
para protecao. A UI salva uma copia exclusiva `<experimento>.regions.json`;
seu caminho/hash e as definicoes ficam na auditoria, sem modificar entradas.
Fotos ausentes ou excluidas por falta de osso causam erro explicito, nao aplicacao
silenciosa em outro corte. Execucoes sem regioes mantem o comportamento anterior.

## Detalhes do algoritmo

Os pesos padrao agora sao iguais: corpo = 1 e osso = 1 (antes, osso = 2).
Os demais limites e regularizadores permanecem inalterados. O piloto
`regional_pilot_001` foi gerado com os pesos anteriores; mudar o padrao nao
recalcula resultados existentes.

- Fotografia inteira fixa e DICOM original móvel. A inicialização é calculada
  pelo motor automático, sem matrizes manuais de sessões do AlignImages.
- Referência óssea CT experimental: HU ≥150 dentro da máscara corporal. O
  limiar é configurável e ainda exige inspeção, especialmente em osso fino.
  Máscaras vazias são excluídas com motivo no `audit.json`.
- As hipóteses globais podem ser reordenadas por sobreposição corporal/óssea
  dentro do suporte anotado. A área exterior ao antigo recorte é desconhecida
  e não participa como negativo na perda óssea.
- Uma ROI comum à sequência recebe malhas bicúbicas 7×7 e 13×13, otimizadas em
  conjunto. Diferentes regiões recebem diferentes deslocamentos suaves.
- Objetivo: correspondência de corpo/osso e penalidades de magnitude,
  gradiente/suavidade, distorção óssea, Jacobiano e variação entre cortes.
- O campo se anula suavemente nas bordas. Limite padrão de deslocamento:
  32 pixels da foto; não é um limite em mm.
- Continuidade: vetores residuais são convertidos para mm paciente pela
  geometria DICOM e inversa da componente global; a diferença é ponderada pela
  distância entre planos. Não acopla lacunas acima de 2 mm, séries distintas
  ou orientações incompatíveis. Mudança apenas de PixelSpacing não rompe o
  vínculo. Essa conversão depende do alinhamento global e não representa
  calibração XY independente da fotografia.

## Critérios e limitações

A avaliação final usa a resolução original: Jacobiano residual entre 0,3 e 3,
deslocamento dentro do limite, perda de Dice corporal até 0,015 e ganho de Dice
ósseo mínimo 0,001. Tenta passos menores; se nenhum satisfizer os critérios,
mantém o global. A continuidade após essa seleção fica registrada.

Passar nesses critérios produz um candidato para revisão, não aprovação
anatômica. A máscara alvo participa da otimização: sua sobreposição não é
validação independente. Ainda é necessário conferir landmarks reservados,
anatomia e a referência óssea obtida por limiar. O refinamento não corrige
pareamento Z errado, ausência de anatomia ou orientação global incorreta.

## Contrato de transformação

As matrizes `forward_matrix_3x3`/`inverse_matrix_3x3` do resultado representam
somente a componente global. O mapeamento completo exige `local_deformation`
e o arquivo `local_field.npz`, cujo hash é verificado pelo visualizador.

O campo é um deslocamento **inverso residual em pixels da foto**, com ROI e
grade explícitas:

`amostra_CT = inverse_global(pixel_foto_saida + residual(pixel_foto_saida))`

Não se deve usá-lo como um deslocamento forward ou simplesmente negar seu
sinal para invertê-lo. Não há exportação forward não linear de pontos nem
inversão numérica da malha implementadas. O Jacobiano verificado é o do
residual inverso, independentemente de eventual reflexão global de aquisição.

Matriz e campo são compostos antes de amostrar a CT original, evitando duas
reamostragens. HU permanece float32 em `aligned_ct_hu.npz`, acompanhado de
suporte. Rótulos usam nearest-neighbor. O preview DICOM interpola HU antes da
janela, reproduzindo essa exportação; Head247 foi conferido pixel a pixel.

`audit.json` registra entradas/hashes, configuração, função objetivo, vínculos
físicos, métricas, passos e exclusões. `control_lattices.npz` guarda nós e
conversões de vetores. Ctrl+C registra cancelamento. Saídas usam pasta nova.

## Piloto atual

`out/deformable/regional_pilot_001`: 32 cortes, Head100 e Head234–Head264, com
30 vínculos em Z, incluindo 235/236 e 259/260. Head005 foi excluído porque a
máscara alvo está vazia; o baseline global foi preservado no experimento de
origem. A escolha de matrizes manuais antigas não participa deste pipeline.

- Dice ósseo médio: 0,7674 → 0,8715 (métrica de otimização).
- Deslocamento máximo: aproximadamente 19,2 pixels.
- Jacobiano residual mínimo: 0,389; dentro das máscaras ósseas, mínimo 0,769.
- Todos os candidatos passaram sem redução do passo.
- Maior RMS de variação pós-seleção: aproximadamente 0,237 mm/mm nos vínculos.

O visualizador abre esse conjunto por padrão. `Aplicar refino regional
(candidato)` alterna global/local; `Mostrar malha de deformação` mostra a
variação espacial. Contornos e máscaras móveis acompanham o campo; métricas
antes/depois aparecem no diagnóstico. A navegação e os quatro modos de
composição continuam disponíveis.

Próximas verificações: anatomia e landmarks independentes, qualidade da
referência óssea HU, calibração dos pesos/limites e avaliação em outras regiões
antes de estender o processamento para toda a sequência.

Referências de implementação: [amostragem de campos no PyTorch](https://docs.pytorch.org/docs/main/generated/torch.nn.functional.grid_sample.html)
e [regularização de registro no elastix](https://elastix.dev/doxygen/group__Metrics.html).

# Plano de refinamento deformável CT → fotografia

Registro de contexto e proposta em 22/10/2026. **Status: módulo experimental dedicado iniciado; preparação e revisão do baseline implementadas, motor deformável pendente.** O usuário autorizou começar o trabalho. Isso não autoriza substituir resultados aceitos.

## Atualizacao: resolucao adaptativa manual implementada

O modulo experimental agora aceita retangulos de refino/protecao por fotografia.
Apos a malha geral, otimiza patches locais mais finos com bordas suaves e
continuidade fisica regularizada; protecao impede somente o incremento adaptativo.
Auditoria, etapas intermediarias e gates separados preservam o resultado geral.
Interface, formato e limitacoes em `experimental/deformable/REGIONAL_REFINEMENT.md`.
Deteccao automatica de regioes e propagacao manual entre cortes continuam pendentes.

## Objetivo atualizado: alinhamento autônomo desde imagens não alinhadas

O usuário esclareceu que deseja alinhar dados ainda sem registro. O módulo não deve exigir matrizes ou landmarks de uma sessão existente. A proposta anterior de começar obrigatoriamente pelo refinamento de uma sessão aceita fica superada como fluxo principal. O leitor e os relatórios existentes continuam úteis para diagnóstico e avaliação.

- Entrada principal: pilhas de fotografia e CT, geometria/espaçamento e referências anatômicas disponíveis. Sessão alinhada é opcional e destinada à avaliação; modo autônomo não deve ler suas matrizes para inicializar.
- Separar o problema de correspondência entre cortes do registro espacial dentro de cada par. Para o primeiro motor autônomo, aceitar tabela de pares ou offset explícito sem exigir transformações. Descoberta automática de correspondência longitudinal é outro componente, ainda não implementado.
- Pipeline proposto: geometria física/FOV e orientação → máscaras corporais/ósseas → múltiplas hipóteses automáticas de posição/rotação/escala → otimização global multirresolução → deformação local limitada e regularizada em XY/Z → avaliação de confiança e revisão.
- Busca global deve testar hipóteses de orientação plausíveis, resolver simetrias com informação óssea e da sequência e preservar hipóteses concorrentes quando houver ambiguidade. Reflexões somente quando compatíveis com a preparação dos dados e claramente identificadas.
- Continuidade longitudinal deve influenciar escolha das hipóteses e refinamento; não propagar cegamente a solução de um corte incerto. Anatomia, limites de deformação e cobertura precisam concordar.
- Testar recuperação de perturbações conhecidas em pares de referência, faixas contíguas incluindo mudanças de FOV e pares novos. Não usar matrizes da avaliação como entrada oculta; perturbações sintéticas não demonstram por si só generalização para novos dados.
- Robustez é critério a medir: acerto anatômico, estabilidade, detecção de falhas/ambiguidade, ausência de dobras e reprodutibilidade. Melhor score não basta para aprovação.
- Próxima entrega funcional passa a ser inicialização global automática sem sessão, seguida de avaliação antes de liberar o ajuste deformável. A escolha entre sessões antigas não bloqueia esse desenvolvimento.

## Primeira entrega

### Estado atual: refinamento regional implementado

Esta seção atualiza as pendências históricas do documento: o motor regional
agora existe. Detalhes e contrato: `experimental/deformable/REGIONAL_REFINEMENT.md`.

- Malhas bicúbicas 7×7/13×13 com deslocamentos regionais e perdas de corpo/osso;
  referência CT por limiar HU ≥150 dentro do corpo, ainda experimental.
- Suporte de anotação exclui exterior ao antigo recorte. Penalidades de
  magnitude, gradiente ósseo, suavidade, Jacobiano e continuidade em Z.
- Continuidade compara vetores em mm paciente via geometria DICOM e inversa
  global, ponderada pela distância entre planos; não confundir com calibração
  independente XY da fotografia. Mantém vínculo através de mudança de FOV.
- Composição inversa antes da amostragem do DICOM original; campo residual
  guardado além da matriz global. Gates em resolução original e possibilidade
  de reduzir passo/rejeitar o candidato, sem aceitar anatomia automaticamente.
- Piloto `out/deformable/regional_pilot_001`: 32 cortes (100 e 234–264), 30
  vínculos. Head005 excluído por máscara óssea vazia. Dice ósseo 0,7674→0,8715,
  máximo deslocamento ~19,2 px, Jacobiano mínimo 0,389 (0,769 nas máscaras ósseas).
- Viewer: comparação global/local, malha, máscaras/contornos deformados,
  métricas e janela DICOM. Conferência Head247: preview/exportação idênticos.
- Validação anatômica independente e extensão para toda a sequência continuam
  pendentes. As seções anteriores de proposta e primeiras entregas são histórico.

### Retorno ao espaço das fotografias originais

**Correção posterior do usuário (prevalece):** todas as fotografias tratadas têm exatamente o mesmo recorte em relação às originais; a sequência original já tem alinhamento anatômico. A tabela abaixo contém resultados de localização automática por template, e suas exceções NÃO são mudanças reais de crop. Não introduzir deslocamentos por corte.

- Recorte comum confirmado: x=699, y=183, largura=650, altura=850. Nas seis exceções históricas (0,3,8,11,13,16), a correlação com o recorte comum fica acima de 0,9988, superando as posições estimadas. Exemplo 0: estimada −0,059 versus comum 0,99885; exemplo 11: estimada 0,248 versus comum 0,99936.
- `restore_photo_space.py` corrigido para usar uma única translação, configurável por `--crop`, em toda a sequência. Usa a tabela somente para correspondência de nomes e diagnóstico histórico.
- Restauração corrigida: `out/deformable/original_photo_space_002`. `_001` é superada para uso geral porque seis máscaras foram colocadas a partir de estimativas erradas. Os oito cortes do piloto não tinham essas exceções, mas suas referências serão atualizadas para o conjunto corrigido.
- Verificação completa concluída: as 265 fotos têm correlação de gradientes ≥0,99885 no recorte comum; 265 máscaras preservadas exatamente, sem variações de translação. Piloto DICOM com fotos inteiras e referências corrigidas em `out/deformable/dicom_full_photo_pilot_002`, agora padrão do visualizador.

- Usuário observou anatomia cortada e pediu fotografias originais e máscaras no mesmo espaço. Tabela encontrada: `../vision-app-Imagens_segmentadas/Informacoes_processamento/_relatorios_consolidacao/crop_digital_por_slice.csv`, documentada no relatório de processamento desse projeto.
- Fotografias originais: `C:\Users\amont\OneDrive\Mestrado PPGTIC\Arquivos de Desenvolvimento\Raw_Data_PNG\Fotografias Originais\1001.png` a `1265.png`, 2048×1216. Amostras 1001/1237/1265 são idênticas em pixels às de `D:\Visible Human Project\Renders\Digital_Photography_PNG_Format`.
- Tabela tem 265 estimativas de localização: 259 em (699,183), três em (699,182), exceções em (1398,0), (699,178) e (698,173), todos 650×850. As seis posições diferentes são erros de localização por template; usar o recorte comum confirmado.
- Scores históricos de template abaixo de 0,9 em 0,1,2,3,4,11. No piloto 5,100 e oito cortes obrigatórios, correlação de magnitude de gradientes entre foto tratada e recorte original acima de 0,9994; isso confirma fortemente a localização para esses exemplos apesar de cor/fundo alterados.
- Novo `restore_photo_space.py` repõe máscaras pela mesma translação inteira em toda a pilha, preserva rótulos/foreground/round-trip, grava suporte de anotação separado e manifesto com hashes e qualidade. Área exterior ao recorte é desconhecida, não negativo ósseo. Não há recuperação de estruturas cortadas da anotação.
- Diretório novo da restauração: `out/deformable/original_photo_space_001`. Fotos são referenciadas em sua origem, sem cópia nem sobrescrita. `run_dicom_pilot.py` agora aceita padrão de fotos `{source_number}.png` e máscaras restauradas de referência somente para visualização.
- Extração de corpo fotográfico atualizada para excluir equipamento escuro no exterior do fundo azul, usando semente de tecido quente e maior componente preenchido. Continua exigindo conferência.

### Fonte de CT alterada para DICOM original por solicitação do usuário

- Implementados `dicom_ct.py` e `run_dicom_pilot.py`; CLI `run_global.py` aceita DICOM e extrai máscara diretamente dos valores reescalonados em HU. Janela 40/400 é usada somente para as imagens de exibição.
- Valida CT de quadro único, PixelSpacing/posição/orientação, slope/intercept e padding; guarda metadados e matriz de pixels nativos para coordenadas paciente.
- Máscaras são normalizadas em grade isotrópica de 0,527344 mm no plano. Transformações/hipóteses finais são compostas de volta para pixels nativos DICOM → fotografia. Não foi implementada normalização de FOV em grade paciente comum nem continuidade Z.
- Reprocessados os oito cortes de verificação, usando tabela de proveniência existente somente para parear foto/DICOM, sem matrizes salvas: `out/deformable/dicom_pilot_001`. Preserva HU alinhado em NPZ e suporte; PNGs são derivados de exibição. O visualizador agora abre esse conjunto por padrão e habilita presets DICOM reais.
- Treze testes aprovados; inclui signed pixels, rescale, padding, geometria física e composição de grade anisotrópica. Layout DICOM conferido em `out/deformable/viewer_dicom_smoke.png`.

### Visualizador desktop dedicado

- Usuário pediu app interativo com as opções de visualização do AlignImages. Implementados `experimental/deformable/viewer.py` (PySide6) e `review_model.py`, lançador `ReviewResults.cmd`.
- Três painéis, quatro modos de composição, alpha/checker, contornos por painel, Fit/1:1/Reset, zoom/pan sincronizáveis, timeline dupla, reprodução, hipóteses, alinhamento ligado/desligado, MI, diagnóstico recolhível e escala de UI.
- Janela/nível DICOM e presets quando fonte é DICOM; PNG tem ajuste de exibição explicitamente sem HU. Aceita result.json e manifest.json do preparador. Nenhuma edição de transformações ou recomputação de registro.
- Testes totais: 11 aprovados (3 motor, 6 preparação, 2 modelo/interface Qt). Captura de layout em `out/deformable/viewer_smoke_v2.png`; contornos, modos, hipóteses, sincronização, navegação e 1:1 exercitados nos testes. Capturas offscreen exigem fonte Segoe UI carregada explicitamente no Windows.

### Motor global autônomo iniciado

- Implementados `experimental/deformable/global_registration.py` e CLI `run_global.py`: sem argumento de sessão ou matriz inicial; máscaras corporais automáticas ou fornecidas e orientação óssea opcional somente quando disponíveis ambas as máscaras.
- Busca de 24 ângulos com múltiplas hipóteses, otimização afim de distâncias simétricas de contornos, restrições leves de anisotropia/shear e reflexão vertical apenas mediante opção explícita. Saídas guardam hipóteses/avisos, inputs/hashes e forward/inverse em pixels.
- Três testes do motor passam: perturbação conhecida com rotação 115°, escala/translação e grade reduzida; reflexão explícita com referência óssea; rejeição de máscara vazia ou apenas uma máscara óssea. Os seis testes anteriores continuam passando.
- Piloto real sem matrizes: Head234,235,236,237,240,250,260,264 ↔ PNG CT de número Head+1001, usando apenas máscaras corporais extraídas. Dice corporal 0,949–0,982; todos exigem revisão, sem validação da anatomia interna.
- Com espelhamento permitido, candidatos vencedores em 260/264 têm determinante negativo e nos seis anteriores positivo: evidência de ambiguidade de orientação ainda não resolvida, apesar de boa sobreposição externa. Não considerar o motor robusto/concluído com base no Dice.
- Galeria: `out/deformable/autonomous_pilot_002/review.html`, gerada por `review_global_runs.py`. Ainda faltam máscaras ósseas fonte confiáveis, normalização física/FOV, acoplamento longitudinal e deformação residual. A execução atual não lê a sessão, mas recebe explicitamente o par; não descobre offset.

- `experimental/deformable/prepare.py`: leitor de sessões antigas/atuais, auditoria dos pares explícitos, orientação e transformação forward, relatório HTML local navegável, inventário DICOM e vínculo pela tabela de proveniência existente.
- Comando e limitações em `experimental/deformable/README.md`; testes de contrato em `test_prepare.py`.
- Execução real inicial em `out/deformable/baseline_001`: 260 pares com caminhos/matrizes verificados, dez exemplos renderizados, cinco pares inválidos; inventário de 844 DICOMs sem erro de leitura. As imagens ainda exigem revisão anatômica.
- Confirmada por metadados associados via CSV a transição Head235/236: pixel spacing 0,527344 → 0,78125 mm e origem XY [-135,-165] → [-200,-200], com Z -250 → -251 mm. O vínculo CSV não prova que o PNG preserve orientação e geometria da origem.
- Execução final com diagnóstico de transições: `out/deformable/baseline_002/review.html`; detectou mudanças de PixelSpacing em 235→236 e 259→260. Ambas com distância entre planos de 1 mm. A segunda transição também exige revisão antes de normalizar a geometria.
- Ainda faltam a confirmação anatômica da indexação das máscaras, a máscara óssea CT fonte, calibração física completa da fotografia, motor residual e acoplamento longitudinal. A preparação não normaliza FOV nem calcula deformações.

## Contexto confirmado

- Projeto: `C:\Dev\mestrado\alinhamento\Align_Images`. Último commit conferido: `2d4054a`, enviado a `origin/main`; worktree limpo antes deste documento.
- O usuário trabalha principalmente com landmarks e interpolação de transformações entre cortes com landmarks. Deseja simplificar a interface antes de acumular novas funcionalidades.
- Objetivo: corrigir diferenças locais entre CT e fotografia, com a menor deformação possível; correções globais de posição, orientação e escala podem ser insuficientes.
- O app atual usa transformações globais e exportação afim; contornos Canny são auxílio visual, não máscaras anatômicas.
- Não é necessário treinar rede neural para o primeiro experimento. Inicialização automática foi discutida como possibilidade futura; o experimento proposto aproveita o alinhamento existente.

## Dados fornecidos e inspeção realizada

Sessão original (somente leitura; usar cópia em testes):

`C:\Users\amont\OneDrive\Mestrado PPGTIC\Arquivos de Desenvolvimento\Head_DG\session_autosave.json`

- Fotografia fixa: `...\Head_DG\Digital`, 265 PNGs, `Head000.png`–`Head264.png`. Amostras 000, 100 e 264: 650 × 850, uint8 RGBA.
- CT móvel: `...\Head_DG\TC_Original`, 260 PNGs, `1006.png`–`1265.png`. Amostras 1006, 1106 e 1265: 512 × 512, uint8 grayscale.
- Sessão: offset global −5, 260 pares válidos, 261 registros (257 `affine_prior_refined`, 4 `manual_landmarks`). Quantidade de registros não equivale à de pares válidos; selecionar pelo par, não pela posição na lista.
- A sessão descreve CT original + transformações; não foi confirmado o diretório da CT já exportada/alinhada mencionado pelo usuário.

Máscaras ósseas alvo:

`E:\Test_Muscle\Osso_Candidato_1`

- 265 PNGs `slice_0000.png`–`slice_0264.png`; amostras 0000, 0100 e 0264: 650 × 850, uint8. A amostra 0000 está vazia.
- **Informação do usuário:** originadas da CT, depois refinadas manualmente para a fotografia. São uma referência corrigida, mas não inteiramente independente do alinhamento inicial.
- Mesmo tamanho e quantidade não provam associação anatômica: verificar explicitamente se `slice_NNNN` corresponde a `HeadNNN`.
- Áreas corrigidas manualmente fornecem informação adicional; áreas sem edição podem reproduzir erros do alinhamento inicial. Não há mapa de edições confirmado.

Diretório DICOM informado:

`D:\Visible Human Project\nlm_visible_human_project - Working Folder\VHP-M\1.3.6.1.4.1.5962.1.2.32.1672334394.26545\CT_1.3.6.1.4.1.5962.1.3.32.4.1672334394.26545`

- Existem 844 arquivos DICOM. Um arquivo amostrado: modalidade CT, 512 × 512, espaçamento 0,9375 mm e espessura 1 mm.
- Isso não confirma o mapeamento desses arquivos aos PNGs nem o espaçamento de toda a série. Verificar tags e correspondência física; não ordenar por UUID ou extrapolar metadados de uma amostra.

## Correspondência obrigatória

### Diagnóstico posterior do baseline

- O usuário relatou desalinhamento importante no primeiro relatório. A sessão indicada na raiz de Head_DG foi modificada em 13/04/2026, usa TC_Original, com 257 registros automáticos e quatro manuais; não representa necessariamente o trabalho posterior com landmarks.
- Encontrada candidata de 13/05/2026 em `Head_DG\TC_tratado_ALinhado\session_autosave.json`: usa `TC_Tratado\quadroNNNN.png`, contém 38 `manual_landmarks`, 222 `interpolated` e dois `similarity`. Usuário ainda precisa confirmar qual sessão adotar; não substituir silenciosamente a entrada.
- Comparação inicial de 11 cortes, incluindo 129 e os oito cortes da transição exigidos no AGENTS.md: reprodução da candidata idêntica pixel a pixel às imagens `TC_tratado_ALinhado\moving_to_fixed\CT_NNN.png`. A exportação avulsa `movingB_to_fixedA_B124_A129.png` difere do resultado atual, mostrando que exportações distintas podem corresponder a revisões distintas.
- Conferência completa concluída: os 260 pares válidos da candidata reproduzem exatamente as 260 exportações `moving_to_fixed/CT_NNN.png`, com erro máximo e médio de pixels zero. Isso confirma a reprodução dessas exportações, não a precisão anatômica nem que essa é a sessão escolhida pelo usuário.
- O primeiro relatório usou blend 50%, enquanto a sessão de abril salva modo multiply e a candidata de maio salva difference. O módulo agora inclui opção de modo salvo para distinguir composição visual de transformação geométrica.
- Comparador dedicado `experimental/deformable/compare_sessions.py`; resultados em `out/deformable/session_diagnosis_001`. Esta investigação não executa registro nem deformação.
- Relatório da candidata com opção de modo salvo: `out/deformable/manual_session_review_002/review.html`. Seis testes de contrato passam. Próximo passo depende de confirmar a sessão baseline e conferir máscaras/fonte da CT.

- Relação nominal: índice móvel = índice fixo −5.
- `Head005.png` ↔ índice móvel 0 ↔ `1006.png`; `Head264.png` ↔ índice móvel 259 ↔ `1265.png`.
- `Head000`–`Head004` não têm correspondente nominal nessa pilha de CT.
- Ler pares válidos e seus `fixed_index`/`moving_index` no JSON; buscar a transformação pelo respectivo par. Não parear por posição de arquivo e não aplicar o offset novamente aos pares já resolvidos.
- Se confirmada a indexação das máscaras, `slice_0005.png` será alvo de `1006.png` no espaço de `Head005.png`.
- Convenções existentes: Stack A fixa/fotografia; Stack B móvel/CT; matriz forward móvel → fixa; inverse fixa → móvel.
- Respeitar orientação, flips, rotação, redimensionamento e grade utilizados para gerar os PNGs. Confirmar alinhamento reproduzido antes de qualquer deformação.
- Na região de mudança de FOV conferir 234, 235, 236, 237, 240, 250, 260 e 264, conforme AGENTS.md; não impor suavidade cega através da transição 235/236.

## Comportamento proposto

1. Assistente de preparação lê cópia da sessão e diretórios, valida arquivos, pares, dimensões, indexação e máscaras. Mostra ausências, máscaras vazias e pares sem referência útil. Não inventa máscaras nem correspondências.
2. Reproduz baseline global em exemplos e mostra fotografia, CT alinhada e máscaras sobrepostas para conferência. Mantém separados alinhamento original e candidato.
3. Obtém máscara óssea da CT no espaço correto. Método ainda a escolher: aproveitar segmentação original disponível ou extrair do DICOM com método validado. Threshold em PNG de 8 bits não equivale a threshold HU. Não usar a máscara alvo como fonte, pois isso eliminaria a referência independente da correção.
4. Obtém contorno corporal nas duas modalidades; propõe extração automática e revisão manual. Fundo azul, suporte, buracos e componentes desconectados exigem conferência. Canny não substitui essa máscara.
5. Ajusta deformação residual após a transformação global, começando da identidade. Candidata: malha B-spline 2D grosseira, com refinamento multiescala e orçamento limitado de deformação.
6. Função objetivo proposta combina discrepância óssea, discrepância do limite corporal, deslocamento, suavidade e preservação local da forma óssea; landmarks opcionais com peso/confiança explícitos. Pesos e limites ainda não escolhidos. Métricas possíveis: distância entre superfícies/campos de distância e sobreposição. Ossos finos não devem ser avaliados apenas pelo Dice.
7. As restrições devem desestimular deformação óssea e compressões excessivas. Não tratar a união de todos os ossos como um único objeto rígido; diferentes ossos podem mudar de posição relativa. Correspondências ambíguas, ausências e regiões fora do campo precisam de exclusão/confiança.
8. Parada por estabilidade, limite de iterações, cancelamento ou violação de qualidade. Melhor sobreposição não implica aceitação automática. B-spline suave por si só não garante ausência de dobras; verificar Jacobiano e rejeitar candidato inadequado. Caso necessário, avaliar parametrização difeomórfica.
9. Aplica uma mesma transformação final à CT e segmentações associadas. Compor global e residual e reamostrar uma vez a partir da fonte quando possível; interpolação contínua para imagem e nearest-neighbor para rótulos. Máscaras alvo/fotografias permanecem referências fixas.
10. Mostra antes/depois, alternância rápida, contornos coloridos, grade deformada, intensidade dos deslocamentos e métricas. Usuário aceita ou descarta candidato por corte.
11. Batch cancelável somente após piloto; registra resultado e falhas por par. A continuidade entre deformações é requisito do método para sequências, não apenas acabamento posterior. Um piloto isolado pode verificar o motor, mas a avaliação da sequência deve incluir acoplamento longitudinal no espaço físico.

## Continuidade física e mudanças de campo de visão

Requisito explicitado pelo usuário: ambas as sequências possuem espaçamento conhecido; usar a continuidade entre deformações como restrição. A CT tem regiões com proporções de exibição diferentes, por exemplo cabeça e tórax. Esses saltos de aquisição precisam ser considerados antes de avaliar continuidade.

- Reconstruir geometria por corte a partir dos DICOMs: PixelSpacing, ImagePositionPatient e ImageOrientationPatient, além do vínculo com cada PNG. Derivar distância entre planos por posições projetadas na normal; SliceThickness não substitui automaticamente o espaçamento real.
- Registrar também espaçamento e orientação das fotografias, correspondência Z, cortes ausentes, crops, resize e flips empregados nos PNGs. Espaçamento conhecido não implica que as pilhas compartilhem origem, orientação ou posição Z.
- Usar uma grade física comum para comparação, ou converter explicitamente os campos para coordenadas físicas comuns sem reamostrar antecipadamente. Uma cabeça que ocupa mais pixels pode apenas ter sido adquirida com FOV menor; não interpretar isso como deformação anatômica.
- Manter conceitualmente separadas a geometria de aquisição, a transformação global já aceita por par e a deformação anatômica residual. Compor os mapeamentos sem aplicar duas vezes a correção de escala/FOV já incorporada na sessão.
- Regularizar a variação do campo residual entre planos correspondentes em função da distância física em Z e na mesma localização física XY. Não suavizar diretamente coeficientes/matrizes ou vetores em pixels de grades distintas. Amplitude em mm requer calibração XY também para o domínio de destino; se faltar, informar unidades de pixel e não inventar conversão.
- Implementação candidata: campos 2D acoplados longitudinalmente (deslocamentos XY variando com Z), sem deslocamento Z na primeira versão. Não afirmar registro 3D completo. Ponderar continuidade por distância, confiança e disponibilidade de dados; não eliminar variações anatômicas reais.
- Detectar blocos de geometria constante e mudanças de FOV/orientação/origem para diagnóstico. Após normalização física correta, uma mudança apenas de FOV não exige quebra da continuidade anatômica. Reduzir ou interromper o acoplamento somente se houver lacuna física, mudança de cobertura, correspondência incerta ou descontinuidade real ainda não resolvida.
- Se a geometria não puder ser reconstruída, tratar blocos separadamente provisoriamente e sinalizar transições para revisão; não forçar continuidade através de um salto sem explicação.
- Validar baseline e candidato nos cortes 234, 235, 236, 237, 240, 250, 260 e 264. Comparar deslocamentos físicos e anatomia, não só tamanho aparente em PNG.

## Persistência e exportação propostas

- Matriz 3 × 3 não representa deformação local. Manter baseline afim e armazenar residual, convenção de coordenadas, direção, domínio/grade, unidades, versão do algoritmo, parâmetros, entradas e diagnósticos.
- Composição geométrica proposta: CT original → transformação global → deformação residual no espaço da foto. Backend pode requerer mapeamento inverso para reamostragem; converter explicitamente sem mudar a semântica forward atual.
- Inversa deformável não é inversa de matriz: calcular/verificar numericamente se necessária; não prometer exportação reversa antes de validar o erro de composição.
- Campos grandes em arquivos auxiliares referenciados pela sessão, com integridade e caminhos portáveis; formato ainda a escolher. Leitura compatível com sessões antigas.
- Salvar candidato e exportações em diretórios novos; histórico com parâmetros, pares afetados, aceitação/rejeição e possibilidade de retornar ao baseline. Nunca sobrescrever dados de entrada, sessão original ou exportações aceitas.

## Validação e implantação gradual

- Primeira entrega proposta: protótipo externo em poucos pares representativos, sem redes neurais e sem mudar o app. Escolher exemplos com discrepância local real e máscaras confiáveis; conferir também a mudança de FOV.
- Registrar baseline, erro de landmarks reservados para avaliação, distâncias ósseas/corporais, sobreposição, deslocamento e Jacobiano antes/depois. Separar pontos usados para guiar o ajuste dos usados para avaliar.
- Não exigir coincidência perfeita: planos distintos, tecido ausente e diferenças de segmentação podem impedir correspondência física.
- Só integrar ao app após demonstrar benefício anatômico com deformação aceitável. UI proposta como refinamento opcional no fluxo de landmarks, com poucos controles, sem reintroduzir todas as automações na tela principal.
- Pendências antes de implementar: confirmar indexação/proveniência das máscaras; identificar segmentação CT fonte; mapear DICOM ↔ PNG/orientação; localizar eventual CT já exportada; validar baseline; escolher backend, métricas, limites, cortes piloto e critérios de aceitação.

## Referências técnicas

- SimpleITK registration overview: https://simpleitk.readthedocs.io/en/v2.3.0/registrationOverview.html
- SimpleITK B-spline example: https://simpleitk.readthedocs.io/en/main/link_ImageRegistrationMethodBSpline1_docs.html
- elastix penalties (bending energy/rigidity): https://elastix.dev/doxygen/parameter.html

Essas referências fundamentam alternativas técnicas; não constituem escolha definitiva do backend.

# Plano: Score de Registro por Informação Mútua (MI)

Documento de handoff para uma sessão futura implementar um score de
similaridade baseado em informação mútua (MI) dentro do `RegistrationEngine`,
como alternativa ou complemento ao score atual (sobreposição de máscara +
concordância de gradiente). Não requer mudanças no otimizador, na pirâmide,
nas âncoras manuais nem na interpolação entre fatias — só na função de score.

## Por que considerar MI

O score atual (`ComputeCombinedScore`, `RegistrationEngine.cpp`) é
`0.7 * IoU(máscara) + 0.3 * concordância_de_gradiente`. Funciona bem quando as
duas modalidades têm silhueta externa bem definida e bordas anatômicas
correspondentes visíveis nos dois lados (CT e foto digital), mas não usa
nenhuma relação estatística entre as intensidades das duas imagens — só forma
e borda.

Informação mútua mede dependência estatística entre as intensidades de duas
imagens **sem assumir nenhuma relação específica entre elas** (linear,
idêntica, etc.), o que a torna adequada especificamente para pares
multimodais como CT × foto, onde não existe correspondência direta de
intensidade. Foi validada extensamente na literatura de imagem médica desde
1997 para exatamente esse tipo de par (CT/RM/PET).

## Referências acadêmicas

1. **Maes, F., Collignon, A., Vandermeulen, D., Marchal, G., & Suetens, P.
   (1997). "Multimodality Image Registration by Maximization of Mutual
   Information." *IEEE Transactions on Medical Imaging*, 16(2), 187–198.**
   DOI: 10.1109/42.563664. PDF local:
   `C:\Dev\mestrado\segmentacao\SAM_Segmentation\docs\academic\Articles\Multimodality_image_registration_by_maximization_of_mutual_information.pdf`

   Paper fundador do critério de MI para registro multimodal rígido. Define a
   métrica, a interpolação por partial volume (PV) para o histograma
   conjunto, a estratégia de otimização (Powell + Brent) e valida precisão
   subvoxel contra registro por marcadores estereotáticos em CT/RM/PET de
   cérebro, sem segmentação ou pré-processamento prévio.

2. **Chen, H., & Varshney, P. K. (2003). "Generalized Partial Volume
   Estimation of Joint Histogram for Mutual Information Based Registration
   of Brain Images." *IEEE Transactions on Medical Imaging*, 22(9),
   1111–1119.** DOI: 10.1109/TMI.2003.816949.

   Refina a interpolação PV do paper 1 (GPVE, ordem 2 ou 3) para eliminar
   artefatos periódicos (mínimos locais falsos) que aparecem na superfície de
   MI quando plotada em função do deslocamento, causados por interpolação
   linear/PV simples. É o método de histograma conjunto usado no paper 3
   abaixo.

3. **Chen, H., Chen, Y.-F., & Gao, J. (2004). "On the Development of a Fully
   Automated Cryosection Image Registration System." *Proceedings of the
   2004 IEEE International Conference on Systems, Man and Cybernetics*, pp.
   3469–3474.** IEEE Xplore: documento 1400879. PDF local:
   `C:\Dev\mestrado\segmentacao\SAM_Segmentation\docs\academic\Articles\On_the_development_of_a_fully_automated_cryosection_image_registration_system.pdf`

   Aplica MI + GPVE ao registro de cryosections consecutivas do Visible Human
   Project (macho, dados 70mm). Contribuição principal para este projeto:
   usa segmentação grosseira de silhueta (limiar de canal de cor) e o centro
   de massa resultante para estimar o ponto de busca inicial de cada
   otimização — a mesma ideia já implementada aqui em
   `ComputeInitialGuess`/`ComputeInitialGuessAffine` via `MaskStats`. Reporta
   também o modo de falha típico: fatias com pouco tecido / imagens
   corrompidas nas junções de seção geram máscaras deficientes e exigem
   correção manual do ponto inicial — mesma categoria de problema já tratada
   aqui pelas âncoras manuais (`is_manual`) e pelo `sigma_multiplier`.

## Teoria (resumo mínimo necessário para implementar)

Sejam `F` (imagem flutuante, aqui a CT já deformada por uma transformação
candidata) e `R` (imagem de referência, a foto). MI é definida como:

```
I(F,R) = H(F) + H(R) - H(F,R)
```

onde `H(F)`, `H(R)` são as entropias marginais e `H(F,R)` a entropia
conjunta, todas calculadas a partir do histograma conjunto normalizado
`p_{F,R}(f,r)`:

```
H(F)    = -sum_f p_F(f)   * log(p_F(f))
H(R)    = -sum_r p_R(r)   * log(p_R(r))
H(F,R)  = -sum_{f,r} p_{F,R}(f,r) * log(p_{F,R}(f,r))
```

O critério de registro é: a transformação ótima é a que **maximiza**
`I(F,R)`. Não há suposição sobre a relação entre `f` e `r` além de
dependência estatística — é isso que permite comparar CT (densidade) com
foto (luminância) sem normalizar intensidades entre modalidades.

### Estimação do histograma conjunto — ponto crítico

Usar nearest-neighbor ou interpolação linear simples para reamostrar a
imagem flutuante introduz **artefatos periódicos** na superfície de MI em
função do deslocamento (mínimos locais falsos, ver Fig. 3a/3b do paper 3) —
isso prejudica qualquer otimizador local, incluindo a busca em grade já
implementada em `RefineParameters`.

Solução mínima viável: **interpolação por partial volume (PV)** — em vez de
arredondar a amostra pra um único bin do histograma, distribui a
contribuição da amostra pelos até 4 bins vizinhos (2D), ponderada pela
distância bilinear. Solução melhor (paper 2): **Generalized Partial Volume
Estimation (GPVE)** de 2ª ordem, que suaviza ainda mais a superfície.
Recomenda-se começar com PV simples (mais barato, já elimina a maior parte
do artefato) e só evoluir para GPVE se a superfície de score ainda mostrar
mínimos falsos na prática.

## Onde entra no código atual

Arquivo: `src/registration/RegistrationEngine.cpp`.

1. **Nova função de score**, no mesmo padrão de `ComputeMaskOverlapScore` e
   `ComputeGradientAgreementScore` (linhas 341–390):

   ```cpp
   double ComputeMutualInformationScore(const cv::Mat& movingImage,
                                        const cv::Mat& fixedGray,
                                        const Transform2D& transform,
                                        int numBins = 64);
   ```

   - Recebe a CT (`movingImage`, ainda não deformada) e a foto já convertida
     pra cinza (`fixedGray`, pré-computável uma vez por `LevelData`, igual
     `fixedNorm` hoje).
   - Aplica a mesma `warpAffine` já usada em `ComputeGradientAgreementScore`
     pra obter a CT deformada no espaço da foto.
   - Constrói o histograma conjunto (sugestão: começar com 64 bins por eixo,
     não 256 — o paper 1 usa 256 pra cérebro em alta resolução; imagens VHP
     recortadas têm menos variação tonal útil e menos pixels por fatia,
     então menos bins reduz ruído estatístico e custo).
   - Restringe o cálculo aos pixels onde `fixedMask` (ou a interseção
     `movingMask ∩ fixedMask` já deformada) é não-zero — mesma lógica de
     `ComputeMaskOverlapScore`, evita que o fundo (preto na CT, azul na foto)
     domine o histograma.
   - Retorna `I(F,R)`, idealmente normalizado pra ficar comparável em escala
     com os scores existentes (ex.: dividir por `min(H(F), H(R))`, ou usar
     informação mútua normalizada `2*I(F,R)/(H(F)+H(R))`, que fica em [0,1]
     como os outros dois termos).

2. **Extensão de `LevelData`** (linha 250): adicionar `cv::Mat fixedGray`
   pré-computado uma vez por nível de pirâmide, do mesmo jeito que
   `fixedNorm` já é pré-computado em `BuildLevelData`.

3. **Integração em `ComputeCombinedScore`** (linha 395): três caminhos
   possíveis, a decidir na sessão de implementação:
   - (a) Terceiro termo ponderado: `0.5*IoU + 0.2*gradiente + 0.3*MI`
     (pesos a recalibrar empiricamente).
   - (b) Substituir o termo de gradiente por MI inteiramente.
   - (c) Tornar o score selecionável de fato — hoje `score_method` é só um
     campo de metadado na sessão (`SessionModel`/`session_autosave.json`),
     não altera comportamento. Precisaria de um enum/branch real lido de
     `project.score_method` e propagado até `ComputeCombinedScore`.

   Recomendação: começar por (a) com MI barato (poucos bins, só no nível
   fino da pirâmide) para não estourar o orçamento de tempo da busca em
   grade, que já avalia 80–242 candidatos por iteração.

4. **Custo computacional**: MI por candidato é mais caro que IoU+Sobel
   (requer preencher um histograma 2D por warp, não só `countNonZero`/Sobel).
   Mitigação sugerida pelo próprio paper 1: **subamostrar** a imagem
   flutuante ao construir o histograma (eles subamostraram até fator 48 sem
   perda de robustez) — nas fatias VHP, um fator de 4–8 já deve bastar dado
   o tamanho das imagens. Alternativa: aplicar MI só no nível mais fino
   (`levels[2]`, resolução total) e manter IoU+gradiente nos níveis grosseiros
   da pirâmide, que já fazem o trabalho pesado de aproximação inicial.

5. **Nenhuma mudança necessária** em: `TransformInterpolator` (interpolação
   entre âncoras continua igual, é geométrica, não depende do score),
   `ConvergenceAnalyzer` (estatísticas sigma continuam sobre os parâmetros
   de transformação, não sobre o score), `LandmarkRegistration` (âncoras
   manuais não usam score automático).

## Decisões em aberto para a próxima sessão

- Número de bins do histograma (começar em 64, testar 32/128).
- PV simples vs. GPVE de 2ª ordem — decidir depois de ver se PV sozinho já
  remove os mínimos falsos na prática desses dados.
- Peso de MI no score combinado, ou se vira o score exclusivo do
  `registration_preset` atual (`ct_photo_initial`) versus um preset novo
  (`ct_photo_mi`) selecionável na sessão.
- Se vale a pena versionar isso como `algorithm_version = "mi_v1"` (o campo
  já existe em `RegistrationResult`/`registrations[].algorithm_version` no
  JSON de sessão) para permitir comparar resultados MI vs. score atual nas
  mesmas 265 fatias da cabeça, aproveitando o histórico de operações já
  registrado em `operations[]`.

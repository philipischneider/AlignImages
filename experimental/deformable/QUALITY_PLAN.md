# Plano de qualidade global e local — 23/10/2026

Principio: melhoria global nao deve compensar livremente regressao local relevante.
Comparar sempre global sem deformacao, malha geral e incremento adaptativo.

## 1. Instrumentacao (primeira implementacao)

- Dice, ASSD, HD95 e Surface Dice para corpo/osso no corte e em janelas
  sobrepostas, regioes manuais e futuramente estruturas anatomicas.
- Calcular contornos antes de recortar janelas; excluir fronteiras artificiais
  de suporte anotado. Regioes vazias/sem suporte sao evidencia insuficiente.
- Distancias em pixels da foto; mm somente com calibracao XY independente.
- Persistir parametros, suporte, comparacoes e mapa melhoria/estabilidade/piora/
  sem evidencia. Tolerancias diagnosticas explicitas, ainda sem mudar gates.
- Instrumentar resultados novos e permitir avaliar existentes sem sobrescreve-los.
- Incluir deslocamento/Jacobiano locais e comparacoes entre etapas.

## 2. Protecao contra regressoes

- Calibrar tolerancias em casos reais identificados pelo usuario.
- Gates globais e locais na malha geral e no adaptativo; preservar referencias
  protegidas. Reduzir/reotimizar incrementos com transicao suave e revalidar campo.
- Nao apagar partes do campo abruptamente; verificar Jacobiano apos composicao.

## 3. Refinamento orientado por erro

- Selecionar regioes com erro e evidencia confiavel, proteger areas ja boas.
- Ciclo avaliar/localizar/propor/reavaliar/aceitar-reduzir-rejeitar, cancelavel.
- Aumentar nos apenas quando demonstrar ganho. Manter selecao manual.

## 4. Sequencia

- Incorporar continuidade fisica aos gates finais, alem da perda de otimizacao.
- Reavaliar vizinhos compativeis apos alteracoes; nao impor continuidade em
  lacunas ou geometrias incompativeis. Sem deslocamento Z nesta versao.

## Validacao e entrega

- Casos sinteticos: identidade, translacao conhecida, regressao local apesar de
  ganho global, regioes vazias, suporte parcial e borda de janela.
- Piloto com regressoes observadas e 234/235/236/237/240/250/260/264.
- Mascaras do objetivo nao sao validacao independente: reservar landmarks e
  revisao anatomica. Nao considerar Dice ou build prova de precisao anatomica.
- Dados originais preservados, novas saidas e formatos opcionais compativeis.
- Entregar instrumentacao primeiro; gates e selecao automatica nas etapas seguintes.

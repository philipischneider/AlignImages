# Implementation Plan: Operation-Centric Refinement Workflow

## Goal

Evolve the application from a single-state alignment tool into an operation-centric workflow where the user can:

- mix automatic alignment, manual landmark refinement, and guided re-alignment
- review the history of executed operations in a lateral stack
- compare the visual effect of different operations in the preview
- use manual landmark-derived transforms as priors for later automatic passes
- restrict automatic refinement using local statistical bounds

## Core Product Direction

The UI should not assume a fixed number of steps or rounds.

Instead, the system should record **operations**:

- batch auto alignment across the stack
- targeted auto alignment on selected slices
- manual landmark adjustment on one pair
- convergence prior analysis
- sigma-restricted automatic refinement

Each operation may affect:

- the whole stack
- a subset of slices
- a single pair

The current state of a pair is the latest operation that touched it.

## Data Model Changes

### 1. Introduce operation records

Add a project-level structure such as `AlignmentOperation` with:

- stable id
- timestamp
- label
- operation kind
- scope kind (`global`, `selection`, `single_pair`)
- affected pairs
- source operation ids
- method parameters
- summary metrics

### 2. Pair-level version tracking

Extend `RegistrationResult` so that pair history can reference operation provenance:

- operation id that produced the current state
- per-operation snapshots
- metrics before/after the operation

### 3. Statistical prior metadata

Extend convergence analysis outputs to store:

- local mean
- local median
- local standard deviation
- lower/upper bounds used for guided refinement

for:

- `tx`
- `ty`
- `theta`
- `scale`
- `sx`
- `sy`

### 4. Session persistence

Persist:

- operation history
- selected operation for preview
- sigma-restriction settings
- pair-to-operation provenance

## Pipeline Changes

### Phase A. Statistical prior generation

Update `ConvergenceAnalyzer` to compute local statistics using neighboring slices.

Outputs per pair:

- priors from neighboring successful results
- optional preference for manual landmark-derived transforms
- standard deviation envelopes

### Phase B. Sigma-restricted refinement

Update `RegistrationEngine` to support refinement from priors with bounded search:

- start from prior
- clamp candidate exploration to `prior +/- k * sigma`
- expose `k` as configurable UI parameter

### Phase C. Manual-guided automatic passes

Use manual landmark results as:

- direct priors for the corrected slices
- anchors that bias neighboring slices during convergence analysis

## UI Redesign Plan

### 1. Keep the 3-viewer center layout

Preserve:

- Stack A viewer
- Stack B viewer
- Preview viewer

### 2. Add operation history sidebar

A dedicated lateral panel should show a Photoshop-like action stack.

Each item should display:

- operation label
- timestamp
- kind
- scope
- number of affected slices
- mean score delta
- outlier delta

Actions per item:

- select for preview
- compare against latest
- rename
- delete

### 3. Add operation-aware preview

The preview should allow:

- latest state
- selected operation state
- before/after comparison against another operation

### 4. Add operation scope clarity

Every operation entry should clearly state:

- `GLOBAL`
- `PARTIAL`
- `SLICE`

### 5. Improve timeline feedback

Show markers for slices affected by:

- manual landmarks
- sigma-restricted refinement
- outlier status

### 6. Improve help text

Add delayed tooltips to:

- statistics
- operations
- refinement controls
- sigma controls
- convergence indicators

## Implementation Sequence

### Step 1. Infrastructure

- add operation data structures
- persist them in session files
- link registrations to operation ids

### Step 2. Operation sidebar

- build the lateral operation stack UI
- support selection, deletion, and preview of a chosen operation

### Step 3. Statistical priors

- compute per-parameter standard deviation
- store sigma bounds in pair results

### Step 4. Guided refinement engine

- implement bounded refinement around priors
- support manual-anchor-biased priors

### Step 5. Timeline and help polish

- add markers and richer tooltips
- show operation provenance in the inspectors

## Immediate First Implementation Target

Start with the operation-centric data model and sidebar foundation:

1. introduce `AlignmentOperation`
2. persist operations in the session
3. show the operation list in the UI
4. bind preview selection to the chosen operation

This creates the base needed for later sigma-restricted refinement without reworking the UI twice.

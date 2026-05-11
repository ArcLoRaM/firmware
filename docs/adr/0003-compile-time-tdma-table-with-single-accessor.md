# ADR-0003: TDMA Table as Compile-Time Constant Accessed Through a Single Interface

## Status
Accepted

## Context
The TDMA pattern is expected to evolve during development. Three options were
considered:

- **Option A**: `const` C array baked into firmware. Changing the schedule
  requires a firmware update.
- **Option B**: Flash-resident, written during provisioning or OTA. Changing the
  schedule does not require recompilation.
- **Option C**: Negotiated at runtime via Mesh_Beacon. Maximum flexibility but
  significant protocol complexity and a versioning problem.

The current need is development-time flexibility — tweaking patterns during
implementation without complex distribution infrastructure.

## Decision
Option A. The TDMA Table is a `const` array in firmware. All access goes through
a single accessor function — no call site holds a direct reference to the array.

## Consequences
Changing the schedule requires a firmware update. The single-accessor boundary
is the only seam needed to migrate to Option B later: swap the backing store
behind the accessor without touching any caller. Do not scatter direct array
references across the codebase — this would foreclose the migration path.

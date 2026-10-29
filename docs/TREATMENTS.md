# Treatments: Age, Deform, Twist, Puncture

Treatments change *what the object is*, so they run in the compile step and reshape the modes.
They are automatable (crossfaded recompiles), and they apply to any part type through the
`ModalBasis` interface.

## Method

Each treatment produces local changes over the part's surface:

- `fIso`: fractional change of local stiffness (tension for membranes)
- `S`: an anisotropic in-plane stress tensor (twist, wrinkles)
- `fMass`: fractional change of local mass
- `loss`: a local loss factor (rubbing cracks, torn edges, slack spots)

These are integrated against the untreated mode shapes and gradients to give modal matrices
δK, δM and D. The treated modes solve `(diag(ω²) + δK) x = λ (I + δM) x`. Frequencies are √λ,
the new shapes are combinations of the old, and each mode's decay is the material decay at its
new frequency plus the strain-energy share of the local losses (`xᵀ D x / 2ω`).

What you hear: degenerate pairs (the cos/sin twins of each m > 0 mode) split into beating
pairs, partials move by different amounts, the overtone pattern gets less regular, and some
modes die faster than others.

## Controls

| Treatment | Controls | What it does physically |
|---|---|---|
| **Age** | Amount, Climate (dry ↔ damp) | Per-material rules: global stiffness/mass/loss drift, a smooth random unevenness field (uneven tension/thickness), pits (material lost), cracks (stiffness lost + friction), and grit. |
| **Deform** | Intensity, Size, Density | Gaussian dents (75 %) and bumps (25 %). On a tensioned head a dent is a stretched slack spot: local tension drops and it adds loss. On solid parts (bars, plates, shells, tubes, springs, box faces) a dent stiffens instead (shallow-shell effect). |
| **Twist** | Amount (±), Pivot, Focus, Wrinkle | Torsional shear stress about a pivot (τ ∝ r₀²/(ρ² + r₀²)), plus second-order stretching ∝ twist², which tightens the part where the twist is strongest. Wrinkle adds fine ridges of uneven stress with extra loss and buzz. |
| **Puncture** | Holes, Size, Place, Scatter, Ragged | Removes mass and stiffness inside each hole; a torn rim adds loss and rattle. Holes sit on a ring (Place = centre → edge, following the part's outline) and Scatter blends them toward random positions. |

## Age rules (at Age = 1, dry → damp)

| Material | Process | Main effects |
|---|---|---|
| Steel | patina → rust | rust layer adds mass and high-frequency loss; deep pits; lots of grit when damp |
| Bronze / Brass / Aluminium | patina / tarnish / oxide | mild loss and unevenness, some pitting |
| Glass / Ceramic | crazing | cracks: split modes, friction loss, ticking grit |
| Slate | weathering | flaking: lighter, lossier, gritty |
| Ice | sublimation / melt | thinner and softer (pitch drops), wet loss when damp |
| Oak / Spruce / Bamboo | drying & checking ↔ swelling & rot | dry: lighter, stiffer, brighter, cracked; damp: heavier, softer, dull |
| Rubber | perishing | hardens (pitch up), cracks |
| Hide | drying ↔ going slack | dry: tighter and brittle; damp: slack, dead, uneven |
| Mylar | stretch (cold flow) | loses tension (pitch drops ~20 %), uneven, deader |
| Paper | brittling ↔ sogginess | dry: stiff and crackly; damp: soft and dead |
| Hyperglass | crystallising | shimmer from tiny splits |

All numbers live in `source/model/Materials.cpp` and are meant to be tuned by ear.

## Limits

- First-order physics in a truncated basis: large holes are approximate (a real hole changes the
  boundary conditions; here it removes mass/stiffness within the solved modes).
- The statistical high band gets the treatments' average effect, not per-mode detail.
- Grit is an output-side crackle until M3's contact joints exist.

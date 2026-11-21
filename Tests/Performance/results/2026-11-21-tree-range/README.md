# Arbres : densité / 5, portée × 5

Comparaison à `d165f928bff4cd0c55cfa7a02790327460e9b755`, scène livrée à
40 096 m, 64³ cellules, huit travaux en vol, Mesa llvmpipe. Les fichiers JSON
contiennent les paramètres, empreintes de compilation/shaders/catalogue et
résultats bruts. `generation-after.json` provient du code modifié non encore
commité au moment de la mesure ; ses empreintes identifient ces sources.

## Résultats

| Mesure | Avant | Après |
|---|---:|---:|
| Probabilité locale de placement | 100 % | 20 % |
| Portée de rendu par défaut | 2 048 m | 10 240 m |
| LOD autorisés à collecter | 0–2 | 0–6 |
| Chunks générés | 2 665 | 2 665 |
| Sommets du terrain | 27 228 894 | 27 228 894 |
| Temps du banc de génération | 10.190 s | 10.982 s |
| Positions capturées en cache | 312 | 108316 |
| Positions publiées avant culling du renderer | 312 | 9783 |
| Réserve GPU permanente | 51 Mio | 51 Mio |
| Buffers temporaires, huit travaux | 29 248 octets | 6 558 272 octets |

La génération mesure ici **+7.8 %**, avec une population lointaine désormais
réellement capturée. Une seule paire de mesures ne caractérise pas la variance
ni les performances de la GTX 1660. Il ne s’agit pas d’une mesure du jeu complet
ou de ses FPS. L’initialisation/compilation est chronométrée séparément ; aucune
autre suite GPU ne tournait pendant ces mesures.

Le banc initialise maintenant la portée de publication depuis la caméra,
comme `ProceduralGeneration`. Les positions restent en cache sur l’ensemble
du terrain résident pour éviter un trou au retour vers un chunk non régénéré.
Seuls les secteurs proches sont publiés ; les 9 783 positions comprennent la
marge conservative des secteurs. La distance exacte, le frustum et le LOD sont
ensuite filtrés par le renderer. Ce nombre n’est pas le nombre d’arbres dessinés.

Sur une région plate de 16,384 × 16,384 km, les huit principaux profils boisés
conservent en moyenne 20,04 % de leurs sites précédents. Les masques de bosquets,
clairières, familles, échelles et espacement minimal sont inchangés. Tous les
profils non nuls ont exactement leur probabilité divisée par cinq.

## Validation

- Placement GPU : compilation normale et fast-math ; terrain identique,
  frontières, annulations, pools, couverture, première capture LOD6,
  subdivision et captures fines/grossières concurrentes sans doublons dans
  les fixtures ; réapparition des mêmes positions après un aller-retour.
- Compactage : 30 cas jusqu’à 65 536 sites, 254 785 positions compactées
  identiques à l’oracle par type, sentinelles mémoire intactes.
- Renderer : LOD, normales/vent et ombres, bornes de pages fines dans un grand
  secteur, réutilisation des caches et des couches d’ombre non animées.
- Compilation syntaxique avec les interfaces moteur réelles.

La hauteur demeure celle de la première capture. Une capture sur les voxels
LOD6 de 64 m peut laisser des pieds approximatifs lorsque le terrain s’affine.
La quantification UNORM16 dans un secteur de 4 096 m ajoute au maximum environ
3,13 cm par axe ; elle n’est pas la source principale de cette approximation.
Aucune passe supplémentaire de génération ou reprojection n’est ajoutée.

## Reproduire

Créer un worktree propre du commit de référence, puis lancer depuis le code
courant pour employer le même banc :

```bash
python Tests/Performance/run.py --engine-root /chemin/vers/reference \
  --scenario generation --objects --max-requests 0 --timeout-seconds 180 \
  --output /tmp/tree-range-before.json
python Tests/Performance/run.py --scenario generation --objects \
  --max-requests 0 --timeout-seconds 180 --output /tmp/tree-range-after.json
python Tests/Trees/check_placement.py
python Tests/Trees/check_placement.py --fast-math
python Tests/Trees/check_runtime.py
python Tests/Trees/check_runtime.py --fast-math
python Tests/Trees/check_compaction.py --output /tmp/tree-range-compaction.json
python Tests/Atmosphere/check_render_integration.py
```

Le test de distribution et ses options de comparaison sont détaillés dans
[`docs/object-placement.md`](../../../../docs/object-placement.md).

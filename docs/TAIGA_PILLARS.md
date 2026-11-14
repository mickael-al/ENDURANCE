# Taïga : colonnes cylindriques inclinées

Le calcul de hauteur utilisateur est conservé dans `Taiga/height.glsl`, fonction
`taigaBaseRelief`. Cela inclut les collines, les montagnes et son abaissement
`h -= 100` lorsque le masque dépasse 0,6. La recette 2D n'ajoute aucune hauteur :
elle élargit seulement la bande nécessaire aux nouvelles formes 3D. La version
de référence est le commit utilisateur `6960a33ecd8ca16a4478e2c818e3ea7ce76fd6c8`,
dont le volume Taiga retournait zéro.

## Dimensions et placement

| Réglage | Valeur | Emplacement |
| --- | --- | --- |
| Diamètre du cylindre | 200–400 m | `pillar_sites.glsl`, rayons `mix(100.0, 200.0, ...)` |
| Hauteur verticale de l'enveloppe | 500–1000 m | `pillar_sites.glsl`, `mix(500.0, 1000.0, ...)` |
| Inclinaison | 0–45° | `pillar_sites.glsl`, `0.7853981634` radians |
| Azimut | 0–360° | `pillar_sites.glsl`, `6.2831853072` radians |
| Espacement demandé | 1000–1500 m | Distance horizontale au pied voisin le plus proche |
| Vide intérieur | 30 % | `pillar_porosity.glsl`, huit seuils calibrés |
| Échelle Perlin horizontale | 75–150 m | Rayon divisé par `4.0 / 3.0` |
| Échelle Perlin axiale | Six cellules par colonne | Coordonnée axiale normalisée multipliée par `6.0` |

Les centres sont issus d'un échantillonnage blue noise périodique, avec exclusion
des voisins trop proches et correction des points isolés. Après quantification,
les distances au voisin le plus proche sont **1034,87–1448,52 m**, y compris aux
bords de la tuile. Les cases de 1600 m servent uniquement à trouver les points :
les colonnes ne sont pas alignées sur leurs centres. Il ne s'agit pas d'une
contrainte sur toutes les paires ni d'une distance entre les bords des cylindres.

La table comporte 1309 pieds dans une tuile de **48 × 48 km**, répétée après une
rotation et une translation déterminées par la seed. Les positions se répètent
donc tous les 48 km dans ce repère ; les dimensions, angles et pores utilisent
aussi l'identifiant de la tuile. La table occupe environ 8,8 Ko. Une recherche de
neuf cases examine au plus 27 candidats, puis élimine ceux hors de l'empreinte.
`Tests/Terrain/generate_taiga_pillar_sites.py` rend la table reproductible.

## Géométrie et creusement

`pillars.glsl` construit un repère orthonormé : c'est une vraie rotation du
cylindre, conservant sa section circulaire. Le pied utilise la hauteur Taiga au
centre du site, macrorelief compris. Le cylindre reste droit sur une pente ; sa
base peut s'enfoncer dans le sol. La longueur axiale vaut
`(height - radius * sin(tilt)) / cos(tilt)` pour que le bord supérieur de
l'enveloppe atteigne la hauteur verticale demandée. Celle-ci ne désigne pas la
longueur le long de l'axe.

La densité est l'intersection entre le cylindre fini et un Perlin 3D : aucun
bruit ne pousse de matière hors de l'enveloppe. Huit champs Perlin ont leur
70e percentile calibré sur le volume du cylindre. Une rotation autour de l'axe
et une phase périodique font varier les pores sans changer cette proportion.
Les dimensions et l'inclinaison préservent également cette fraction volumique.
Changer la fréquence, le bruit ou les seeds demande une nouvelle calibration
avec `Tests/Terrain/calibrate_taiga_pillar_porosity.py`.

Les **30 % portent sur le cylindre isolé**, avant fusion. L'union avec le terrain
et d'éventuelles autres colonnes remplit une partie des vides ; elle ne retire
jamais de sol existant. Les pores peuvent aussi retirer le point le plus haut
de l'enveloppe. Aux transitions climatiques, les poids du pipeline atténuent
normalement la contribution du biome. L'ancrage reste celui du relief Taiga pur
au pied : il peut différer du sol mélangé à la frontière d'un autre biome.
Les bornes de dimensions et les mesures d'ancrage concernent donc Taiga isolé.

## Bande, coût et activation

La bande 3D suit l'empreinte projetée et couvre la colonne entière, même si elle
penche au-dessus d'un terrain plus bas. Le plafond global passe de 8192 à 16384
unités brutes, avec sa contrepartie CPU, pour éviter des colonnes tronquées.
Le relief maximal et le stockage 32 octets ne changent pas ; la précision des
bandes historiques ≤576 est inchangée, celle des bandes larges est maintenant
16384/65535 unité brute. La hauteur reste en FP32.

Le rejet d'empreinte précède le calcul de la hauteur au pied et le Perlin.
L'ancrage exact recalcule le relief Taiga pour les candidats proches dans le
shader volumique ; ce coût reste à mesurer sur le GPU de jeu. La table évite une
recherche de voisins non bornée. Les dimensions sont en mètres monde pour une
échelle de détail positive ; le mode sans volumes reste respecté. Une échelle
de détail extrêmement petite peut finir par atteindre le plafond global de bande.

**Recompiler le moteur puis régénérer le terrain**, car la borne CPU a changé.

## Validation

Le rapport [JSON](biome-validation/taiga_pillars.json) et la
[figure](biome-validation/taiga_pillars.png) sont calculés à partir des shaders.
La figure montre la répartition et des volumes isolés, pas une capture du jeu.
Les tests comparent le `h` et les contrôles au snapshot utilisateur, mesurent le
vide dans les cylindres, vérifient les bornes géométriques et l'absence de retrait
du sol, puis contrôlent la bande sur le vrai relief et les valeurs du pipeline.

- `h` identique bit à bit sur 111 747 positions ; 225 816 densités vérifiées sans
  retrait de terrain existant.
- Vide mesuré entre 29,9576 % et 30,0293 % sur 20 cylindres, chacun échantillonné
  en 131 072 points uniformes dans son volume.
- 47 104 points mondiaux par échelle de détail 0,2 / 0,35 / 1 : aucune pierre
  tronquée ; bande monde maximale respective 2154,83 / 2359,65 / 3341,55 m.
- 1331 densités concordantes avec l'accumulation réelle et 125 points communs
  entre LOD identiques. La montagne Taiga antérieure conserve son pic de 700 m
  et ses dimensions dans les tests froids historiques.

Les 26 biomes passent la suite générale. Les tests CPU, bandes, stockage et
dispatch passent avec la nouvelle limite, dont des échantillons modifiés à plus
de 2 km du centre de bande. Ces validations sont exécutées sous Mesa llvmpipe ;
elles ne mesurent pas les performances Windows, les collisions ni la jouabilité.

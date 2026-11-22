# SSAO : correction, filtrage et coût

Le moteur conserve le SSAO à hémisphère orienté issu de LearnOpenGL :
**64 échantillons, résolution complète, rayon de 0,5 m et biais de 0,025 m**.
Le calcul utilise la profondeur linéaire RG32F du G-buffer ; aucune réduction
de sa précision ni résolution intermédiaire à demi-taille n'est introduite.

## Activer ou désactiver le SSAO

Le moteur active le SSAO par défaut (`SettingInfo::m_ssaoEnabled = true`).
ENDURANCE le désactive dans `ANTAGONIZ/Source/Main.cpp`, avant l'initialisation :

```cpp
e.settingManager->setSsaoEnabled(false);
```

Le même setter peut être appelé pendant le jeu, depuis le thread du moteur.
Passer `true` réactive l'effet à la prochaine image ; `getSsaoEnabled()` permet
de consulter le choix. L'entrée dans une grotte n'est pas détectée automatiquement :
le jeu pourra appeler ce setter à partir de ses propres zones ou réglages.

Le gestionnaire prépare deux programmes d'éclairage au chargement. Dans
`Asset/Shader/forward.fs.glsl`, `SSAO_ACTIVE` vaut `1` par défaut et la variante
désactivée reçoit `#define SSAO_ACTIVE 0`. Les gardes `#if SSAO_ACTIVE` retirent
la fonction et sa boucle de 64 échantillons, la transformation de normale pour
l'AO, le SSBO du kernel et le sampler du bruit. Il n'y a aucun branchement
d'activation par fragment, ni compilation au moment de changer le réglage.
Le rendu ignore également les bindings SSAO quand il est désactivé. Le même
triangle plein écran sert aux deux variantes ; ses attributs ont des emplacements
explicites, partagés avec les passes de post-traitement.

Les minuscules ressources statiques restent préparées pour éviter une allocation
à la réactivation : 1 024 octets de kernel et 128 octets de texels de bruit,
hors surcoût des objets du pilote. Aucune texture AO plein écran n'est allouée.
La variante désactivée conserve l'occlusion des matériaux (`other.z`), les ombres,
l'éclairage direct, l'IBL et la composition atmosphérique.

`Tests/SSAO/check_gpu.py` compile les vrais gestionnaires C++ et les programmes
GLSL, vérifie l'absence des interfaces SSAO dans le programme désactivé et alterne
les deux programmes avec le même VAO. Il compare le résultat à une référence
d'éclairage où l'AO écran vaut exactement 1, avec et sans atmosphère, puis vérifie
que le contact entre surfaces revient à la réactivation. Le gain de temps de
frame dépend de la vue : les raccourcis sur le ciel et les surfaces lointaines
étaient déjà actifs dans la version avec SSAO.

## Défauts de l'ancienne version

L'upload de `64 * sizeof(glm::vec3)` ne correspondait pas au pas de 16 octets
du tableau GLSL `std430 vec3[]`. Avec le GLM du projet, le buffer ne contenait
que 768 octets : les composantes étaient désalignées et les 16 derniers
échantillons sortaient du buffer. Le nouveau kernel est explicitement composé
de 64 `vec4`, soit 1 024 octets.

Chaque obstruction était comptée deux fois, d'abord sans pondération puis avec
le poids de distance. Le résultat pouvait devenir négatif. De plus, la position
était en espace vue et la normale en espace monde : tourner la caméra modifiait
l'orientation de l'hémisphère par rapport à la surface.

La rotation aléatoire répétée sur 4 × 4 pixels était ajoutée directement à
l'image sans débruitage. Les lectures hors écran pouvaient aussi revenir du
bord opposé à cause du wrapping du G-buffer.

Enfin, l'occlusion écran et l'occlusion matériau atténuaient la lumière directe
`Lo`. Elles doivent atténuer l'éclairage indirect ; le terme direct dispose
déjà des cartes d'ombres. La nouvelle composition est `ambient * screenAO + Lo`,
où `ambient` contient déjà l'occlusion matériau. Le ciel, l'émission et la
composition atmosphérique gardent leurs traitements séparés.

## Chemin de rendu retenu

Le calcul reste dans le shader d'éclairage différé, via
`Asset/Shader/SSAO/evaluate.glsl`. Aucun dispatch, framebuffer ni grande texture
AO supplémentaire n'est ajouté. Le kernel float32 fait 1 024 octets et la
rotation 4 × 4 utilise deux composantes RG32F au lieu de RGBA32F.

Une seule accumulation pondérée est normalisée et bornée dans `[0,1]`. Les
projections derrière la caméra, hors écran, le ciel et les lectures retombant
sur le pixel courant sont ignorés. La normale du matériau est
transformée de l’espace monde vers l’espace vue, en conservant le lissage et les
normal maps ; les normales de triangles restent réservées aux biais d’ombre.
La base tangente reste valide lorsque le bruit est parallèle à la normale.

L'atténuation de distance devient nulle au rayon demandé, avec une transition
progressive : l'ancienne expression `smoothstep(0,1,radius/separation)` pouvait
encore assombrir une autre couche à plusieurs mètres malgré un rayon de 0,5 m.
Le ciel et les surfaces émissives ne consomment pas d'AO ; une surface émissive
peut toutefois occulter l'éclairage ambiant d'un voisin.

La projection n'évalue que les composantes XY/W nécessaires aux caméras du
moteur. Une borne conservative de la projection de toute la sphère de rayon
0,5 m évite les 64 échantillons lorsqu'ils retomberaient tous sur le pixel
courant. Ce n'est pas un seuil arbitraire en mètres : la validation compare
avec la même version sans cette optimisation.

## Débruitage évalué puis écarté

Une version séparée, calcul AO puis filtre spatial 5 × 5 tenant compte de la
profondeur et des normales, a été évaluée. Une variante partageait un voisinage
12 × 12 entre groupes de 8 × 8 pixels, sans changer la formule du filtre.
Le partage réduisait son coût mais le chemin complet restait nettement plus
lent dans les scènes proches et mixtes sur le backend testé. Un filtre fixe en
pixels peut aussi atténuer un contact fin si sa portée dépasse celle de l'AO.

Cette version n'est **pas** le chemin de production : elle est conservée dans
`Tests/SSAO/filtered_candidate` comme candidat de comparaison. Le passage
à deux textures R16F et aux deux dispatchs n'est donc pas imposé au jeu.

La correction du kernel, des espaces et du comptage retire les causes de bruit
anormal sur les surfaces qui devraient rester éclairées. Elle ne transforme
pas le SSAO stochastique en rendu débruité : une variance résiduelle peut rester
près des contacts. Réduire ce grain davantage demanderait une comparaison de
qualité/coût sur la carte cible, conformément à la contrainte du projet.

Le benchmark doit inclure le chemin complet et les vues proches, lointaines,
mixtes et le ciel seul. Les résultats Mesa/llvmpipe ne prédisent pas les FPS sur
GPU matériel. Les témoins anciens contiennent des défauts et ne constituent
pas une référence de qualité égale ; ils servent aussi à rendre visible le
coût des corrections, sans masquer les éventuelles régressions.

## Pourquoi conserver cet algorithme

La règle du projet est de remplacer le SSAO seulement si un candidat améliore
à la fois la qualité et la vitesse dans ENDURANCE. Les publications disponibles
ne suffisent pas à démontrer cette double condition dans ce moteur.

| Candidat | Intérêt | Limite de la comparaison |
| --- | --- | --- |
| XeGTAO | Intégration de l'occlusion plus physique, profondeur mipmappée, filtre spatial | Les chiffres favorables publiés utilisent un autre pipeline ; la comparaison de qualité bénéficie notamment du TAA. |
| HBAO/HBAO+ | Recherche d'horizons, organisation des lectures favorable au cache | Un gain contre HBAO classique ne prouve pas un gain contre ce SSAO ; portage et mesure nécessaires. |
| CACAO | Calcul adaptatif, filtrage conservant les contours | Plusieurs niveaux de qualité impliquent des compromis ; intégration officielle DX12/Vulkan. |

Sources primaires :

- [LearnOpenGL — SSAO](https://learnopengl.com/Advanced-Lighting/SSAO) : espaces de coordonnées, rotation du kernel, filtrage et composition ambiante.
- [Intel — XeGTAO](https://github.com/GameTechDev/XeGTAO) : passes, dépendance du rendu temporel et mesures contextualisées.
- [NVIDIA — exemple HBAO OpenGL](https://github.com/nvpro-samples/gl_ssao) : désentrelacement, filtre bilatéral et comparaison entre ses deux versions.
- [AMD — CACAO](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/combined-adaptive-compute-ambient-occlusion/) : organisation compute et compromis de qualité.

## Mesures et validation reproductibles

Commandes à lancer depuis la racine du dépôt :

```sh
python3 Tests/SSAO/check_gpu.py
python3 Tests/Atmosphere/run_all.py
python3 Tests/SSAO/benchmark_ssao.py --sizes 128 256 --samples 13 --warmup 4
# Candidat écarté, pour comparaison uniquement :
python3 Tests/SSAO/benchmark_ssao.py --sizes 256 --filtered-candidate
```

Le test SSAO exécute le vrai `SSAOBuffer.cpp` et relit les ressources OpenGL.
Il vérifie les 64 échantillons, les plans vus sous différents angles, un contact
entre deux plans, les ruptures de profondeur, les sorties d'écran et les
réinitialisations. La composition de l'éclairage est testée dans le vrai shader
différé. La suite générale couvre aussi l'atmosphère, les ombres et les formats
du G-buffer. Résultat du 19 novembre 2026 : **26 groupes de validation réussis**, dont la
compilation de 25 programmes GLSL de production et les contrôles SSAO ajoutés.
Ces tests EGL/Linux ne remplacent pas une exécution du jeu Windows.

Mesure indicative du 19 novembre 2026, Mesa 25.0.7 / llvmpipe LLVM 19.1.7,
256 × 256 pixels, médianes en millisecondes. Le banc compare le travail AO dans
deux passes fragment identiques, pas le temps complet d'une image du jeu.
L'ancien calcul reçoit lui aussi 64 échantillons correctement alignés pour ne
pas fausser la comparaison par ses lectures hors buffer.

| Scène synthétique | Ancien calcul | Calcul corrigé | Candidat avec filtre |
| --- | ---: | ---: | ---: |
| Géométrie proche | 3,682 | 5,930 | 13,977 |
| Géométrie à 10 km | 3,138 | 0,168 | 1,630 |
| Ciel seul | 0,125 | 0,133 | 1,780 |
| Mélange proche / loin / ciel | 2,889 | 3,118 | 5,003 |

**La version corrigée n'est donc pas démontrée plus rapide dans tous les cas.**
Les contrôles de validité et de projection ont un coût dans les vues proches
sur ce backend ; la suppression exacte des lectures inutiles aide beaucoup à
grande distance. Les corrections de validité sont conservées, le filtre plus
coûteux est écarté. Il reste à mesurer sur le GPU cible avant toute affirmation
de gain global, et à examiner le grain résiduel aux contacts en jeu.

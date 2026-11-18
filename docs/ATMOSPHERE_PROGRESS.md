# Ciel et atmosphère — état de reprise

## Correction des ombres de surface validée

Après le retour sur `3b57f79`, le vrai terrain triangulé reproduit le
quadrillage : environ 53 % de faux pixels sur les bosses avec le filtre
par plan récepteur, aussi bien pour les marges de 0, 4 et 16 km.

Cette étape remplace ce filtre par quatre comparaisons PCF interpolées.
La normale géométrique est calculée sur la primitive et transmise dans le
canal gPosition.w existant, sans texture ni stockage supplémentaire. Les
biais par lumière (profondeur 0,5 ; normale 1,5 texel) sont exposés dans
ImGui et sauvegardés. Les matériaux transparents gardent leurs dérivées
sur la primitive ; les anciens producteurs G-buffer ont un repli compatible.

Les régressions de cette étape passent : 18 configurations de terrain,
438 732 récepteurs sans fausse ombre mesurée, vrais occultants de 2 m/4 m
et 0,5 m aux résolutions documentées, microdétail indépendant, géométrie
encodée, anciennes scènes et sérialisation. Les plans et contrôles
atmosphériques existants passent également. Le témoin ancien est conservé
pour montrer que le test détecte bien la régression.

## Étape suivante

Intégrer la marge de base de 4 km et son extension conservatrice par cascade
selon les bounds des chunks publiés, puis actualiser la documentation
complète et publier les deux étapes sur la PR #4. La couverture ne doit
pas perdre une montagne hors écran déjà chargée ; aucun readback GPU ne
doit être ajouté. La validation complète avec cette deuxième étape est
décrite dans le commit qui suit.

Branche : `feature/atmosphere-biome-sky`, base `feature/terrain-noise-library`.
Ne pas repartir de l’ancien master. Les correctifs de continuité IBL/vent
et de coordonnées écran restent intégrés. Le runtime graphique des tests
est EGL/Mesa llvmpipe ; compilation/lancement de l’éditeur Windows et
mesures Ultra sur la carte de jeu restent à effectuer.

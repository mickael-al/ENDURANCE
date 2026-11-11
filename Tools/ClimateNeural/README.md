# Neural Climate Trainer

Cet outil entraîne une petite fonction implicite :

```text
(position 2D + Fourier + Perlin) -> (humidite, temperature)
```

L'image de référence est un PNG, idéalement 2048×2048 :

- canal **R** : température ;
- canal **G** : humidité ;
- canal B : ignoré.

Le modèle est entraîné sur des fenêtres 512×512 décalées aléatoirement, mais
les coordonnées transmises au réseau restent globales dans l'image complète.
Le modèle exporté est évalué directement en GLSL, sans texture au runtime.

## Installation

Depuis la racine du dépôt :

```powershell
python -m venv .venv-climate
.venv-climate\Scripts\activate
python -m pip install -r Tools/ClimateNeural/requirements.txt
```

## Entraînement

```powershell
python Tools/ClimateNeural/train_climate.py `
  --image D:\Images\climate_reference.png
```

Les sorties par défaut sont :

```text
Asset/Shader/Noise/neural_climate_weights.glsl
Tools/ClimateNeural/output/neural_climate.pt
Tools/ClimateNeural/output/neural_climate_preview.png
Tools/ClimateNeural/output/neural_climate_preview_prediction_rg.png
```

L'aperçu contient, de gauche à droite :

1. la référence ;
2. la prédiction ;
3. l'erreur absolue amplifiée.

Le script remplace `neural_climate_weights.glsl` de manière atomique à chaque
aperçu. Dans `ClimateNoiseLab.scene`, sélectionner **Neural Climate**, puis
cliquer sur **Recharger le shader / poids**.

## Réglage conseillé

```powershell
python Tools/ClimateNeural/train_climate.py `
  --image D:\Images\climate_reference.png `
  --steps 8000 `
  --crop-size 512 `
  --grid-size 128 `
  --hidden-width 32 `
  --device auto
```

`grid-size` contrôle le nombre d'échantillons réellement évalués dans chaque
crop. Une grille 128×128 garde le coût raisonnable tout en changeant
aléatoirement de zone à chaque étape.

Pour reprendre :

```powershell
python Tools/ClimateNeural/train_climate.py `
  --image D:\Images\climate_reference.png `
  --resume Tools/ClimateNeural/output/neural_climate.pt `
  --hidden-width 32 `
  --steps 12000
```

La largeur doit rester identique à celle du checkpoint.

## Fonction de coût

La perte combine :

- la différence RG ;
- la différence des gradients horizontaux et verticaux ;
- une pénalité de variation dans les zones uniformes de la référence.

Cela évite qu'une simple moyenne floue obtienne un bon score tout en perdant
les limites et les aplats.

## Domaine infini

Une image de référence occupe une période réglable en mètres dans le Lab.
Les entrées sinus/cosinus se répètent sur cette période, mais quatre features
Perlin utilisent les coordonnées non repliées. Elles modifient donc
progressivement le résultat quand on déplace l'origine au-delà de l'image.

Pour reproduire au mieux l'image d'entraînement, conserver la même seed que
`--feature-seed` (1337 par défaut) et afficher exactement une période.
L'extrapolation hors image reste une généralisation du réseau, pas une garantie
de produire partout une nouvelle composition aussi bonne que la référence.

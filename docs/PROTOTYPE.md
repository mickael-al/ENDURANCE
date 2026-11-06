# ENDURANCE — Notes de prototype

## Intention

ENDURANCE est un jeu de traversée solitaire dans un monde étranger et démesuré. Le voyage doit demander un effort physique perceptible : prendre de l'élan, franchir le vide, tenir face au vent et continuer malgré la vulnérabilité du personnage. L'atmosphère recherchée associe tristesse, ampleur épique et mystère. La distance parcourue et l'impossibilité progressive de revenir donnent du poids à l'avancée.

Le bootstrap du dépôt correspond à la reprise du **moteur seul depuis la branche `master` de BellGrave**. Il ne constitue pas une implémentation du gameplay décrit dans cette note.

## Monde et personnage

Le protagoniste possède un design simple, mesure approximativement la taille d'un humain et reste le plus petit être de cette planète. Il vivait seul dans son foyer. Sa fragilité doit se comprendre immédiatement face aux reliefs, aux phénomènes naturels et aux créatures gigantesques.

Les paysages privilégient les grands espaces, la végétation monumentale, les formes lointaines partiellement cachées par la brume et les différences d'échelle. Les palettes restent variées : chaleur, désert ardent, lumière ouverte, puis froid et vents intenses. La nature doit conserver une logique sensible malgré son étrangeté. L'univers ne comporte ni humains, ni cadre galactique explicite, ni technologie ostensiblement futuriste. Aucun compagnon central n'accompagne le voyage.

## Déplacement et ambitions de traversée

La marche, la course avec accélération progressive et le grappin constituent le cœur envisagé du déplacement. La prise de vitesse doit rendre l'élan perceptible. Le grappin et l'accroche servent à rejoindre un appui, tenir sur un support et franchir les ruptures du terrain. Leur intérêt doit être éprouvé dans une scène simple avant de multiplier les situations.

Les gouffres, les ponts et les passages exposés donnent une conséquence visible à une erreur de déplacement. Le vent peut freiner la progression ou repousser le personnage. La caméra doit permettre de comprendre les appuis et les distances tout en faisant sentir l'immensité du décor.

Les tempêtes plus complexes, les tornades et les créatures géantes constituent des ambitions à éprouver. Certaines créatures pourraient servir de supports mobiles ou de transport contextuel. Leur présence participe à la traversée et à l'échelle du monde ; ces éléments ne sont pas tous obligatoires pour le premier prototype. La génération procédurale reste l'orientation envisagée pour produire une grande partie des environnements.

## Ouverture et raison du voyage

L'ouverture montre un quotidien calme. Le personnage quitte son foyer pour cueillir des baies situées en hauteur. Ce trajet introduit les déplacements et le grappin dans une situation compréhensible, avant que la catastrophe impose une autre urgence.

Au retour, un fragment de l'astre lunaire tombe et pulvérise son foyer ainsi que son environnement proche. Il perd son lieu de vie et ses repères. La catastrophe existe indépendamment de son passé et de ses actions.

Une stèle ancienne, présente sur le chemin, décrit des signes du désastre et désigne un lieu lointain associé à une montagne. La correspondance entre les signes gravés et l'événement vécu donne du crédit à cet avertissement. Le personnage suit cette piste parce qu'elle constitue sa seule indication crédible d'un endroit à tenter. La stèle ne garantit pas sa survie. L'objectif doit être compréhensible dès l'ouverture, tandis que la nature exacte de la destination reste mystérieuse.

La chute d'un fragment lunaire demeure la version de départ envisagée. Un supervolcan reste une variante possible pour l'origine de la catastrophe. Un hiver provoqué par les poussières ou les cendres volcaniques est une piste retenue pour conduire progressivement le voyage vers le froid et les conditions les plus éprouvantes. Ces possibilités restent à préciser ensemble.

## Fin validée

Au terme du voyage, le personnage atteint une ouverture noire et opaque dans la montagne. Il la traverse dans la continuité de son déplacement, à travers un espace non euclidien. Ce passage le transporte plusieurs siècles plus tard.

La sortie donne sur le même paysage, encore reconnaissable, mais guéri par le temps écoulé. La reconnaissance des lieux permet de comprendre ce qui s'est produit. Le voyage se conclut immédiatement sur cette révélation : **aucun retour jouable ni nouvelle phase d'exploration du paysage guéri n'est prévu**.

## Première scène à prototyper

Le premier périmètre jouable doit rester court et permettre d'évaluer :

- une course dont l'accélération est sensible ;
- un gouffre et des appuis permettant d'utiliser le grappin ;
- un danger météorologique simple, avec un vent qui freine ou repousse ;
- une caméra qui conserve la lisibilité du mouvement et le sentiment d'échelle ;
- une destination perceptible, reliant l'effort de traversée à l'intention narrative.

Cette scène doit permettre de juger les sensations de déplacement, la compréhension du franchissement et l'envie de poursuivre. Elle sert de base pour décider ensuite de la place des géants, des tornades et des traversées plus complexes.

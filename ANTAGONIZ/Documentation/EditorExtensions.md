# Extensions de l’éditeur

`Ge::Editor` ne connaît pas les fenêtres ou les scripts des jeux. Il appelle
`Ge::EditorExtensionRegistry` pour dessiner les menus, les fenêtres ouvertes et
les inspecteurs associés aux types de scripts. Chaque extension déclare sa
factory dans son propre `.cpp` ; aucun ajout dans `Editor.cpp/.hpp` n’est requis.

## Ajouter une fenêtre

```cpp
#include "EditorExtension.hpp"
#include "imgui-cmake/Header/imgui.h"

class MyToolsWindow final : public Ge::EditorWindowExtension
{
public:
    void render(const Ge::EditorExtensionContext& context, bool* open) override
    {
        if (!*open) return;
        if (ImGui::Begin("Mes outils", open))
        {
            ImGui::TextUnformatted("Outils du projet");
            if (context.hasProject)
                ImGui::TextWrapped("%s", context.projectRoot.generic_u8string().c_str());
        }
        ImGui::End();
    }
};

REGISTER_EDITOR_WINDOW(MyToolsWindow, "MyGame.Tools", "Mon jeu", "Outils")
```

Les paramètres après la classe sont l’identifiant unique, le menu et le libellé.
Un dernier booléen optionnel `false` permet une fenêtre utilisable sans projet :

```cpp
REGISTER_EDITOR_WINDOW(MyToolsWindow, "Engine.Tools", "Outils", "Utilitaire", false)
```

Les fenêtres d’un même menu sont regroupées. Le tri par menu puis identifiant
rend l’ordre indépendant de l’ordre d’initialisation des fichiers C++.
Le clic sur une entrée ouvre ou ferme sa fenêtre ; l’état `open` appartient au
registre, pas à `Editor`. Une instance est construite à sa première ouverture.

Après cette ouverture, son callback est appelé à chaque frame de l’éditeur,
même quand `*open == false` ou quand le projet change. Une fenêtre possédant des
modifications non enregistrées peut ainsi gérer sa confirmation de fermeture.
Elle peut remettre `*open` à `true` pour conserver son contenu. Une fenêtre sans
cette confirmation peut simplement retourner immédiatement comme dans l’exemple.

## Ajouter un inspecteur de script

```cpp
#include "EditorExtension.hpp"
#include "imgui-cmake/Header/imgui.h"

class MyScriptInspector final : public Ge::EditorScriptInspectorExtension
{
public:
    void render(const Ge::EditorExtensionContext&, std::string& json) override
    {
        ImGui::TextUnformatted("Parametres de MyScript");
        if (ImGui::Button("Reinitialiser"))
            json = R"({"value":0})";
    }
};

REGISTER_EDITOR_INSPECTOR(MyScriptInspector, "MyGame.MyScriptInspector", "MyScript", 320.0f)
```

Le troisième paramètre est le `pathName` du script dans la scène, généralement
le nom enregistré par la factory de `Behaviour`. La hauteur est facultative
(200 pixels par défaut). Le registre ne démarre aucun `Behaviour` pour rendre
ces contrôles. Il construit l’inspecteur à sa première utilisation.

Le JSON est passé par référence depuis `scriptObject` : ses modifications suivent
la sauvegarde habituelle des scènes. `Editor` garde le JSON brut sous
**Parametres du script (JSON)**. Les scripts sans inspecteur enregistré conservent
leur éditeur JSON habituel. La capture clavier est gérée par le moteur après le
rendu des extensions, comme pour les autres contrôles de l’éditeur.

## Contexte et enregistrement

- `projectRoot` est la racine du projet, et non le dossier actuellement exploré
  dans le navigateur d’assets. Il est vide lorsque `hasProject` vaut `false`.
- Les constructeurs des extensions ne sont pas appelés pendant l’enregistrement
  statique : les macros stockent seulement leurs factories.
- Les classes ont un constructeur public sans argument et héritent de
  `EditorWindowExtension` ou `EditorScriptInspectorExtension`.
- Les identifiants sont uniques entre fenêtres et inspecteurs. Un type de script
  ne peut avoir qu’un inspecteur ; une seconde inscription est refusée.
- Les macros ont un linkage interne et ne dépendent pas de `CLEAR_REGISTRY`.
  Les placer dans les fichiers `.cpp` évite les inscriptions répétées et les
  symboles globaux externes des factories de scripts.
- Le rendu et les inscriptions ultérieures se font sur le thread de l’éditeur.
  Les fenêtres et inspecteurs sont propres au processus de l’éditeur.

Le glob CMake détecte automatiquement les nouveaux fichiers avec CMake 3.12 ou
plus récent. Avec CMake 3.10/3.11, relancer la configuration après avoir ajouté un
fichier `.cpp`. Il n’y a aucun nom d’extension à ajouter à `Editor` ou à une liste
d’extensions centrale.

## Vérifications

`bash Tests/run-editor-extensions.sh` vérifie le registre sans renderer OpenGL :
enregistrement entre unités de compilation, création différée, collisions,
menus, cycle d’ouverture/fermeture, changement de projet et modification du JSON.

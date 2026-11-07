#ifndef __PREFAB_DEFFERED_LOADER__
#define __PREFAB_DEFFERED_LOADER__

#include "Behaviour.hpp"
#include "Debug.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"


struct PrefabLoad;
namespace Ge
{
    class Editor;
}

class PrefabDefferedLoader : public Behaviour
{
public:
    void start();
    void fixedUpdate();
    void update();
    void stop();
    void onGUI();

#ifndef CLEAR_REGISTRY
    inline std::string serialize()
    {
        return JS::serializeStruct(*this);
    }
    inline void load(std::string jsonFile)
    {
        JS::ParseContext context(jsonFile);
        context.parseTo(*this);
    }
#endif
private:
    const ptrClass* m_pc;
    Editor* m_ed;
    std::vector<PrefabLoad*> m_prefabLoad;
    std::vector<PrefabLoad*> m_finishedPrefabLoad;
public:
    bool playPrefab = true;
};

#ifndef CLEAR_REGISTRY

REGISTER(Behaviour, PrefabDefferedLoader, playPrefab);

#endif

#endif //!__PREFAB_DEFFERED_LOADER__
#ifndef __PREFAB_LOD__
#define __PREFAB_LOD__

#include "Behaviour.hpp"
#include "Debug.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "StaticMeshCollider.hpp"

struct PrefabLoad;
namespace Ge
{
    class Editor;
}

struct LODObject
{
    GObject* object = nullptr;

    int currentLOD = -1;
    int wantedLOD = -1;
    int failedLOD = -1;

    PrefabLoad* currentLoad = nullptr;
    PrefabLoad* pendingLoad = nullptr;

    bool loading = false;
    unsigned int large = 0;
    unsigned int flag = 0;
};

class PrefabLOD : public Behaviour
{
public:
    void start();
    void fixedUpdate();
    void update();
    void stop();
    void onGUI();
    int computeLOD(float dist, unsigned int large) const;
    void loadLOD(LODObject& lod, int lodLevel);
    void destroyLoad(PrefabLoad*& load);
    static void RefreshObject(GObject* object);

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
    int wantedLOD(const LODObject& lod) const;
    void refreshObject(GObject* object);

private:
    static PrefabLOD* s_instance;
    const ptrClass* m_pc;
    Editor* m_ed;
    std::vector<LODObject> m_objects;

public:
    std::vector<float> distanceLOD = {50.0f, 150.0f};
    std::string pathPrefab = "/Ressources/Model/Prefab/";
    bool playPrefab = true;
};

#ifndef CLEAR_REGISTRY

REGISTER(Behaviour, PrefabLOD, distanceLOD, pathPrefab, playPrefab);

#endif

#endif //!__PREFAB_LOD__

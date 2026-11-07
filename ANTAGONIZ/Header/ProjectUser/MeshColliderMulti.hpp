#ifndef __MESH_COLLIDER_MULTI__
#define __MESH_COLLIDER_MULTI__

#include "Behaviour.hpp"
#include "Debug.hpp"
#include "Engine.hpp"
#include "PointeurClass.hpp"
#include "RigidWraper.hpp"
#include "CollisionWraper.hpp"
#include "LoadingState.hpp"
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btTriangleIndexVertexArray.h>
#include <thread>
#include <functional>
#include <atomic>
#include <vector>
#include <cstdint>

using namespace Ge;

struct MeshCObjMulti
{
    std::vector<std::uint32_t> indices;
    std::vector<btVector3> triangleMesh;
    btTriangleIndexVertexArray* meshInterface = nullptr;
    btBvhTriangleMeshShape* bvh = nullptr;
    StaticMeshCollider* smc = nullptr;
};

class MeshColliderMulti : public Behaviour
{
public:
    void start();
    void fixedUpdate();
    void update();
    void stop();
    void onGUI();
    void loadMeshData(std::string cp, bool fbx, std::vector<GObject*> obj);

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
    const ptrClass* m_pc = nullptr;
    std::vector<FBXMeshData> m_fmds;
    std::vector<MeshCObjMulti> m_shape;
    std::atomic<bool> taskReady = false;
    std::atomic<bool> m_loadFailed = false;
    Ge::LoadingState::Ticket m_loadingTicket;
    std::thread worker;
    void finishLoading(bool failed = false) noexcept;
    void loadCapturedMeshData(const std::string& cp, bool fbx,
        const std::vector<glm::mat4>& objectWorlds) noexcept;
public:
    std::string meshStatic = "";
    std::string targetTag = "";
    bool threading = true;
};

#ifndef CLEAR_REGISTRY

REGISTER(Behaviour, MeshColliderMulti, meshStatic, targetTag, threading);

#endif

#endif //!__MESH_COLLIDER_MULTI__
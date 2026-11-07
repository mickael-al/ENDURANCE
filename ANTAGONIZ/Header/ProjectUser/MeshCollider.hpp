#ifndef __MESH_COLLIDER__
#define __MESH_COLLIDER__

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

using namespace Ge;

struct MeshCObj
{
    FBXMeshData fmd;
    std::vector<btVector3> triangleMesh;
    btTriangleIndexVertexArray* meshInterface = nullptr;
    btBvhTriangleMeshShape* bvh = nullptr;
    StaticMeshCollider* smc = nullptr;
};

struct FbxTarget
{
    glm::vec3 pos;
    JS_OBJ(pos);
};

class MeshCollider : public Behaviour
{
public:
    void start();
    void fixedUpdate();
    void update();
    void stop();
    void onGUI();
    void loadMeshData(std::string cp, bool fbx, GObject* obj);

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
    std::vector<MeshCObj> m_shape;
    std::atomic<bool> taskReady = false;
    std::atomic<bool> m_loadFailed = false;
    Ge::LoadingState::Ticket m_loadingTicket;
    std::thread worker;
    void finishLoading(bool failed = false) noexcept;
    void loadCapturedMeshData(const std::string& cp, bool fbx,
        const glm::mat4& objectWorld, const std::vector<FbxTarget>& offsets) noexcept;
public:
    std::string meshStatic = "";
    bool threading = true;
    std::vector<FbxTarget> tpos = { {glm::vec3(0,0,0) },{glm::vec3(0,0,0) } };
};

#ifndef CLEAR_REGISTRY

REGISTER(Behaviour, MeshCollider, meshStatic, threading, tpos);

#endif

#endif //!__MESH_COLLIDER__
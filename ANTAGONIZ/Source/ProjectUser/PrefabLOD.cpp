#include "PrefabLOD.hpp"
#define CLEAR_REGISTRY
#include "Camera.hpp"
#include "Editor.hpp"
#include "PathManager.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

PrefabLOD* PrefabLOD::s_instance = nullptr;

namespace
{
    std::string prefabAssetName(GObject* object)
    {
        std::string name = *object->getName();
        name.erase(
            std::remove_if(
                name.begin(),
                name.end(),
                [](unsigned char character)
                {
                    return !std::isprint(character);
                }),
            name.end());

        // Text after '#' identifies a scene instance and is not part of the
        // shared prefab filename (for example AbyssBeacon#07).
        const size_t instanceSeparator = name.find('#');
        if (instanceSeparator != std::string::npos)
        {
            name.erase(instanceSeparator);
        }
        return name;
    }
}

void PrefabLOD::start()
{
    s_instance = this;
    m_pc = Engine::getPtrClassAddr();
    m_ed = Editor::instance();

    const std::vector<GObject*> objlist = GObject::FindObjectsWithTag("Prefab");
    for (GObject* obj : objlist)
    {
        if (!obj)
        {
            continue;
        }

        LODObject lod;
        lod.object = obj;
        if (obj->hasTag("Large"))
        {
            lod.large = 2;
        }
        if (obj->hasTag("Medium"))
        {
            lod.large = 1;
        }
        if (obj->hasTag("LV1"))
        {
            lod.flag = 1;
        }
        m_objects.emplace_back(std::move(lod));
    }
}

void PrefabLOD::fixedUpdate()
{
}

int PrefabLOD::wantedLOD(const LODObject& lod) const
{
    Camera* camera = m_pc->cameraManager->getCurrentCamera();
    if (camera == nullptr || lod.object == nullptr)
    {
        return static_cast<int>(distanceLOD.size());
    }

    const float distance =
        glm::distance(camera->getPosition(), lod.object->getPosition());
    int newLOD = computeLOD(distance, lod.large);
    if (lod.flag == 1 && newLOD == 1)
    {
        newLOD = static_cast<int>(distanceLOD.size());
    }
    return newLOD;
}

void PrefabLOD::update()
{
    Camera* camera = m_pc->cameraManager->getCurrentCamera();
    if (!camera)
    {
        return;
    }

    for (auto& lod : m_objects)
    {
        if (lod.object == nullptr)
        {
            continue;
        }

        const int newLOD = wantedLOD(lod);
        if (newLOD == lod.currentLOD || newLOD == lod.failedLOD)
        {
            continue;
        }
        if (lod.loading && lod.wantedLOD == newLOD)
        {
            continue;
        }
        loadLOD(lod, newLOD);
    }

    for (auto& lod : m_objects)
    {
        if (!lod.pendingLoad || !lod.loading || !lod.pendingLoad->finish.load())
        {
            continue;
        }

        if (lod.pendingLoad->worker.joinable())
        {
            lod.pendingLoad->worker.join();
        }
        if (lod.pendingLoad->error.empty() && lod.pendingLoad->finalize == nullptr)
        {
            lod.pendingLoad->error = "LOD prefab finalizer is unavailable.";
            lod.pendingLoad->loadingTicket.fail();
        }
        if (lod.pendingLoad->error.empty() && lod.pendingLoad->finalize != nullptr)
        {
            lod.pendingLoad->finalize();
            lod.pendingLoad->finalize = nullptr;
        }
        if (!lod.pendingLoad->error.empty())
        {
            Debug::Warn("LOD prefab loading failed: %s", lod.pendingLoad->error.c_str());
            const int failedLOD = lod.wantedLOD;
            destroyLoad(lod.pendingLoad);
            lod.failedLOD = failedLOD;
            lod.loading = false;
            continue;
        }
        if (playPrefab) m_ed->playPrefab(lod.pendingLoad);

        destroyLoad(lod.currentLoad);
        lod.currentLoad = lod.pendingLoad;
        lod.pendingLoad = nullptr;
        lod.currentLOD = lod.wantedLOD;
        lod.loading = false;
    }
}

void PrefabLOD::stop()
{
    for (auto& lod : m_objects)
    {
        destroyLoad(lod.currentLoad);
        destroyLoad(lod.pendingLoad);
    }
    m_objects.clear();
    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

void PrefabLOD::onGUI()
{
}

int PrefabLOD::computeLOD(float distance, unsigned int large) const
{
    const unsigned int size = 1u << large;
    for (size_t i = 0; i < distanceLOD.size(); ++i)
    {
        if (distance < distanceLOD[i] * size)
        {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(distanceLOD.size());
}

void PrefabLOD::loadLOD(LODObject& lod, int lodLevel)
{
    // A movement or explicit refresh can retry a previously failed asset.
    lod.failedLOD = -1;
    if (lod.pendingLoad)
    {
        destroyLoad(lod.pendingLoad);
        lod.loading = false;
    }

    if (lodLevel >= static_cast<int>(distanceLOD.size()))
    {
        destroyLoad(lod.currentLoad);
        lod.currentLOD = lodLevel;
        return;
    }

    const std::string path =
        PathManager::getHomeProjectPath() +
        pathPrefab +
        prefabAssetName(lod.object) +
        "_lod_" +
        std::to_string(lodLevel) +
        ".scene";

    if (!fs::exists(path))
    {
        Debug::Warn("Missing LOD prefab: %s", path.c_str());
        auto failed = Ge::LoadingState::begin();
        failed.fail();
        lod.failedLOD = lodLevel;
        return;
    }

    lod.pendingLoad = m_ed->loadPrefab(path, lod.object);
    lod.wantedLOD = lodLevel;
    lod.loading = true;
}

void PrefabLOD::destroyLoad(PrefabLoad*& load)
{
    if (!load)
    {
        return;
    }

    if (load->worker.joinable())
    {
        load->worker.join();
    }
    if (load->error.empty() && load->finalize != nullptr)
    {
        load->finalize();
        load->finalize = nullptr;
    }
    if (playPrefab)
    {
        m_ed->stopPrefab(load);
    }
    m_ed->clearScene(&load->sd);
    delete load;
    load = nullptr;
}

void PrefabLOD::RefreshObject(GObject* object)
{
    if (s_instance != nullptr)
    {
        s_instance->refreshObject(object);
    }
}

void PrefabLOD::refreshObject(GObject* object)
{
    for (LODObject& lod : m_objects)
    {
        if (lod.object != object)
        {
            continue;
        }

        // Force a reload even when the distance LOD did not change: the
        // instance name may now select another visual state.
        loadLOD(lod, wantedLOD(lod));
        return;
    }
}

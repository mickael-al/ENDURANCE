#include "PrefabDefferedLoader.hpp"
#define CLEAR_REGISTRY
#include "Editor.hpp"
#include "PathManager.hpp"
#include <filesystem>
#include "Camera.hpp"

namespace fs = std::filesystem;

void PrefabDefferedLoader::start()
{
	m_pc = Engine::getPtrClassAddr();
	m_ed = Editor::instance();
	const fs::path prefabDirectory = fs::path(PathManager::getHomeProjectPath()) /
		"Ressources" / "Model" / "Prefab";
	std::vector<GObject*> objlist = GObject::FindObjectsWithTag("Prefab");
	for (unsigned int i = 0;i < objlist.size(); ++i)
	{
        std::string name = *objlist[i]->getName();
        name.erase(
            std::remove_if(name.begin(), name.end(),
                [](unsigned char c) 
                {
                    return !std::isprint(c);
                }),
            name.end()
        );

		const fs::path pfile = prefabDirectory / (name + ".scene");
		if (fs::is_regular_file(pfile))
		{
			m_prefabLoad.push_back(m_ed->loadPrefab(pfile.string()));
		}
        else
        {
            Debug::Warn("Missing prefab: %s", pfile.string().c_str());
            auto failed = Ge::LoadingState::begin();
            failed.fail();
        }
	}	    
}

void PrefabDefferedLoader::fixedUpdate()
{

}

void PrefabDefferedLoader::update()
{
    for (auto it = m_prefabLoad.begin(); it != m_prefabLoad.end();)
    {
        PrefabLoad * load = *it;

        if (!load->finish.load())
        {
            ++it;
            continue;
        }
        
        if (load->worker.joinable())
        {
            load->worker.join();
        }
        
        if (load->error.empty() && !load->finalize)
        {
            load->error = "Prefab finalizer is unavailable.";
            load->loadingTicket.fail();
        }
        if (load->error.empty() && load->finalize)
        {
            load->finalize();
            load->finalize = {};
        }
        if (!load->error.empty())
            Debug::Warn("Prefab loading failed: %s", load->error.c_str());
        else if (playPrefab)
            m_ed->playPrefab(load);
        
        m_finishedPrefabLoad.push_back(load);
        it = m_prefabLoad.erase(it);
    }
}

void PrefabDefferedLoader::stop()
{
    for (auto& load : m_prefabLoad)
    {
        if (load->worker.joinable())
        {
            load->worker.join();
        }
        if (playPrefab)
        {
            m_ed->stopPrefab(load);
        }
        m_ed->clearScene(&load->sd);
        delete load;
    }

    for (auto& load : m_finishedPrefabLoad)
    {
        if (load->worker.joinable())
        {
            load->worker.join();
        }
        if (playPrefab)
        {
            m_ed->stopPrefab(load);
        }
        m_ed->clearScene(&load->sd);
        delete load;
    }

    m_prefabLoad.clear();
    m_finishedPrefabLoad.clear();
}

void PrefabDefferedLoader::onGUI()
{

}

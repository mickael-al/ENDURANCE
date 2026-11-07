#include "Engine.hpp"
#include "GraphicsDataMisc.hpp"
#include "PointeurClass.hpp"
#include "Debug.hpp"
#include "RenderingEngine.hpp"
#include "InputManager.hpp"
#include "Time.hpp"
#include "BehaviourManager.hpp"
#include "SceneManager.hpp"
#include "Lights.hpp"
#include "PhysicsEngine.hpp"
#include "PhysicsWraper.hpp"
#include "SoundManager.hpp"
#include "Editor.hpp"
#include "PathManager.hpp"
#include "CommandQueue.hpp"
#include "PhysicsWorkerLoop.hpp"
#include <cmath>
#if ENDURANCE_ENABLE_NETWORK
#include "OnlineSessionManager.hpp"
#endif

namespace Ge
{
	ptrClass Engine::m_pointeurClass;
	Engine::Engine()
	{
		m_queue = new CommandQueue();
		m_graphicsDataMisc = new GraphicsDataMisc();
		m_pointeurClass.settingManager = m_settingManager = new SettingManager();
		m_pointeurClass.inputManager = m_inputManager = new InputManager();
		m_pointeurClass.time = m_time = new Time();
		m_pointeurClass.behaviourManager = m_behaviourManager = new BehaviourManager();
		m_pointeurClass.sceneManager = m_sceneManager = new SceneManager();
		m_physicsEngine = new PhysicsEngine();
		m_pointeurClass.physicsEngine = m_physicsEngineWraper = new PhysicsWraper(m_physicsEngine,m_queue);
		m_pointeurClass.soundManager = m_soundManager = new SoundManager();
#if ENDURANCE_ENABLE_NETWORK
		m_pointeurClass.onlineSessionManager = m_onlineSessionManager = new OnlineSessionManager();
#endif
		m_editor = new Editor();
		m_renderingEngine = new RenderingEngine(m_graphicsDataMisc);		
		m_pointeurClass.gdm = m_graphicsDataMisc;
	}

	Engine::~Engine()
	{
		m_fixedThreadRuning = false;
		m_queue->wake();
		if (m_fixedThread.joinable())
		{
			m_fixedThread.join();
		}
		delete m_queue;
		delete m_settingManager;
		delete m_inputManager;
		delete m_time;
		delete m_behaviourManager;
		delete m_sceneManager;
		delete m_graphicsDataMisc;
		delete m_physicsEngine;
		delete m_physicsEngineWraper;
		delete m_soundManager;
#if ENDURANCE_ENABLE_NETWORK
		delete m_onlineSessionManager;
		m_pointeurClass.onlineSessionManager = nullptr;
#endif
		delete m_editor;
		delete m_renderingEngine;
	}

	const ptrClass & Engine::getPtrClass()
	{
		return m_pointeurClass;
	}

	const ptrClass * Engine::getPtrClassAddr()
	{
		return &m_pointeurClass;
	}

    bool Engine::initialize()
    {
		PathManager::initDirectory();
#if ENDURANCE_ENABLE_NETWORK
		// Initialize Steam before the graphics context so its overlay can hook it.
		// Steam being unavailable keeps the editor and direct-IP transport usable.
		m_onlineSessionManager->initialize();
#endif
        if(!m_renderingEngine->initialize(&m_pointeurClass))
        {
            Debug::INITFAILED("RenderingEngine");
            return false;
        }
        if(!m_inputManager->initialize(m_graphicsDataMisc))
        {
            Debug::INITFAILED("InputManager");
            return false;
        }
		if (!m_soundManager->initialize())
		{
			Debug::INITFAILED("SoundManager");
			return false;
		}		
        return true;
    }

    void Engine::release()
    {        
        m_sceneManager->release();
        m_behaviourManager->release();
#if ENDURANCE_ENABLE_NETWORK
		m_onlineSessionManager->release();
#endif
		m_renderingEngine->release();
        m_time->release();
        m_inputManager->release();
		m_soundManager->release();
		m_fixedThreadRuning = false;
		m_queue->wake();
		if (m_fixedThread.joinable())
		{
			m_fixedThread.join();
		}
		if (m_settingManager->getEditor())
		{
			m_behaviourManager->removeBehaviour(m_editor,true);
		}
        Debug::RELEASESUCCESS("Engine");
    }

	void updatePhysique(glm::vec3 gravity, PhysicsEngine* pe,
		std::atomic<bool>* fixedThreadRuning, CommandQueue* queue)
	{
		if (!pe->Initialize(gravity, queue)) return;
		runPhysicsWorkerLoop(*queue, *fixedThreadRuning,
			[pe] { pe->ProcessCommands(); },
			[pe](float step) { pe->Update(step); });
		pe->Shutdown();
	}

    void Engine::start()
    {        
        m_time->startTime();		
		m_fixedThreadRuning = true;
		m_fixedThread = std::thread(updatePhysique, m_settingManager->getGravity(), m_physicsEngine, &m_fixedThreadRuning, m_queue);
        Debug::Info("Moteur Start");
		if (m_settingManager->getEditor())
		{
			m_behaviourManager->addBehaviour(m_editor);
		}
        m_sceneManager->loadEntryScene();
        Engine::update();
    }   

    void Engine::update()
    {
        while (!glfwWindowShouldClose(m_graphicsDataMisc->str_window))/*gestion d'evenement lier a la fermeture de la fenetre via la croix */
		{
			glfwPollEvents();/*event de recuperation*/
            m_time->fixedUpdateTime();
			m_lag += m_time->getFixedDeltaTime();
			m_behaviourManager->fixedUpdate();
			const double frameInterval = 1.0 / m_settingManager->getFps();
			if (m_lag >= frameInterval)
			{
				// Missed render frames cannot be recovered. Keep only the fractional
				// phase instead of bursting through stale frames after a slow draw.
				m_lag = static_cast<float>(std::fmod(m_lag, frameInterval));
				m_time->updateTime();
				m_inputManager->updateAxis();
#if ENDURANCE_ENABLE_NETWORK
				m_onlineSessionManager->update();
#endif
				m_behaviourManager->update();
				m_renderingEngine->drawFrame();
			}
		}
    }
}

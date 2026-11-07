#include "BehaviourManager.hpp"

namespace Ge
{
	void BehaviourManager::addBehaviour(Behaviour * b)
	{
		m_behaviours.push_back(b);
		m_startBehaviours.push_back(b);
	}

	void BehaviourManager::addBehaviourWithoutStart(Behaviour* b)
	{
		m_behaviours.push_back(b);
	}

	void BehaviourManager::removeBehaviour(Behaviour * b,bool ignoreStop)
	{
		if (b == nullptr)
		{
			return;
		}

		const bool wasWaitingForStart = std::find(m_startBehaviours.begin(), m_startBehaviours.end(), b) != m_startBehaviours.end();
		const bool wasActive = std::find(m_behaviours.begin(), m_behaviours.end(), b) != m_behaviours.end();

		m_startBehaviours.erase(std::remove(m_startBehaviours.begin(), m_startBehaviours.end(), b),m_startBehaviours.end());
		m_behaviours.erase(std::remove(m_behaviours.begin(), m_behaviours.end(), b),m_behaviours.end());
		if (m_pauseExempt == b)
		{
			m_pauseExempt = nullptr;
		}

		if (ignoreStop)
		{
			m_stopBehaviours.erase(std::remove(m_stopBehaviours.begin(), m_stopBehaviours.end(), b),m_stopBehaviours.end());
		}
		else if (wasActive && !wasWaitingForStart && std::find(m_stopBehaviours.begin(), m_stopBehaviours.end(), b) == m_stopBehaviours.end())
		{
			m_stopBehaviours.push_back(b);
		}
	}

	void BehaviourManager::setPaused(bool paused, Behaviour* exemptBehaviour)
	{
		m_paused = paused;
		m_pauseExempt = paused ? exemptBehaviour : nullptr;
	}

	bool BehaviourManager::isPaused() const
	{
		return m_paused;
	}

	void BehaviourManager::update()
	{
		if (!m_paused)
		{
			for (size_t i = 0; i < m_startBehaviours.size();)
			{
				Behaviour* behaviour = m_startBehaviours[i];
				if (behaviour == nullptr)
				{
					m_startBehaviours.erase(m_startBehaviours.begin() + i);
					continue;
				}

				behaviour->start();

				if (i < m_startBehaviours.size() && m_startBehaviours[i] == behaviour)
				{
					++i;
				}
			}
			m_startBehaviours.clear();
		}

		for (size_t i = 0; i < m_behaviours.size() && (m_paused || m_startBehaviours.empty());)
		{
			Behaviour* behaviour = m_behaviours[i];
			if (behaviour == nullptr)
			{
				m_behaviours.erase(m_behaviours.begin() + i);
				continue;
			}

			if (!m_paused || behaviour == m_pauseExempt)
			{
				behaviour->update();
			}

			if (i < m_behaviours.size() && m_behaviours[i] == behaviour)
			{
				++i;
			}
		}

		for (size_t i = 0; i < m_stopBehaviours.size();)
		{
			Behaviour* behaviour = m_stopBehaviours[i];
			if (behaviour == nullptr)
			{
				m_stopBehaviours.erase(m_stopBehaviours.begin() + i);
				continue;
			}

			behaviour->stop();

			if (i < m_stopBehaviours.size() && m_stopBehaviours[i] == behaviour)
			{
				++i;
			}
		}
		m_stopBehaviours.clear();
	}

	void BehaviourManager::release()
	{
		for (int i = 0; i < m_stopBehaviours.size(); i++)
		{
			if (m_stopBehaviours[i] != nullptr)
			{
				m_stopBehaviours[i]->stop();
			}
		}
		for (int i = 0; i < m_behaviours.size(); i++)
		{
			if (m_behaviours[i] != nullptr)
			{
				m_behaviours[i]->stop();
			}
		}
		m_stopBehaviours.clear();
		m_behaviours.clear();
		m_startBehaviours.clear();
		m_paused = false;
		m_pauseExempt = nullptr;
	}

	void BehaviourManager::fixedUpdate()
	{
		for (size_t i = 0; i < m_behaviours.size();)
		{
			Behaviour* behaviour = m_behaviours[i];
			if (behaviour == nullptr)
			{
				m_behaviours.erase(m_behaviours.begin() + i);
				continue;
			}

			if (!m_paused || behaviour == m_pauseExempt)
			{
				behaviour->fixedUpdate();
			}

			if (i < m_behaviours.size() && m_behaviours[i] == behaviour)
			{
				++i;
			}
		}
	}


}

#ifndef __ENGINE_BEHAVIOUR_MANAGER__
#define __ENGINE_BEHAVIOUR_MANAGER__

#include "Behaviour.hpp"
#include <vector>
#include <algorithm>

namespace Ge
{
	class BehaviourManager
	{
	public:
		void addBehaviour(Behaviour* b);
		void addBehaviourWithoutStart(Behaviour* b);
		void removeBehaviour(Behaviour * b,bool ignoreStop = false);
		void setPaused(bool paused, Behaviour* exemptBehaviour = nullptr);
		bool isPaused() const;
		bool hasPendingStarts() const { return !m_startBehaviours.empty(); }
		void update();
		void fixedUpdate();		
		void release();
	private:
		std::vector<Behaviour *>  m_behaviours;
		std::vector<Behaviour *>  m_startBehaviours;
		std::vector<Behaviour *>  m_stopBehaviours;
		bool m_paused = false;
		Behaviour* m_pauseExempt = nullptr;
	};
}

#endif //__ENGINE_BEHAVIOUR_MANAGER__
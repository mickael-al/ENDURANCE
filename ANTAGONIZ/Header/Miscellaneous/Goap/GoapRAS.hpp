#ifndef __GOAP_RAS_MANAGER__
#define __GOAP_RAS_MANAGER__

#include "Goap.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class GAction;

// Reverse A* GOAP planner. It regresses the objective preconditions toward an
// immutable resource snapshot. Non-invertible effects are ignored by design.
class GoapRAS final
{
public:
	GoapRAS(std::vector<GAction*>& allActions, std::vector<GAction*>& objectifs);
	~GoapRAS();
	std::unordered_map<std::string, float>& getRessource();
	std::vector<const GAction*> Resolve();

	// Runs an independent reachability pass using the same static-resource and
	// invertible-effect rules as Resolve(). Deepest missing dependencies come first.
	std::vector<GGoapBlocker> FindPlanBlockers() const;

	void addObjectif(GAction* objectif);

private:
	std::vector<GAction*> m_allActions;
	std::vector<GAction*> m_objectifs;
	std::unordered_map<std::string, float> m_ressource;
};

#endif

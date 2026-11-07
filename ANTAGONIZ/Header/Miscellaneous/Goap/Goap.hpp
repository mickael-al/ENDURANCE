#ifndef __GOAP_MANAGER__
#define __GOAP_MANAGER__

#include <vector>
#include <unordered_map>
#include <string>

class GAction;
class GPreCondition;

// One unsatisfied precondition from the closest state reached by the diagnostic pass.
struct GGoapBlocker
{
	const GAction* action = nullptr;
	const GPreCondition* preCondition = nullptr;
	std::string actionName;
	std::string ressource;
	float closestValue = 0.0f;
	float requiredValue = 0.0f;
};

class Goap final
{
public:
	Goap(std::vector<GAction*> & allActions, std::vector<GAction*> & objectifs);
	~Goap();
	std::unordered_map<std::string, float>& getRessource();
	std::vector<const GAction*> Resolve();

	// Runs an independent reachability pass. Call it after Resolve() returns no plan
	// to identify the closest unsatisfied resources for each relevant action.
	std::vector<GGoapBlocker> FindPlanBlockers() const;

	void addObjectif(GAction * ga);
private:
	void CalculateResolver() const;
private:
	std::vector<GAction*> m_allActions;
	std::vector<GAction*> m_objectifs;
	std::unordered_map<std::string, float> m_ressource;
}; 

#endif //!__GOAP_MANAGER__

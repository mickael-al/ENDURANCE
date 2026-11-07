#ifndef __G_NODE__
#define __G_NODE__

#include <string>
#include <unordered_map>
#include <vector>

class GAction;

struct GNode
{
	std::vector<const GAction*> openNode;
	const GAction* action = nullptr;
	std::unordered_map<std::string, float> ressource;
	unsigned int cost = 0;
	GNode* parent = nullptr;
};

#endif //!__G_NODE__

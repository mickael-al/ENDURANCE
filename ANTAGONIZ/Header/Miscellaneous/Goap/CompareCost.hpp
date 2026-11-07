#ifndef __COMPARE_COST__
#define __COMPARE_COST__

#include "GNode.hpp"

struct CompareCost
{
	bool operator()(const GNode* const left, const GNode* const right) const
	{
		if (left->cost != right->cost)
		{
			return left->cost > right->cost;
		}
		return left->openNode.size() > right->openNode.size();
	}
};

#endif //!__COMPARE_COST__

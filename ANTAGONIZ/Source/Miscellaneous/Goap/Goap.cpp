#include "Goap.hpp"

#include "Debug.hpp"
#include "GAction.hpp"
#include "GEffect.hpp"
#include "GPreCondition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace Ge;

namespace
{
	using WorldState = std::unordered_map<std::string, float>;
	constexpr std::size_t MaxExpandedNodes = 100000;
	constexpr std::size_t MaxPlanLength = 256;

	struct StateScore
	{
		std::uint64_t cost = 0;
		std::size_t actionCount = 0;
	};

	struct SearchNode
	{
		WorldState state;
		std::vector<const GAction*> path;
		std::uint64_t cost = 0;
		std::uint64_t insertionOrder = 0;
		bool goalReached = false;
	};

	struct CompareSearchNode
	{
		bool operator()(const SearchNode& left, const SearchNode& right) const
		{
			if (left.cost != right.cost)
			{
				return left.cost > right.cost;
			}
			if (left.path.size() != right.path.size())
			{
				return left.path.size() > right.path.size();
			}
			return left.insertionOrder > right.insertionOrder;
		}
	};

	struct DiagnosticState
	{
		WorldState state;
		std::size_t depth = 0;
	};

	struct ActionDiagnostic
	{
		const GAction* action = nullptr;
		std::size_t dependencyDepth = 0;
		bool executable = false;
		bool initialized = false;
		std::size_t bestSatisfiedCount = 0;
		double bestDistance = std::numeric_limits<double>::infinity();
		std::vector<GGoapBlocker> blockers;
	};

	bool CanExecute(const GAction& action, const WorldState& state)
	{
		for (const GPreCondition* condition : action.GetPreConditions())
		{
			if (condition == nullptr || !condition->CheckPreCondition(state))
			{
				return false;
			}
		}
		return true;
	}

	bool Simulate(const GAction& action, WorldState& state)
	{
		for (const GEffect* effect : action.GetEffects())
		{
			if (effect != nullptr)
			{
				effect->SimulateEffect(state);
			}
		}
		for (const auto& ressource : state)
		{
			if (!std::isfinite(ressource.second))
			{
				return false;
			}
		}
		return true;
	}

	float GetStateValue(const WorldState& state, const std::string& ressource)
	{
		const auto value = state.find(ressource);
		return value == state.end() ? 0.0f : value->second;
	}

	double ConditionDistance(float current, Condition condition, float required)
	{
		const double strictStep =
			std::max(1.0, std::abs(static_cast<double>(required))) *
			static_cast<double>(std::numeric_limits<float>::epsilon());
		switch (condition)
		{
		case Condition::INF:
			return current < required ? 0.0 :
				static_cast<double>(current) - required + strictStep;
		case Condition::SUP:
			return current > required ? 0.0 :
				static_cast<double>(required) - current + strictStep;
		case Condition::INF_EQUALS:
			return current <= required ? 0.0 :
				static_cast<double>(current) - required;
		case Condition::SUP_EQUALS:
			return current >= required ? 0.0 :
				static_cast<double>(required) - current;
		default:
			return std::numeric_limits<double>::infinity();
		}
	}

	std::vector<ActionDiagnostic> BuildActionDiagnostics(const GAction* objectif)
	{
		std::vector<ActionDiagnostic> diagnostics;
		if (objectif == nullptr)
		{
			return diagnostics;
		}

		std::unordered_map<const GAction*, std::size_t> knownActions;
		diagnostics.push_back({ objectif, 0 });
		knownActions[objectif] = 0;

		for (std::size_t index = 0; index < diagnostics.size(); ++index)
		{
			const GAction* action = diagnostics[index].action;
			if (action == nullptr)
			{
				continue;
			}
			for (const GPreCondition* condition : action->GetPreConditions())
			{
				if (condition == nullptr)
				{
					continue;
				}
				for (const GAction* resolver : condition->GetResolver())
				{
					if (resolver == nullptr || knownActions.find(resolver) != knownActions.end())
					{
						continue;
					}
					knownActions[resolver] = diagnostics.size();
					diagnostics.push_back(
						{ resolver, diagnostics[index].dependencyDepth + 1 });
				}
			}
		}
		return diagnostics;
	}

	void UpdateActionDiagnostic(ActionDiagnostic& diagnostic, const WorldState& state)
	{
		if (diagnostic.executable || diagnostic.action == nullptr)
		{
			return;
		}

		GNode evaluationNode;
		evaluationNode.ressource = state;
		std::size_t satisfiedCount = 0;
		double distance = 0.0;
		std::vector<GGoapBlocker> blockers;
		for (const GPreCondition* condition : diagnostic.action->GetPreConditions())
		{
			if (condition != nullptr && condition->CheckPreCondition(state))
			{
				++satisfiedCount;
				continue;
			}

			GGoapBlocker blocker;
			blocker.action = diagnostic.action;
			blocker.preCondition = condition;
			blocker.actionName = diagnostic.action->GetName();
			if (condition == nullptr)
			{
				blocker.ressource = "<null precondition>";
				distance = std::numeric_limits<double>::infinity();
			}
			else
			{
				blocker.ressource = condition->GetRessource();
				blocker.closestValue = GetStateValue(state, blocker.ressource);
				blocker.requiredValue = condition->GetValue(&evaluationNode);
				distance += ConditionDistance(
					blocker.closestValue,
					condition->GetCondition(),
					blocker.requiredValue);
			}
			blockers.push_back(std::move(blocker));
		}

		if (blockers.empty())
		{
			diagnostic.executable = true;
			diagnostic.blockers.clear();
			return;
		}

		const bool isBetter =
			!diagnostic.initialized ||
			satisfiedCount > diagnostic.bestSatisfiedCount ||
			(satisfiedCount == diagnostic.bestSatisfiedCount &&
			 distance < diagnostic.bestDistance);
		if (isBetter)
		{
			diagnostic.initialized = true;
			diagnostic.bestSatisfiedCount = satisfiedCount;
			diagnostic.bestDistance = distance;
			diagnostic.blockers = std::move(blockers);
		}
	}

	std::string StateKey(const WorldState& state)
	{
		std::vector<std::pair<std::string, float>> orderedState;
		orderedState.reserve(state.size());
		for (const auto& ressource : state)
		{
			// Missing resources and explicit zero resources have identical GOAP semantics.
			if (ressource.second != 0.0f)
			{
				orderedState.push_back(ressource);
			}
		}
		std::sort(
			orderedState.begin(),
			orderedState.end(),
			[](const auto& left, const auto& right) { return left.first < right.first; });

		std::string key;
		for (const auto& ressource : orderedState)
		{
			std::uint32_t valueBits = 0;
			static_assert(sizeof(valueBits) == sizeof(ressource.second), "Unexpected float size");
			std::memcpy(&valueBits, &ressource.second, sizeof(valueBits));
			key += std::to_string(ressource.first.size());
			key += ':';
			key += ressource.first;
			key += ':';
			key += std::to_string(valueBits);
			key += ';';
		}
		return key;
	}
}

Goap::Goap(std::vector<GAction*>& allActions, std::vector<GAction*>& objectifs)
	: m_allActions(allActions), m_objectifs(objectifs)
{
	for (GAction* objectif : m_objectifs)
	{
		if (objectif == nullptr ||
			std::find(m_allActions.begin(), m_allActions.end(), objectif) == m_allActions.end())
		{
			Debug::Error("Objectif n'est pas dans action");
		}
	}
	CalculateResolver();
}

Goap::~Goap() = default;

void Goap::CalculateResolver() const
{
	// Kept for compatibility with code inspecting GetResolver(). The planner itself
	// simulates complete states, because clamp/resource effects cannot be inverted.
	for (GAction* owner : m_allActions)
	{
		if (owner == nullptr)
		{
			continue;
		}
		for (GPreCondition* condition : owner->GetPreConditions())
		{
			if (condition == nullptr)
			{
				continue;
			}
			std::vector<const GAction*> resolvers;
			for (GAction* candidate : m_allActions)
			{
				if (candidate == nullptr)
				{
					continue;
				}
				for (const GEffect* effect : candidate->GetEffects())
				{
					if (effect != nullptr && effect->GetRessource() == condition->GetRessource())
					{
						resolvers.push_back(candidate);
						break;
					}
				}
			}
			condition->SetResolver(resolvers);
		}
	}
}

std::unordered_map<std::string, float>& Goap::getRessource()
{
	return m_ressource;
}

void Goap::addObjectif(GAction* objectif)
{
	if (objectif == nullptr ||
		std::find(m_allActions.begin(), m_allActions.end(), objectif) == m_allActions.end())
	{
		Debug::Error("Objectif n'est pas dans action");
		return;
	}
	m_objectifs.push_back(objectif);
}

std::vector<const GAction*> Goap::Resolve()
{
	if (m_objectifs.empty() || m_objectifs.front() == nullptr)
	{
		Debug::Error("Aucun objectif a resoudre.");
		return {};
	}

	const GAction* const objectif = m_objectifs.front();
	std::priority_queue<SearchNode, std::vector<SearchNode>, CompareSearchNode> openNodes;
	std::unordered_map<std::string, StateScore> bestScoreByState;
	std::uint64_t insertionOrder = 0;

	SearchNode firstNode;
	firstNode.state = m_ressource;
	firstNode.insertionOrder = insertionOrder++;
	bestScoreByState[StateKey(firstNode.state)] = { 0, 0 };
	openNodes.push(std::move(firstNode));

	std::size_t expandedNodes = 0;
	bool planLengthLimitReached = false;
	while (!openNodes.empty() && expandedNodes < MaxExpandedNodes)
	{
		SearchNode current = openNodes.top();
		openNodes.pop();

		if (current.goalReached)
		{
			return current.path;
		}

		const std::string currentKey = StateKey(current.state);
		const auto knownScore = bestScoreByState.find(currentKey);
		if (knownScore != bestScoreByState.end() &&
			(current.cost > knownScore->second.cost ||
				(current.cost == knownScore->second.cost &&
				 current.path.size() > knownScore->second.actionCount)))
		{
			continue;
		}
		++expandedNodes;
		if (current.path.size() >= MaxPlanLength)
		{
			planLengthLimitReached = true;
			continue;
		}

		for (const GAction* action : m_allActions)
		{
			if (action == nullptr || !CanExecute(*action, current.state))
			{
				continue;
			}

			SearchNode next;
			next.state = current.state;
			if (!Simulate(*action, next.state))
			{
				continue;
			}
			next.cost = current.cost + static_cast<std::uint64_t>(action->GetCost());
			next.path = current.path;
			next.path.push_back(action);
			next.insertionOrder = insertionOrder++;
			next.goalReached = action == objectif;

			if (next.goalReached)
			{
				// Every action cost is non-negative. Reaching the objective with only
				// its own cost is therefore the absolute lower bound, even when an
				// unbounded zero-cost action could keep creating new states.
				if (next.cost == static_cast<std::uint64_t>(objectif->GetCost()))
				{
					return next.path;
				}
				openNodes.push(std::move(next));
				continue;
			}

			const std::string nextKey = StateKey(next.state);
			// An action with no represented state change can never unlock another action.
			if (nextKey == currentKey)
			{
				continue;
			}

			const auto previousScore = bestScoreByState.find(nextKey);
			if (previousScore != bestScoreByState.end() &&
				(previousScore->second.cost < next.cost ||
					(previousScore->second.cost == next.cost &&
					 previousScore->second.actionCount <= next.path.size())))
			{
				continue;
			}
			bestScoreByState[nextKey] = { next.cost, next.path.size() };
			openNodes.push(std::move(next));
		}
	}

	if (expandedNodes >= MaxExpandedNodes)
	{
		Debug::Warn("GOAP: limite de recherche atteinte (%d noeuds).", static_cast<int>(MaxExpandedNodes));
	}
	if (planLengthLimitReached)
	{
		Debug::Warn("GOAP: longueur maximale du plan atteinte (%d actions).", static_cast<int>(MaxPlanLength));
	}
	Debug::Error("Aucun chemin trouve.");
	return {};
}


std::vector<GGoapBlocker> Goap::FindPlanBlockers() const
{
	if (m_objectifs.empty() || m_objectifs.front() == nullptr)
	{
		Debug::Error("GOAP diagnostic: aucun objectif a analyser.");
		return {};
	}

	std::vector<ActionDiagnostic> diagnostics =
		BuildActionDiagnostics(m_objectifs.front());
	std::queue<DiagnosticState> openStates;
	std::unordered_set<std::string> visitedStates;

	DiagnosticState firstState;
	firstState.state = m_ressource;
	visitedStates.insert(StateKey(firstState.state));
	openStates.push(std::move(firstState));

	std::size_t expandedStates = 0;
	bool planLengthLimitReached = false;
	while (!openStates.empty() && expandedStates < MaxExpandedNodes)
	{
		DiagnosticState current = std::move(openStates.front());
		openStates.pop();
		++expandedStates;

		for (ActionDiagnostic& diagnostic : diagnostics)
		{
			UpdateActionDiagnostic(diagnostic, current.state);
		}
		if (!diagnostics.empty() && diagnostics.front().executable)
		{
			// The objective is reachable. An empty result is intentional even when
			// this method is called without a preceding failed Resolve().
			return {};
		}

		if (current.depth >= MaxPlanLength)
		{
			planLengthLimitReached = true;
			continue;
		}

		for (const GAction* action : m_allActions)
		{
			if (action == nullptr || !CanExecute(*action, current.state))
			{
				continue;
			}

			DiagnosticState next;
			next.state = current.state;
			next.depth = current.depth + 1;
			if (!Simulate(*action, next.state))
			{
				continue;
			}

			const std::string key = StateKey(next.state);
			if (visitedStates.insert(key).second)
			{
				openStates.push(std::move(next));
			}
		}
	}

	if (expandedStates >= MaxExpandedNodes)
	{
		Debug::Warn(
			"GOAP diagnostic: limite de recherche atteinte (%d etats).",
			static_cast<int>(MaxExpandedNodes));
	}
	if (planLengthLimitReached)
	{
		Debug::Warn(
			"GOAP diagnostic: longueur maximale atteinte (%d actions).",
			static_cast<int>(MaxPlanLength));
	}

	// The deepest dependencies are generally the root causes, so expose them first.
	std::stable_sort(
		diagnostics.begin(),
		diagnostics.end(),
		[](const ActionDiagnostic& left, const ActionDiagnostic& right)
		{
			return left.dependencyDepth > right.dependencyDepth;
		});

	std::vector<GGoapBlocker> blockers;
	for (const ActionDiagnostic& diagnostic : diagnostics)
	{
		if (!diagnostic.executable && diagnostic.initialized)
		{
			blockers.insert(
				blockers.end(),
				diagnostic.blockers.begin(),
				diagnostic.blockers.end());
		}
	}
	return blockers;
}

#include "GoapRAS.hpp"

#include "Debug.hpp"
#include "GAction.hpp"
#include "GEffect.hpp"
#include "GNode.hpp"
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
	constexpr std::uint64_t InfiniteCost = std::numeric_limits<std::uint64_t>::max();

	struct Requirement
	{
		bool hasLower = false;
		double lower = 0.0;
		bool lowerInclusive = true;
		bool hasUpper = false;
		double upper = 0.0;
		bool upperInclusive = true;
	};

	using RequirementState = std::unordered_map<std::string, Requirement>;

	struct ActionModel
	{
		const GAction* action = nullptr;
		std::unordered_map<std::string, double> deltas;
		RequirementState preConditions;
		bool valid = true;
	};

	struct StateScore
	{
		std::uint64_t cost = 0;
		std::size_t actionCount = 0;
	};

	struct SearchNode
	{
		RequirementState requirements;
		std::vector<const GAction*> reversePath;
		std::uint64_t cost = 0;
		std::uint64_t heuristic = 0;
		std::uint64_t insertionOrder = 0;
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

	std::uint64_t SaturatedAdd(std::uint64_t left, std::uint64_t right)
	{
		return right > InfiniteCost - left ? InfiniteCost : left + right;
	}

	struct CompareSearchNode
	{
		bool operator()(const SearchNode& left, const SearchNode& right) const
		{
			const std::uint64_t leftTotal = SaturatedAdd(left.cost, left.heuristic);
			const std::uint64_t rightTotal = SaturatedAdd(right.cost, right.heuristic);
			if (leftTotal != rightTotal)
			{
				return leftTotal > rightTotal;
			}
			if (left.cost != right.cost)
			{
				return left.cost > right.cost;
			}
			if (left.reversePath.size() != right.reversePath.size())
			{
				return left.reversePath.size() > right.reversePath.size();
			}
			return left.insertionOrder > right.insertionOrder;
		}
	};

	float GetStaticValue(const WorldState& state, const std::string& ressource)
	{
		const auto value = state.find(ressource);
		return value == state.end() ? 0.0f : value->second;
	}

	bool IsConsistent(const Requirement& requirement)
	{
		if (!requirement.hasLower || !requirement.hasUpper)
		{
			return true;
		}
		if (requirement.lower < requirement.upper)
		{
			return true;
		}
		return requirement.lower == requirement.upper &&
			requirement.lowerInclusive && requirement.upperInclusive;
	}

	void MergeLowerBound(Requirement& requirement, double value, bool inclusive)
	{
		if (!requirement.hasLower || value > requirement.lower)
		{
			requirement.hasLower = true;
			requirement.lower = value;
			requirement.lowerInclusive = inclusive;
		}
		else if (value == requirement.lower)
		{
			requirement.lowerInclusive = requirement.lowerInclusive && inclusive;
		}
	}

	void MergeUpperBound(Requirement& requirement, double value, bool inclusive)
	{
		if (!requirement.hasUpper || value < requirement.upper)
		{
			requirement.hasUpper = true;
			requirement.upper = value;
			requirement.upperInclusive = inclusive;
		}
		else if (value == requirement.upper)
		{
			requirement.upperInclusive = requirement.upperInclusive && inclusive;
		}
	}

	bool MergeCondition(
		RequirementState& requirements,
		const GPreCondition* condition,
		const WorldState& staticRessources)
	{
		if (condition == nullptr)
		{
			return false;
		}

		GNode staticNode;
		staticNode.ressource = staticRessources;
		const double value = static_cast<double>(condition->GetValue(&staticNode));
		if (!std::isfinite(value))
		{
			return false;
		}

		Requirement& requirement = requirements[condition->GetRessource()];
		switch (condition->GetCondition())
		{
		case Condition::INF:
			MergeUpperBound(requirement, value, false);
			break;
		case Condition::SUP:
			MergeLowerBound(requirement, value, false);
			break;
		case Condition::INF_EQUALS:
			MergeUpperBound(requirement, value, true);
			break;
		case Condition::SUP_EQUALS:
			MergeLowerBound(requirement, value, true);
			break;
		default:
			return false;
		}
		return IsConsistent(requirement);
	}

	bool MergeConditions(
		RequirementState& requirements,
		const GAction& action,
		const WorldState& staticRessources)
	{
		for (const GPreCondition* condition : action.GetPreConditions())
		{
			if (!MergeCondition(requirements, condition, staticRessources))
			{
				return false;
			}
		}
		return true;
	}

	bool MergeRequirements(RequirementState& destination, const RequirementState& source)
	{
		for (const auto& entry : source)
		{
			Requirement& requirement = destination[entry.first];
			const Requirement& added = entry.second;
			if (added.hasLower)
			{
				MergeLowerBound(requirement, added.lower, added.lowerInclusive);
			}
			if (added.hasUpper)
			{
				MergeUpperBound(requirement, added.upper, added.upperInclusive);
			}
			if (!IsConsistent(requirement))
			{
				return false;
			}
		}
		return true;
	}

	bool Satisfies(double value, const Requirement& requirement)
	{
		if (requirement.hasLower &&
			(value < requirement.lower ||
				(value == requirement.lower && !requirement.lowerInclusive)))
		{
			return false;
		}
		if (requirement.hasUpper &&
			(value > requirement.upper ||
				(value == requirement.upper && !requirement.upperInclusive)))
		{
			return false;
		}
		return true;
	}

	bool AllRequirementsSatisfied(
		const RequirementState& requirements,
		const WorldState& staticRessources)
	{
		for (const auto& entry : requirements)
		{
			if (!Satisfies(GetStaticValue(staticRessources, entry.first), entry.second))
			{
				return false;
			}
		}
		return true;
	}

	ActionModel BuildActionModel(const GAction* action, const WorldState& staticRessources)
	{
		ActionModel model;
		model.action = action;
		if (action == nullptr)
		{
			model.valid = false;
			return model;
		}

		model.valid = MergeConditions(model.preConditions, *action, staticRessources);
		for (const GEffect* effect : action->GetEffects())
		{
			float delta = 0.0f;
			if (effect != nullptr && effect->TryGetReverseDelta(staticRessources, delta) &&
				std::isfinite(delta) && delta != 0.0f)
			{
				model.deltas[effect->GetRessource()] += static_cast<double>(delta);
			}
		}
		for (auto delta = model.deltas.begin(); delta != model.deltas.end();)
		{
			if (!std::isfinite(delta->second) || delta->second == 0.0)
			{
				delta = model.deltas.erase(delta);
			}
			else
			{
				++delta;
			}
		}
		return model;
	}

	double ConditionDistance(float current, Condition condition, float required)
	{
		if (!std::isfinite(current) || !std::isfinite(required))
		{
			return std::numeric_limits<double>::infinity();
		}

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

	bool DeltaCanResolve(Condition condition, double delta)
	{
		switch (condition)
		{
		case Condition::INF:
		case Condition::INF_EQUALS:
			return delta < 0.0;
		case Condition::SUP:
		case Condition::SUP_EQUALS:
			return delta > 0.0;
		default:
			return false;
		}
	}

	std::vector<ActionDiagnostic> BuildActionDiagnostics(
		const GAction* objectif,
		const std::vector<ActionModel>& actionModels)
	{
		std::vector<ActionDiagnostic> diagnostics;
		if (objectif == nullptr)
		{
			return diagnostics;
		}

		std::unordered_map<const GAction*, std::size_t> knownActions;
		ActionDiagnostic objectiveDiagnostic;
		objectiveDiagnostic.action = objectif;
		diagnostics.push_back(std::move(objectiveDiagnostic));
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
				for (const ActionModel& model : actionModels)
				{
					if (model.action == nullptr ||
						knownActions.find(model.action) != knownActions.end())
					{
						continue;
					}
					const auto delta = model.deltas.find(condition->GetRessource());
					if (delta == model.deltas.end() ||
						!DeltaCanResolve(condition->GetCondition(), delta->second))
					{
						continue;
					}

					knownActions[model.action] = diagnostics.size();
					ActionDiagnostic dependencyDiagnostic;
					dependencyDiagnostic.action = model.action;
					dependencyDiagnostic.dependencyDepth =
						diagnostics[index].dependencyDepth + 1;
					diagnostics.push_back(std::move(dependencyDiagnostic));
				}
			}
		}
		return diagnostics;
	}

	void UpdateActionDiagnostic(
		ActionDiagnostic& diagnostic,
		const WorldState& state,
		const WorldState& staticRessources)
	{
		if (diagnostic.executable || diagnostic.action == nullptr)
		{
			return;
		}

		GNode staticNode;
		staticNode.ressource = staticRessources;
		std::size_t satisfiedCount = 0;
		double distance = 0.0;
		std::vector<GGoapBlocker> blockers;
		for (const GPreCondition* condition : diagnostic.action->GetPreConditions())
		{
			GGoapBlocker blocker;
			blocker.action = diagnostic.action;
			blocker.preCondition = condition;
			blocker.actionName = diagnostic.action->GetName();

			if (condition == nullptr)
			{
				blocker.ressource = "<null precondition>";
				distance = std::numeric_limits<double>::infinity();
				blockers.push_back(std::move(blocker));
				continue;
			}

			blocker.ressource = condition->GetRessource();
			blocker.closestValue = GetStaticValue(state, blocker.ressource);
			blocker.requiredValue = condition->GetValue(&staticNode);
			const double conditionDistance = ConditionDistance(
				blocker.closestValue,
				condition->GetCondition(),
				blocker.requiredValue);
			if (conditionDistance == 0.0)
			{
				++satisfiedCount;
				continue;
			}
			distance += conditionDistance;
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

	bool ApplyActionModel(const ActionModel& model, WorldState& state)
	{
		for (const auto& delta : model.deltas)
		{
			const double result =
				static_cast<double>(GetStaticValue(state, delta.first)) + delta.second;
			if (!std::isfinite(result))
			{
				return false;
			}
			const float storedResult = static_cast<float>(result);
			if (!std::isfinite(storedResult))
			{
				return false;
			}
			state[delta.first] = storedResult;
		}
		return true;
	}

	bool IsRelevant(
		const ActionModel& model,
		const RequirementState& requirements)
	{
		// A requirement that is satisfied in the initial state may still need to be
		// restored after an earlier action changes it. Regress every action that can
		// affect an active requirement and let the state/cost checks prune it later.
		for (const auto& entry : requirements)
		{
			const auto delta = model.deltas.find(entry.first);
			if (delta != model.deltas.end() && delta->second != 0.0)
			{
				return true;
			}
		}
		return false;
	}

	bool Regress(
		RequirementState& requirements,
		const ActionModel& model)
	{
		for (const auto& delta : model.deltas)
		{
			const auto requirement = requirements.find(delta.first);
			if (requirement == requirements.end())
			{
				continue;
			}
			if (requirement->second.hasLower)
			{
				requirement->second.lower -= delta.second;
				if (requirement->second.lower == 0.0)
				{
					requirement->second.lower = 0.0;
				}
			}
			if (requirement->second.hasUpper)
			{
				requirement->second.upper -= delta.second;
				if (requirement->second.upper == 0.0)
				{
					requirement->second.upper = 0.0;
				}
			}
			if ((requirement->second.hasLower && !std::isfinite(requirement->second.lower)) ||
				(requirement->second.hasUpper && !std::isfinite(requirement->second.upper)))
			{
				return false;
			}
		}
		return MergeRequirements(requirements, model.preConditions);
	}

	std::uint64_t EstimateRemainingCost(
		const RequirementState& requirements,
		const WorldState& staticRessources,
		const std::vector<ActionModel>& actions,
		const GAction* objectif)
	{
		std::uint64_t estimate = 0;
		for (const auto& entry : requirements)
		{
			if (Satisfies(GetStaticValue(staticRessources, entry.first), entry.second))
			{
				continue;
			}
			std::uint64_t cheapest = InfiniteCost;
			for (const ActionModel& model : actions)
			{
				if (!model.valid || model.action == nullptr || model.action == objectif)
				{
					continue;
				}
				const auto delta = model.deltas.find(entry.first);
				if (delta != model.deltas.end() && delta->second != 0.0)
				{
					cheapest = std::min(
						cheapest,
						static_cast<std::uint64_t>(model.action->GetCost()));
				}
			}
			if (cheapest == InfiniteCost)
			{
				return InfiniteCost;
			}
			estimate = std::max(estimate, cheapest);
		}
		return estimate;
	}

	void AppendDouble(std::string& key, double value)
	{
		if (value == 0.0)
		{
			value = 0.0;
		}
		std::uint64_t bits = 0;
		static_assert(sizeof(bits) == sizeof(value), "Unexpected double size");
		std::memcpy(&bits, &value, sizeof(bits));
		key += std::to_string(bits);
	}

	std::string StateKey(const RequirementState& requirements)
	{
		std::vector<std::pair<std::string, Requirement>> ordered(
			requirements.begin(), requirements.end());
		std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
		{
			return left.first < right.first;
		});

		std::string key;
		for (const auto& entry : ordered)
		{
			key += std::to_string(entry.first.size());
			key += ':';
			key += entry.first;
			key += ':';
			key += entry.second.hasLower ? '1' : '0';
			if (entry.second.hasLower)
			{
				key += entry.second.lowerInclusive ? '1' : '0';
				AppendDouble(key, entry.second.lower);
			}
			key += ':';
			key += entry.second.hasUpper ? '1' : '0';
			if (entry.second.hasUpper)
			{
				key += entry.second.upperInclusive ? '1' : '0';
				AppendDouble(key, entry.second.upper);
			}
			key += ';';
		}
		return key;
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

GoapRAS::GoapRAS(std::vector<GAction*>& allActions, std::vector<GAction*>& objectifs)
	: m_allActions(allActions), m_objectifs(objectifs)
{
	for (GAction* objectif : m_objectifs)
	{
		if (objectif == nullptr ||
			std::find(m_allActions.begin(), m_allActions.end(), objectif) == m_allActions.end())
		{
			Debug::Error("GOAP RAS: objectif absent de la liste des actions.");
		}
	}
}

GoapRAS::~GoapRAS() = default;

std::unordered_map<std::string, float>& GoapRAS::getRessource()
{
	return m_ressource;
}

void GoapRAS::addObjectif(GAction* objectif)
{
	if (objectif == nullptr ||
		std::find(m_allActions.begin(), m_allActions.end(), objectif) == m_allActions.end())
	{
		Debug::Error("GOAP RAS: objectif absent de la liste des actions.");
		return;
	}
	m_objectifs.push_back(objectif);
}

std::vector<const GAction*> GoapRAS::Resolve()
{
	if (m_objectifs.empty() || m_objectifs.front() == nullptr)
	{
		Debug::Error("GOAP RAS: aucun objectif a resoudre.");
		return {};
	}

	const GAction* const objectif = m_objectifs.front();
	const WorldState staticRessources = m_ressource;
	std::vector<ActionModel> actionModels;
	actionModels.reserve(m_allActions.size());
	for (const GAction* action : m_allActions)
	{
		actionModels.push_back(BuildActionModel(action, staticRessources));
	}

	SearchNode firstNode;
	if (!MergeConditions(firstNode.requirements, *objectif, staticRessources))
	{
		Debug::Error("GOAP RAS: preconditions contradictoires ou invalides sur l'objectif.");
		return {};
	}
	firstNode.reversePath.push_back(objectif);
	firstNode.cost = static_cast<std::uint64_t>(objectif->GetCost());
	firstNode.heuristic = EstimateRemainingCost(
		firstNode.requirements, staticRessources, actionModels, objectif);
	if (firstNode.heuristic == InfiniteCost &&
		!AllRequirementsSatisfied(firstNode.requirements, staticRessources))
	{
		Debug::Error("GOAP RAS: aucun chemin trouve.");
		return {};
	}

	std::priority_queue<SearchNode, std::vector<SearchNode>, CompareSearchNode> openNodes;
	std::unordered_map<std::string, StateScore> bestScoreByState;
	std::uint64_t insertionOrder = 0;
	firstNode.insertionOrder = insertionOrder++;
	bestScoreByState[StateKey(firstNode.requirements)] =
		{ firstNode.cost, firstNode.reversePath.size() };
	openNodes.push(std::move(firstNode));

	std::size_t expandedNodes = 0;
	bool planLengthLimitReached = false;
	while (!openNodes.empty() && expandedNodes < MaxExpandedNodes)
	{
		SearchNode current = openNodes.top();
		openNodes.pop();

		const std::string currentKey = StateKey(current.requirements);
		const auto knownScore = bestScoreByState.find(currentKey);
		if (knownScore != bestScoreByState.end() &&
			(current.cost > knownScore->second.cost ||
				(current.cost == knownScore->second.cost &&
				 current.reversePath.size() > knownScore->second.actionCount)))
		{
			continue;
		}

		if (AllRequirementsSatisfied(current.requirements, staticRessources))
		{
			std::reverse(current.reversePath.begin(), current.reversePath.end());
			return current.reversePath;
		}

		++expandedNodes;
		if (current.reversePath.size() >= MaxPlanLength)
		{
			planLengthLimitReached = true;
			continue;
		}

		for (const ActionModel& model : actionModels)
		{
			if (!model.valid || model.action == nullptr || model.action == objectif ||
				!IsRelevant(model, current.requirements))
			{
				continue;
			}

			SearchNode next;
			next.requirements = current.requirements;
			if (!Regress(next.requirements, model))
			{
				continue;
			}
			const std::string nextKey = StateKey(next.requirements);
			if (nextKey == currentKey)
			{
				continue;
			}

			next.reversePath = current.reversePath;
			next.reversePath.push_back(model.action);
			next.cost = SaturatedAdd(
				current.cost,
				static_cast<std::uint64_t>(model.action->GetCost()));
			next.heuristic = EstimateRemainingCost(
				next.requirements, staticRessources, actionModels, objectif);
			if (next.heuristic == InfiniteCost &&
				!AllRequirementsSatisfied(next.requirements, staticRessources))
			{
				continue;
			}
			next.insertionOrder = insertionOrder++;

			const auto previousScore = bestScoreByState.find(nextKey);
			if (previousScore != bestScoreByState.end() &&
				(previousScore->second.cost < next.cost ||
					(previousScore->second.cost == next.cost &&
					 previousScore->second.actionCount <= next.reversePath.size())))
			{
				continue;
			}
			bestScoreByState[nextKey] = { next.cost, next.reversePath.size() };
			openNodes.push(std::move(next));
		}
	}

	if (expandedNodes >= MaxExpandedNodes)
	{
		Debug::Warn("GOAP RAS: limite de recherche atteinte (%d noeuds).",
			static_cast<int>(MaxExpandedNodes));
	}
	if (planLengthLimitReached)
	{
		Debug::Warn("GOAP RAS: longueur maximale atteinte (%d actions).",
			static_cast<int>(MaxPlanLength));
	}
	Debug::Error("GOAP RAS: aucun chemin trouve.");
	return {};
}

std::vector<GGoapBlocker> GoapRAS::FindPlanBlockers() const
{
	if (m_objectifs.empty() || m_objectifs.front() == nullptr)
	{
		Debug::Error("GOAP RAS diagnostic: aucun objectif a analyser.");
		return {};
	}

	const GAction* const objectif = m_objectifs.front();
	const WorldState staticRessources = m_ressource;
	std::vector<ActionModel> actionModels;
	actionModels.reserve(m_allActions.size());
	for (const GAction* action : m_allActions)
	{
		actionModels.push_back(BuildActionModel(action, staticRessources));
	}

	std::vector<ActionDiagnostic> diagnostics =
		BuildActionDiagnostics(objectif, actionModels);
	std::unordered_set<const GAction*> relevantActions;
	for (const ActionDiagnostic& diagnostic : diagnostics)
	{
		if (diagnostic.action != nullptr)
		{
			relevantActions.insert(diagnostic.action);
		}
	}

	std::queue<DiagnosticState> openStates;
	std::unordered_set<std::string> visitedStates;
	DiagnosticState firstState;
	firstState.state = staticRessources;
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
			UpdateActionDiagnostic(diagnostic, current.state, staticRessources);
		}
		if (!diagnostics.empty() && diagnostics.front().executable)
		{
			// The objective is reachable under the exact RAS model. Returning no
			// blocker is intentional even if Resolve() was not called first.
			return {};
		}

		if (current.depth >= MaxPlanLength)
		{
			planLengthLimitReached = true;
			continue;
		}

		for (const ActionModel& model : actionModels)
		{
			if (!model.valid || model.action == nullptr || model.action == objectif ||
				relevantActions.find(model.action) == relevantActions.end() ||
				!AllRequirementsSatisfied(model.preConditions, current.state))
			{
				continue;
			}

			DiagnosticState next;
			next.state = current.state;
			next.depth = current.depth + 1;
			if (!ApplyActionModel(model, next.state))
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
			"GOAP RAS diagnostic: limite de recherche atteinte (%d etats).",
			static_cast<int>(MaxExpandedNodes));
	}
	if (planLengthLimitReached)
	{
		Debug::Warn(
			"GOAP RAS diagnostic: longueur maximale atteinte (%d actions).",
			static_cast<int>(MaxPlanLength));
	}

	// Root causes are usually the deepest dependencies, so expose them first.
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

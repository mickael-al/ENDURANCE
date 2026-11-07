#include "GAction.hpp"
#include "GEffect.hpp"

GAction::GAction(const std::string& name, unsigned int cost)
	: m_name(name), m_cost(cost)
{
}

GAction::GAction(const GAction* const action)
	: m_name(action == nullptr ? std::string() : action->GetName()),
	  m_preConditions(action == nullptr ? std::vector<GPreCondition*>() : action->GetPreConditions()),
	  m_effects(action == nullptr ? std::vector<const GEffect*>() : action->GetEffects()),
	  m_cost(action == nullptr ? 1u : action->GetCost())
{
}

GAction::~GAction()
{
	m_preConditions.clear();
	m_effects.clear();
}

void GAction::SetName(const std::string& name)
{
	m_name = name;
}

const std::string& GAction::GetName() const
{
	return m_name;
}

void GAction::AddPreCondition(GPreCondition* preCondition)
{
	m_preConditions.push_back(preCondition);
}

const std::vector<GPreCondition*>& GAction::GetPreConditions() const
{
	return m_preConditions;
}

void GAction::AddEffect(const GEffect* effect)
{
	m_effects.push_back(effect);
}

const std::vector<const GEffect*>& GAction::GetEffects() const
{
	return m_effects;
}

unsigned int GAction::GetCost() const
{
	return m_cost;
}

void GAction::SetCost(unsigned int cost)
{
	m_cost = cost;
}

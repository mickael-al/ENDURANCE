#include "GPreCondition.hpp"
#include "GNode.hpp"

namespace
{
	float GetRessourceValue(
		const std::unordered_map<std::string, float>& state,
		const std::string& ressource)
	{
		const auto value = state.find(ressource);
		return value == state.end() ? 0.0f : value->second;
	}

	bool CompareValues(float value, Condition condition, float expected)
	{
		switch (condition)
		{
		case Condition::INF:
			return value < expected;
		case Condition::SUP:
			return value > expected;
		case Condition::INF_EQUALS:
			return value <= expected;
		case Condition::SUP_EQUALS:
			return value >= expected;
		default:
			return false;
		}
	}
}

GPreCondition::GPreCondition(const std::string ressource, Condition condition, float value)
	: m_ressource(ressource), m_condition(condition), m_value(value)
{
}

GPreCondition::GPreCondition(const GPreCondition* const preCondition)
	: m_ressource(preCondition == nullptr ? std::string() : preCondition->m_ressource),
	  m_condition(preCondition == nullptr ? Condition::SUP_EQUALS : preCondition->m_condition),
	  m_value(preCondition == nullptr ? 0.0f : preCondition->m_value)
{
}

GPreCondition::~GPreCondition() = default;

void GPreCondition::Check() const
{
}

const std::string& GPreCondition::GetRessource() const
{
	return m_ressource;
}

const Condition& GPreCondition::GetCondition() const
{
	return m_condition;
}

bool GPreCondition::CheckPreCondition(GNode* const node) const
{
	return node != nullptr && CheckPreCondition(node->ressource);
}

bool GPreCondition::CheckPreCondition(const std::unordered_map<std::string, float>& state) const
{
	Check();
	return CompareValues(GetRessourceValue(state, m_ressource), m_condition, m_value);
}

void GPreCondition::SetResolver(std::vector<const GAction*>& actions)
{
	m_resolver = actions;
}

const std::vector<const GAction*>& GPreCondition::GetResolver() const
{
	return m_resolver;
}

float GPreCondition::GetValue(GNode* const) const
{
	return m_value;
}

GPreconditionsRessource::GPreconditionsRessource(
	const std::string ressource,
	Condition condition,
	const std::string value)
	: GPreCondition(ressource, condition, 0.0f), m_valueRessource(value)
{
}

bool GPreconditionsRessource::CheckPreCondition(GNode* const node) const
{
	return node != nullptr && CheckPreCondition(node->ressource);
}

bool GPreconditionsRessource::CheckPreCondition(
	const std::unordered_map<std::string, float>& state) const
{
	Check();
	return CompareValues(
		GetRessourceValue(state, m_ressource),
		m_condition,
		GetRessourceValue(state, m_valueRessource));
}

float GPreconditionsRessource::GetValue(GNode* const node) const
{
	return node == nullptr ? 0.0f : GetRessourceValue(node->ressource, m_valueRessource);
}

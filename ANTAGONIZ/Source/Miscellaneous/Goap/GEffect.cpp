#include "GEffect.hpp"
#include <algorithm>
#include <cmath>

namespace
{
	float GetRessourceValue(
		const std::unordered_map<std::string, float>& state,
		const std::string& ressource)
	{
		const auto value = state.find(ressource);
		return value == state.end() ? 0.0f : value->second;
	}

	float ApplyModifier(float current, Modifier modifier, float value)
	{
		return current + (modifier == Modifier::ADD ? value : -value);
	}
}

GEffect::GEffect(const std::string& ressource, Modifier mod, float value)
	: m_ressource(ressource), m_modifier(mod), m_value(value)
{
}

GEffect::GEffect(const GEffect* const effect)
	: m_ressource(effect == nullptr ? std::string() : effect->m_ressource),
	  m_modifier(effect == nullptr ? Modifier::ADD : effect->m_modifier),
	  m_value(effect == nullptr ? 0.0f : effect->m_value)
{
}

GEffect::~GEffect() = default;

const std::string& GEffect::GetRessource() const
{
	return m_ressource;
}

const Modifier& GEffect::GetModifier() const
{
	return m_modifier;
}

bool GEffect::TryGetReverseDelta(
	const std::unordered_map<std::string, float>&,
	float& delta) const
{
	delta = ApplyModifier(0.0f, m_modifier, m_value);
	return std::isfinite(delta);
}

void GEffect::SimulateEffect(std::unordered_map<std::string, float>& state) const
{
	state[m_ressource] = ApplyModifier(GetRessourceValue(state, m_ressource), m_modifier, m_value);
}

void GEffect::ExecuteEffect(std::unordered_map<std::string, float>& state) const
{
	SimulateEffect(state);
	Execute();
}

void GEffect::Execute() const
{
}

GEffectClamp::GEffectClamp(
	const std::string& ressource,
	Modifier mod,
	float value,
	float min,
	float max)
	: GEffect(ressource, mod, value),
	  m_min(std::min(min, max)),
	  m_max(std::max(min, max))
{
}

bool GEffectClamp::TryGetReverseDelta(
	const std::unordered_map<std::string, float>&,
	float& delta) const
{
	delta = 0.0f;
	return false;
}

void GEffectClamp::SimulateEffect(std::unordered_map<std::string, float>& state) const
{
	const float result = ApplyModifier(GetRessourceValue(state, m_ressource), m_modifier, m_value);
	state[m_ressource] = std::clamp(result, m_min, m_max);
}

GEffectClampRessource::GEffectClampRessource(
	const std::string& ressource,
	Modifier mod,
	const std::string& value,
	const std::string& min,
	const std::string& max)
	: GEffect(ressource, mod, 0.0f),
	  m_minRessource(min),
	  m_maxRessource(max),
	  m_valueRessource(value)
{
}

bool GEffectClampRessource::TryGetReverseDelta(
	const std::unordered_map<std::string, float>&,
	float& delta) const
{
	delta = 0.0f;
	return false;
}

void GEffectClampRessource::SimulateEffect(std::unordered_map<std::string, float>& state) const
{
	const float value = GetRessourceValue(state, m_valueRessource);
	const float firstBound = GetRessourceValue(state, m_minRessource);
	const float secondBound = GetRessourceValue(state, m_maxRessource);
	const float minimum = std::min(firstBound, secondBound);
	const float maximum = std::max(firstBound, secondBound);
	const float result = ApplyModifier(GetRessourceValue(state, m_ressource), m_modifier, value);
	state[m_ressource] = std::clamp(result, minimum, maximum);
}

GEffectRessource::GEffectRessource(
	const std::string& ressource,
	Modifier mod,
	const std::string& value)
	: GEffect(ressource, mod, 0.0f), m_valueRessource(value)
{
}

bool GEffectRessource::TryGetReverseDelta(
	const std::unordered_map<std::string, float>& staticRessources,
	float& delta) const
{
	delta = ApplyModifier(
		0.0f,
		m_modifier,
		GetRessourceValue(staticRessources, m_valueRessource));
	return std::isfinite(delta);
}

void GEffectRessource::SimulateEffect(std::unordered_map<std::string, float>& state) const
{
	const float value = GetRessourceValue(state, m_valueRessource);
	state[m_ressource] = ApplyModifier(GetRessourceValue(state, m_ressource), m_modifier, value);
}

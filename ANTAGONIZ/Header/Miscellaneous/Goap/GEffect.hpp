#ifndef __G_GEffect__
#define __G_GEffect__

#include "EnumModifier.hpp"
#include "GNode.hpp"

#include <string>
#include <unordered_map>

class GEffect
{
public:
	GEffect(const std::string& ressource, Modifier mod, float value);
	GEffect(const GEffect* const effect);
	virtual ~GEffect();

	const std::string& GetRessource() const;
	const Modifier& GetModifier() const;

	// Returns the additive delta used by reverse planners. Resource-backed values
	// are read from the immutable snapshot supplied by the planner. Effects that
	// cannot be inverted (for example clamps) return false.
	virtual bool TryGetReverseDelta(
		const std::unordered_map<std::string, float>& staticRessources,
		float& delta) const;

	// Changes a planning state without running the gameplay callback.
	virtual void SimulateEffect(std::unordered_map<std::string, float>& state) const;

	// Applies the state change and then runs the gameplay callback.
	virtual void ExecuteEffect(GNode* const node) const
	{
		if (node == nullptr)
		{
			return;
		}
		SimulateEffect(node->ressource);
		Execute();
	}
	virtual void ExecuteEffect(std::unordered_map<std::string, float>& state) const;
	virtual void Execute() const;

protected:
	std::string m_ressource;
	Modifier m_modifier;
	float m_value;
};

class GEffectClamp : public GEffect
{
public:
	GEffectClamp(const std::string& ressource, Modifier mod, float value, float min, float max);
	bool TryGetReverseDelta(
		const std::unordered_map<std::string, float>& staticRessources,
		float& delta) const override;
	void SimulateEffect(std::unordered_map<std::string, float>& state) const override;

private:
	float m_min;
	float m_max;
};

class GEffectClampRessource : public GEffect
{
public:
	GEffectClampRessource(
		const std::string& ressource,
		Modifier mod,
		const std::string& value,
		const std::string& min,
		const std::string& max);
	bool TryGetReverseDelta(
		const std::unordered_map<std::string, float>& staticRessources,
		float& delta) const override;
	void SimulateEffect(std::unordered_map<std::string, float>& state) const override;

private:
	std::string m_minRessource;
	std::string m_maxRessource;
	std::string m_valueRessource;
};

class GEffectRessource : public GEffect
{
public:
	GEffectRessource(const std::string& ressource, Modifier mod, const std::string& value);
	bool TryGetReverseDelta(
		const std::unordered_map<std::string, float>& staticRessources,
		float& delta) const override;
	void SimulateEffect(std::unordered_map<std::string, float>& state) const override;

private:
	std::string m_valueRessource;
};

#endif //!__G_GEffect__

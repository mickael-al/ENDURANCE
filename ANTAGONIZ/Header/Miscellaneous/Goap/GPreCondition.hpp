#ifndef __G_PRE_CONDITION__
#define __G_PRE_CONDITION__

#include "EnumCondition.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class GAction;
class GNode;

class GPreCondition
{
public:
	GPreCondition(const std::string ressource, Condition condition, float value);
	GPreCondition(const GPreCondition* const preCondition);
	virtual ~GPreCondition();

	virtual void Check() const;
	const std::string& GetRessource() const;
	const Condition& GetCondition() const;
	virtual bool CheckPreCondition(GNode* const node) const;
	virtual bool CheckPreCondition(const std::unordered_map<std::string, float>& state) const;
	void SetResolver(std::vector<const GAction*>& actions);
	const std::vector<const GAction*>& GetResolver() const;
	virtual float GetValue(GNode* const node) const;

protected:
	std::string m_ressource;
	Condition m_condition;
	float m_value;
	std::vector<const GAction*> m_resolver;
};

class GPreconditionsRessource : public GPreCondition
{
public:
	GPreconditionsRessource(
		const std::string ressource,
		Condition condition,
		const std::string value);
	bool CheckPreCondition(GNode* const node) const override;
	bool CheckPreCondition(const std::unordered_map<std::string, float>& state) const override;
	float GetValue(GNode* const node) const override;

private:
	std::string m_valueRessource;
};

#endif //!__G_PRE_CONDITION__

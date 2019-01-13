#pragma once

#include "sysdefs.h"
#include <initializer_list>
#include <string>
#include <functional>
#include <utility>
#include <vector>
#include <unordered_map>

enum class ActionType
{
	main,
	monitor,
	reg,
	trace,
	memory,
	banks,
	xt,

	empty,
};

typedef std::function<void()> ActionFunctor;

class UIAction final
{
	std::vector<ActionFunctor> actions_{};
public:
	static UIAction empty;

	const ActionType type;
	const std::string name;

	u16 k1{}, k2{}, k3{}, k4{};

	UIAction(ActionType type, std::string name, const ActionFunctor& action) : type(type), name(std::move(name))
	{
		actions_.push_back(action);
	}

	auto invoke()
	{
		for (auto& item : actions_)
			item();
	}

	auto subscrible(const ActionFunctor& action)
	{
		actions_.push_back(action);
	}

	auto is_empty() const -> bool
	{
		return type != ActionType::empty && name != "empty";
	}
};

class ActionManager final
{
	static ActionManager * instance_;
	std::vector<UIAction> actions_{};
public:

	static auto get_instance() -> ActionManager*
	{
		if (instance_ == nullptr)
			instance_ = new ActionManager;

		return instance_;
	}

	static auto subscrible(ActionType type, const std::string& name, ActionFunctor action) -> void
	{
		auto item = find(type, name);

		if (item.is_empty())
			get_instance()->actions_.emplace_back(type, name, action);
		else
			item.subscrible(action);
	}

	static auto find(ActionType type, const std::string& name) -> UIAction&
	{
		for(auto& item : get_instance()->actions_)
		{
			if (item.type == type && item.name == name)
				return item;
		}

		return UIAction::empty;
	}

	static auto invoke(ActionType type, std::string name) -> void
	{
		auto item = find(type, name);
		
		if (!item.is_empty())
			item.invoke();
	}
};
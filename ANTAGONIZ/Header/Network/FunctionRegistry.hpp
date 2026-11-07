#ifndef __FUNCTION_REGISTRY__
#define __FUNCTION_REGISTRY__

#include <any>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"

class FunctionRegistry
{
public:
	using RawData = std::vector<std::uint8_t>;

	struct IFunctor
	{
		virtual ~IFunctor() = default;
		virtual void callFromBinary(const RawData& data) = 0;
	};

	template<typename... Args>
	struct Functor : IFunctor
	{
		std::function<void(Args...)> fn;

		explicit Functor(std::function<void(Args...)> function)
			: fn(std::move(function))
		{
		}

		void callFromBinary(const RawData& data) override
		{
			auto arguments = deserializeTuple<Args...>(data);
			std::apply(fn, arguments);
		}
	};

	template<typename... Args>
	void registerFunction(std::uint32_t name, std::function<void(Args...)> function)
	{
		functions[name] = std::make_unique<Functor<Args...>>(std::move(function));
	}

	template<typename... Args>
	RawData extract(std::uint32_t name, Args... args) const
	{
		if (functions.find(name) == functions.end())
		{
			throw std::runtime_error("Function not found: " + std::to_string(name));
		}
		return serializeTuple(std::make_tuple(args...));
	}

	void receive(std::uint32_t name, const RawData& data)
	{
		const auto function = functions.find(name);
		if (function == functions.end())
		{
			throw std::runtime_error("Function not found: " + std::to_string(name));
		}
		function->second->callFromBinary(data);
	}

private:
	template<typename T>
	static void serializeOne(RawData& buffer, const T& value)
	{
		static_assert(std::is_trivially_copyable_v<T>, "Network values must be trivially copyable");
		const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&value);
		buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
	}

	static void serializeOne(RawData& buffer, const std::string& value)
	{
		if (value.size() > (std::numeric_limits<std::uint32_t>::max)())
		{
			throw std::length_error("Network string is too large");
		}
		const std::uint32_t length = static_cast<std::uint32_t>(value.size());
		serializeOne(buffer, length);
		buffer.insert(buffer.end(), value.begin(), value.end());
	}

	static void serializeOne(RawData& buffer, const glm::vec3& value)
	{
		serializeOne(buffer, value.x);
		serializeOne(buffer, value.y);
		serializeOne(buffer, value.z);
	}

	static void serializeOne(RawData& buffer, const glm::quat& value)
	{
		serializeOne(buffer, value.x);
		serializeOne(buffer, value.y);
		serializeOne(buffer, value.z);
		serializeOne(buffer, value.w);
	}

	template<typename Tuple, std::size_t... I>
	static RawData serializeTupleImpl(const Tuple& values, std::index_sequence<I...>)
	{
		RawData buffer;
		(serializeOne(buffer, std::get<I>(values)), ...);
		return buffer;
	}

	template<typename... Args>
	static RawData serializeTuple(const std::tuple<Args...>& values)
	{
		return serializeTupleImpl(values, std::index_sequence_for<Args...>{});
	}

	template<typename T>
	static T deserializeOne(const std::uint8_t*& pointer, std::size_t& remaining)
	{
		if constexpr (std::is_same_v<T, std::string>)
		{
			const std::uint32_t length = deserializeOne<std::uint32_t>(pointer, remaining);
			if (remaining < length)
			{
				throw std::runtime_error("Truncated network string");
			}
			std::string value(reinterpret_cast<const char*>(pointer), length);
			pointer += length;
			remaining -= length;
			return value;
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			if (remaining < sizeof(bool))
			{
				throw std::runtime_error("Truncated network boolean");
			}
			// Arbitrary bytes copied into bool can cause undefined behavior
			// before an RPC handler gets a chance to validate its arguments.
			// Compare bytes against the existing wire representation instead.
			const bool falseValue = false;
			const bool trueValue = true;
			bool value;
			if (std::memcmp(pointer, &falseValue, sizeof(bool)) == 0)
			{
				value = false;
			}
			else if (std::memcmp(pointer, &trueValue, sizeof(bool)) == 0)
			{
				value = true;
			}
			else
			{
				throw std::runtime_error("Invalid network boolean");
			}
			pointer += sizeof(bool);
			remaining -= sizeof(bool);
			return value;
		}
		else if constexpr (std::is_same_v<T, glm::vec3>)
		{
			glm::vec3 value;
			value.x = deserializeOne<float>(pointer, remaining);
			value.y = deserializeOne<float>(pointer, remaining);
			value.z = deserializeOne<float>(pointer, remaining);
			return value;
		}
		else if constexpr (std::is_same_v<T, glm::quat>)
		{
			glm::quat value;
			value.x = deserializeOne<float>(pointer, remaining);
			value.y = deserializeOne<float>(pointer, remaining);
			value.z = deserializeOne<float>(pointer, remaining);
			value.w = deserializeOne<float>(pointer, remaining);
			return value;
		}
		else
		{
			static_assert(std::is_trivially_copyable_v<T>, "Network values must be trivially copyable");
			if (remaining < sizeof(T))
			{
				throw std::runtime_error("Truncated network argument");
			}
			T value{};
			std::memcpy(&value, pointer, sizeof(T));
			pointer += sizeof(T);
			remaining -= sizeof(T);
			return value;
		}
	}

	template<typename... Args, std::size_t... I>
	static std::tuple<Args...> deserializeTupleImpl(
		const std::uint8_t*& pointer,
		std::size_t& remaining,
		std::index_sequence<I...>)
	{
		std::tuple<Args...> result;
		((std::get<I>(result) = deserializeOne<Args>(pointer, remaining)), ...);
		return result;
	}

	template<typename... Args>
	static std::tuple<Args...> deserializeTuple(const RawData& buffer)
	{
		const std::uint8_t* pointer = buffer.data();
		std::size_t remaining = buffer.size();
		std::tuple<Args...> result = deserializeTupleImpl<Args...>(
			pointer,
			remaining,
			std::index_sequence_for<Args...>{});
		if (remaining != 0)
		{
			throw std::runtime_error("Unexpected trailing network data");
		}
		return result;
	}

public:
	std::map<std::uint32_t, std::unique_ptr<IFunctor>> functions;
};

#endif //!__FUNCTION_REGISTRY__

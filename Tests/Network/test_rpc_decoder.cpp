#include "FunctionRegistry.hpp"

#include <cassert>
#include <iostream>
#include <limits>

using Bytes = FunctionRegistry::RawData;

template<typename F>
void rejects(F&& operation)
{
    bool rejected = false;
    try { operation(); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
}

int main()
{
    FunctionRegistry registry;
    int booleanCalls = 0;
    bool decoded = false;
    registry.registerFunction<bool>(1, std::function<void(bool)>([&](bool value)
    {
        ++booleanCalls;
        decoded = value;
    }));
    const Bytes falseBytes = registry.extract(1, false);
    const Bytes trueBytes = registry.extract(1, true);
    assert(falseBytes.size() == sizeof(bool) && trueBytes.size() == sizeof(bool));
    registry.receive(1, falseBytes); assert(booleanCalls == 1 && !decoded);
    registry.receive(1, trueBytes); assert(booleanCalls == 2 && decoded);

    // In particular byte 2 used to be memcpy'd into bool and read, triggering
    // UBSan before any game-side argument validation. Reject every such byte.
    for (std::size_t position = 0; position < sizeof(bool); ++position)
    {
        for (int value = 0; value <= 255; ++value)
        {
            Bytes invalid = falseBytes;
            invalid[position] = static_cast<std::uint8_t>(value);
            if (invalid == falseBytes || invalid == trueBytes) continue;
            rejects([&] { registry.receive(1, invalid); });
            assert(booleanCalls == 2);
        }
    }
    rejects([&] { registry.receive(1, {}); });
    Bytes trailing = trueBytes;
    trailing.push_back(0);
    rejects([&] { registry.receive(1, trailing); });
    assert(booleanCalls == 2);
    registry.receive(1, falseBytes); assert(booleanCalls == 3 && !decoded);
    std::cout << "PASS valid bool wire compatibility, all invalid byte representations, no callback before validation and recovery\n";

    int tupleCalls = 0;
    const std::string binary("a\0b", 3);
    registry.registerFunction<int, std::string, glm::vec3, glm::quat, bool>(2,
        std::function<void(int, std::string, glm::vec3, glm::quat, bool)>(
        [&](int number, std::string text, glm::vec3 position, glm::quat rotation, bool active)
        {
            ++tupleCalls;
            assert(number == -17 && text == binary && active);
            assert(position.x == 1.0f && position.y == -2.0f && position.z == 3.0f);
            assert(rotation.x == 2.0f && rotation.y == 3.0f && rotation.z == 4.0f && rotation.w == 1.0f);
        }));
    const Bytes tuple = registry.extract(2, -17, binary, glm::vec3(1, -2, 3), glm::quat(1, 2, 3, 4), true);
    registry.receive(2, tuple); assert(tupleCalls == 1);
    for (std::size_t length = 0; length < tuple.size(); ++length)
    {
        const Bytes truncated(tuple.begin(), tuple.begin() + length);
        rejects([&] { registry.receive(2, truncated); });
        assert(tupleCalls == 1);
    }
    Bytes badBoolean = tuple;
    badBoolean.back() = 2;
    rejects([&] { registry.receive(2, badBoolean); });
    assert(tupleCalls == 1);
    Bytes badLength = tuple;
    const auto impossibleLength = (std::numeric_limits<std::uint32_t>::max)();
    std::memcpy(badLength.data() + sizeof(int), &impossibleLength, sizeof(impossibleLength));
    rejects([&] { registry.receive(2, badLength); });
    Bytes extra = tuple;
    extra.push_back(0);
    rejects([&] { registry.receive(2, extra); });
    assert(tupleCalls == 1);
    registry.receive(2, tuple); assert(tupleCalls == 2);
    std::cout << "PASS mixed RPC tuple order, binary strings, vectors/quaternions, every truncation and oversized string rejection\n";

    int emptyCalls = 0;
    registry.registerFunction<>(3, std::function<void()>([&] { ++emptyCalls; }));
    registry.receive(3, {}); assert(emptyCalls == 1);
    rejects([&] { registry.receive(3, Bytes{0}); });
    rejects([&] { registry.receive(999, {}); });
    assert(emptyCalls == 1);
    registry.receive(3, {}); assert(emptyCalls == 2);
    std::cout << "PASS empty RPC payloads, unknown function and trailing-data rejection\n";
}

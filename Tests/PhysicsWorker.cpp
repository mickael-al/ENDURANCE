#include "Physics/PhysicsWorkerLoop.hpp"
#include "btBulletDynamicsCommon.h"

#include <atomic>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct FunctionCommand final : Ge::Command
    {
        explicit FunctionCommand(std::function<void()> function) : callback(std::move(function)) {}
        void execute() override { callback(); }
        std::function<void()> callback;
    };

    void processCommands(Ge::CommandQueue& queue)
    {
        for (unsigned i = 0; i < 64; ++i)
        {
            std::unique_ptr<Ge::Command> command(queue.tryPop());
            if (!command) return;
            command->execute();
        }
    }

    void clockAndBullet()
    {
        using Tick = Ge::PhysicsTickClock;
        const Tick::Clock::time_point start{};
        Tick clock(start);
        require(clock.consume(start) == 0, "A new worker must not run a zero-duration step");
        require(clock.consume(clock.deadline() - Tick::Clock::duration(1)) == 0,
            "Physics ran before its deadline");

        btDefaultCollisionConfiguration configuration;
        btCollisionDispatcher dispatcher(&configuration);
        btDbvtBroadphase broadphase;
        btSequentialImpulseConstraintSolver solver;
        btDiscreteDynamicsWorld world(&dispatcher, &broadphase, &solver, &configuration);
        world.setGravity(btVector3(0, 0, 0));
        unsigned internalSteps = 0;
        world.setWorldUserInfo(&internalSteps);
        world.setInternalTickCallback([](btDynamicsWorld* dynamics, btScalar dt) {
            require(dt == Tick::StepSeconds, "Bullet received a rounded or variable tick");
            ++*static_cast<unsigned*>(dynamics->getWorldUserInfo());
        });
        // setInternalTickCallback also sets the user pointer, so restore it.
        world.setWorldUserInfo(&internalSteps);
        for (unsigned i = 1; i <= 60; ++i)
        {
            const auto due = clock.consume(start + Tick::period() * i);
            require(due == 1, "Steady deadlines skipped or doubled a tick");
            require(world.stepSimulation(Tick::StepSeconds, 4, Tick::StepSeconds) == 1,
                "An exact 1/60 tick did not produce one Bullet step");
        }
        require(internalSteps == 60, "Sixty scheduled ticks must produce sixty Bullet steps");

        const auto stalledAt = start + std::chrono::hours(1) + Tick::period() / 2;
        require(clock.consume(stalledAt) == Tick::MaximumCatchUpSteps,
            "Catch-up did not stop at four steps");
        require(clock.deadline() > stalledAt && clock.deadline() - stalledAt <= Tick::period(),
            "Catch-up lost the fractional phase or retained an old deadline");
        require(clock.consume(stalledAt) == 0, "A long pause left an immediate catch-up loop");

        // An inter-tick command must leave its force in Bullet until a real
        // simulation step. The old Update(0) path cleared it immediately.
        btSphereShape shape(1);
        btVector3 inertia;
        shape.calculateLocalInertia(1, inertia);
        btDefaultMotionState motion;
        btRigidBody::btRigidBodyConstructionInfo info(1, &motion, &shape, inertia);
        btRigidBody body(info);
        world.addRigidBody(&body);
        Ge::CommandQueue queue;
        queue.push(new FunctionCommand([&] { body.applyCentralForce(btVector3(60, 0, 0)); }));
        processCommands(queue);
        require(body.getTotalForce().x() == 60, "Command processing cleared the pending force");
        world.stepSimulation(Tick::StepSeconds, 4, Tick::StepSeconds);
        require(std::abs(body.getLinearVelocity().x() - 1.0f) < 0.0001f,
            "The force command did not reach the next real tick");
        world.removeRigidBody(&body);
    }

    void commandAndStopWake()
    {
        using Clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;
        Ge::CommandQueue queue;
        std::atomic<bool> running{true};
        std::promise<void> entered, processed;
        auto processing = processed.get_future();
        std::thread worker([&] {
            entered.set_value();
            queue.waitUntil(Clock::now() + 2s, running);
            processCommands(queue);
        });
        entered.get_future().wait();
        queue.push(new FunctionCommand([&] { processed.set_value(); }));
        const bool immediate = processing.wait_for(250ms) == std::future_status::ready;
        running.store(false, std::memory_order_release);
        queue.wake();
        worker.join();
        require(immediate, "A command waited for the timer instead of waking the queue");

        running = true;
        std::promise<void> waiting, stopped;
        auto stoppedFuture = stopped.get_future();
        std::thread sleeper([&] {
            waiting.set_value();
            queue.waitUntil(Clock::now() + 2s, running);
            stopped.set_value();
        });
        waiting.get_future().wait();
        running.store(false, std::memory_order_release);
        queue.wake();
        const bool promptStop = stoppedFuture.wait_for(250ms) == std::future_status::ready;
        sleeper.join();
        require(promptStop, "Stopping an idle worker did not interrupt its wait");
    }

    void realWorkerLoop()
    {
        using namespace std::chrono_literals;
        Ge::CommandQueue queue;
        std::atomic<bool> running{true};
        std::atomic<unsigned> pumps{0}, steps{0};
        std::atomic<bool> correctStep{true};
        std::promise<void> firstTick;
        auto started = firstTick.get_future();
        std::thread worker([&] {
            Ge::runPhysicsWorkerLoop(queue, running,
                [&] { ++pumps; processCommands(queue); },
                [&](float dt) {
                    if (dt != Ge::PhysicsTickClock::StepSeconds) correctStep = false;
                    if (++steps == 1) firstTick.set_value();
                });
        });
        const bool ticking = started.wait_for(500ms) == std::future_status::ready;
        std::this_thread::sleep_for(100ms);
        std::promise<void> commandResult;
        auto result = commandResult.get_future();
        queue.push(new FunctionCommand([&] { commandResult.set_value(); }));
        const bool commandCompleted = result.wait_for(250ms) == std::future_status::ready;
        running.store(false, std::memory_order_release);
        queue.wake();
        worker.join();
        require(ticking && commandCompleted, "Production worker failed to service ticks or commands");
        require(correctStep, "Production worker submitted an invalid physics dt");
        // One explicit command can add one wake between timed ticks. A busy
        // loop would execute many more command pumps while no tick is due.
        require(pumps <= steps + 1, "Idle worker continued pumping without a command or a due tick");
        std::cout << "idle worker: " << pumps << " wakes / " << steps << " fixed steps\n";
    }
}

int main()
{
    try
    {
        clockAndBullet();
        commandAndStopWake();
        realWorkerLoop();
        std::cout << "PASS physics scheduling: 60 exact Bullet steps, bounded catch-up, force preservation, command and stop wake, idle sleep\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

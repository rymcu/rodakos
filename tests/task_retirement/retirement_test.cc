#include "task_retirement_host.h"
#include "phone_os/task-retirement.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
using rodakos::TaskRetirementOwner;
using rodakos::TaskRetirementTicket;
using retirement_host::Gate;
[[noreturn]] void Fail(const char* marker) {
    std::fprintf(stderr, "RETIREMENT_TEST_ASSERT: %s\n", marker);
    std::fflush(stderr);
    std::abort();
}
void Check(bool value, const char* marker) { if (!value) Fail(marker); }
template<class T> void Await(T condition, const char* marker) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) Fail(marker);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct Body {
    std::atomic<unsigned> calls{0};
    std::atomic<bool> destroyed{false};
    std::atomic<bool> create_returned{false};
    Gate* body_gate = nullptr;
    Gate* destructor_gate = nullptr;
    TaskRetirementTicket* self = nullptr;
    BaseType_t core = tskNO_AFFINITY;
};
Body* observed = nullptr;
Gate* external_wait = nullptr;
void RunBody(void* argument) {
    auto& body = *static_cast<Body*>(argument);
    Check(body.create_returned, "body must wait for handle publication");
    struct Finalizer {
        Body& body;
        ~Finalizer() {
            if (body.destructor_gate) body.destructor_gate->Enter();
            body.destroyed = true;
        }
    } finalizer{body};
    ++body.calls;
    if (body.self) {
        Check(body.self->IsCurrentTask(), "self ticket identifies exact worker");
        body.self->Join();
    }
    if (body.body_gate) body.body_gate->Enter();
}
TaskRetirementTicket Start(TaskRetirementOwner& owner, Body& body) {
    auto ticket = rodakos::ReserveTaskRetirement(owner, RunBody, &body);
    Check(bool(ticket), "admission available");
    TaskHandle_t handle = nullptr;
    Check(xTaskCreatePinnedToCoreWithCaps(rodakos::TaskRetirementEntry, "retirement_test", 4096,
          rodakos::TaskRetirementContext(ticket), 3, &handle, body.core, 6) == pdPASS,
          "business task created");
    body.create_returned = true;
    rodakos::PublishTaskRetirement(ticket, handle);
    return ticket;
}
void BeforeCreateReturns(TaskHandle_t) {
    Check(observed && observed->calls == 0, "body must wait for handle publication");
}
void BeforeSuspend(TaskHandle_t) {
    Check(observed && observed->destroyed, "complete body destruction precedes retirement claim");
}
void ExternalDelay(TickType_t) {
    if (!retirement_host::IsWorkerTask() && external_wait) external_wait->Enter();
}
void CheckReclaimed(size_t count = 1) {
    retirement_host::JoinTasks();
    const auto state = retirement_host::Snapshot();
    Check(state.task_deletes == count && state.live_tasks == 0 && state.live_task_buffers == 0,
          "each task stack and TCB reclaimed exactly once");
    Check(state.cleanup_create_attempts == 0, "exit never creates cleanup task");
}
void Publication() {
    TaskRetirementOwner owner;
    Body body;
    observed = &body;
    retirement_host::SetBeforeCreateReturnsHook(BeforeCreateReturns);
    auto ticket = Start(owner, body);
    ticket.Join();
    Check(body.calls == 1 && body.destroyed, "body executes once after publish");
    CheckReclaimed();
}
void Destructor() {
    TaskRetirementOwner owner;
    Gate destruction, waiter;
    Body body;
    body.destructor_gate = &destruction;
    observed = &body;
    retirement_host::SetBeforeExternalSuspendHook(BeforeSuspend);
    auto ticket = Start(owner, body);
    Check(destruction.Wait(), "body destructor reached hold");
    external_wait = &waiter;
    retirement_host::SetDelayHook(ExternalDelay);
    std::atomic<bool> joined{false};
    std::thread joiner([&] { ticket.Join(); joined = true; });
    Check(waiter.Wait(), "join waits for full body return");
    rodakos::PumpTaskRetirements();
    Check(!joined && retirement_host::Snapshot().external_suspends == 0,
          "no retirement claim while body destructor held");
    retirement_host::SetDelayHook(nullptr);
    destruction.Release();
    waiter.Release();
    joiner.join();
    CheckReclaimed();
}
void Concurrent() {
    TaskRetirementOwner owner;
    Gate core, second_wait;
    Body body;
    retirement_host::HoldCoreAfterSuspend(&core);
    auto ticket = Start(owner, body);
    auto second = ticket;
    Check(core.Wait(), "finished worker reaches final suspend");
    const auto allocations = retirement_host::Snapshot().allocation_calls;
    std::thread first([&] { ticket.Join(); });
    Await([] { return retirement_host::Snapshot().external_suspends != 0; }, "first reaper claims");
    external_wait = &second_wait;
    retirement_host::SetDelayHook(ExternalDelay);
    std::thread other([&] { second.Join(); });
    Check(second_wait.Wait(), "second join observes claimed generation");
    std::thread pump([] { rodakos::PumpTaskRetirements(); });
    // At least two cross-core polling rounds establish that the winning reaper
    // is inside real IDF convergence while the other paths see its claim.
    Await([] { return retirement_host::Snapshot().yields >= 2; }, "IDF waits for old core");
    Check(retirement_host::Snapshot().task_deletes == 0, "no free before cross-core convergence");
    retirement_host::SetDelayHook(nullptr);
    second_wait.Release();
    core.Release();
    first.join(); other.join(); pump.join();
    Check(retirement_host::Snapshot().external_suspends == 1, "single external retirement claim");
    Check(retirement_host::Snapshot().allocation_calls == allocations, "exit infrastructure allocates nothing");
    CheckReclaimed();
}
void Autonomous() {
    TaskRetirementOwner owner;
    Body body;
    auto ticket = Start(owner, body);
    ticket.Reset();
    Await([&] { return body.destroyed.load(); }, "autonomous body returned");
    Await([] {
        rodakos::PumpTaskRetirements();
        return retirement_host::Snapshot().live_tasks == 0;
    }, "autonomous worker reclaimed by permanent pump");
    CheckReclaimed();
}
void Drain() {
    TaskRetirementOwner owner;
    Gate business, waiter;
    Body body;
    body.body_gate = &business;
    auto ticket = Start(owner, body);
    ticket.Reset();
    Check(business.Wait(), "business worker reached hold");
    owner.Close();
    Check(!rodakos::ReserveTaskRetirement(owner, RunBody, &body), "closed owner rejects new generation");
    external_wait = &waiter;
    retirement_host::SetDelayHook(ExternalDelay);
    std::thread drain([&] { owner.Drain(); });
    Check(waiter.Wait(), "owner drain waits retained historical generation");
    Check(retirement_host::Snapshot().task_deletes == 0, "owner not destroyed while worker accesses business state");
    retirement_host::SetDelayHook(nullptr);
    business.Release(); waiter.Release(); drain.join();
    CheckReclaimed();
}
void Cancel() {
    TaskRetirementOwner owner;
    Body body;
    retirement_host::SetCreationAllowed(false);
    auto ticket = rodakos::ReserveTaskRetirement(owner, RunBody, &body);
    TaskHandle_t handle = nullptr;
    Check(xTaskCreateWithCaps(rodakos::TaskRetirementEntry, "failed", 4096,
          rodakos::TaskRetirementContext(ticket), 3, &handle, 6) == pdFAIL,
          "task creation rejected");
    rodakos::CancelTaskRetirement(ticket);
    ticket.Join(); ticket.Reset();
    Check(retirement_host::Snapshot().live_task_buffers == 0, "failed create releases both original buffers");
    retirement_host::SetCreationAllowed(true);
    auto retry = Start(owner, body);
    retry.Join();
    CheckReclaimed();
}
void Capacity() {
    TaskRetirementOwner owner;
    Body body;
    std::array<TaskRetirementTicket, rodakos::kTaskRetirementSlots> tickets;
    for (auto& ticket : tickets) {
        ticket = rodakos::ReserveTaskRetirement(owner, RunBody, &body);
        Check(bool(ticket), "all bounded slots admit");
    }
    Check(!rodakos::ReserveTaskRetirement(owner, RunBody, &body), "full registry rejects before task create");
    auto retained = tickets[0];
    for (auto& ticket : tickets) { rodakos::CancelTaskRetirement(ticket); ticket.Reset(); }
    for (size_t i = 0; i < tickets.size() - 1; ++i) {
        tickets[i] = rodakos::ReserveTaskRetirement(owner, RunBody, &body);
        Check(bool(tickets[i]), "released slots are reusable");
    }
    Check(!rodakos::ReserveTaskRetirement(owner, RunBody, &body), "retained ticket prevents ABA reuse");
    retained.Join(); retained.Reset();
    tickets.back() = rodakos::ReserveTaskRetirement(owner, RunBody, &body);
    Check(bool(tickets.back()), "last reference permits reuse");
    for (auto& ticket : tickets) { rodakos::CancelTaskRetirement(ticket); ticket.Reset(); }
    CheckReclaimed(0);
}
void Reference() {
    TaskRetirementOwner owner;
    Body old_body, new_body;
    Gate new_gate;
    auto old = Start(owner, old_body);
    auto retained = old;
    old.Join(); old.Reset();
    new_body.body_gate = &new_gate;
    auto next = Start(owner, new_body);
    Check(new_gate.Wait(), "replacement generation active");
    retained.Join();
    Check(retirement_host::Snapshot().task_deletes == 1, "old ticket never waits or deletes replacement");
    new_gate.Release(); next.Join();
    retained.Reset();
    CheckReclaimed(2);
}
void Core() {
    TaskRetirementOwner owner;
    Gate core;
    Body body;
    body.core = 1;
    retirement_host::HoldCoreAfterSuspend(&core);
    auto ticket = Start(owner, body);
    Check(core.Wait(), "CPU1 worker reaches final suspend");
    std::thread joiner([&] { ticket.Join(); });
    Await([] { return retirement_host::Snapshot().yields >= 2; }, "IDF scans both cores before reclaim");
    const auto held = retirement_host::Snapshot();
    Check(held.cross_core_queries >= 4 && held.task_deletes == 0 && held.live_task_buffers == 2,
          "CPU1 current task retains both WithCaps buffers");
    core.Release(); joiner.join();
    CheckReclaimed();
}
void SelfJoin() {
    TaskRetirementOwner owner;
    Body body;
    retirement_host::SetAutoStart(false);
    auto ticket = Start(owner, body);
    body.self = &ticket;
    retirement_host::RunTasks();
    ticket.Join();
    CheckReclaimed();
}
void PoolFailure() {
    TaskRetirementOwner owner;
    Body body;
    retirement_host::FailNextAllocation();
    Check(!rodakos::ReserveTaskRetirement(owner, RunBody, &body), "pool OOM rejects admission");
    Check(retirement_host::Snapshot().tasks_created == 0, "no task before retirement resources reserved");
    auto ticket = Start(owner, body);
    ticket.Join();
    const auto snapshot = retirement_host::Snapshot();
    Check(snapshot.pool_allocations == 1 && snapshot.pool_caps == 6, "bounded pool explicitly uses PSRAM and 8BIT");
    CheckReclaimed();
}
void LegacyEntry(void*) {
    struct Local {
        ~Local() { std::fputs("UNEXPECTED_LEGACY_LOCAL_DESTRUCTOR\n", stderr); }
    } local;
    vTaskDeleteWithCaps(nullptr);
    Fail("legacy WithCaps self delete unexpectedly returned");
}
void Legacy() {
    TaskHandle_t handle = nullptr;
    Check(xTaskCreateWithCaps(LegacyEntry, "legacy_body", 4096, nullptr, 3, &handle, 6) == pdPASS,
          "legacy business creation succeeds");
    // SIGABRT from the real IDF self-delete branch must end this process.
    std::this_thread::sleep_for(std::chrono::seconds(4));
    Fail("legacy did not reach expected abort");
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    retirement_host::Reset();
    const char* scenario = argv[1];
    if (!std::strcmp(scenario, "publication")) Publication();
    else if (!std::strcmp(scenario, "destructor")) Destructor();
    else if (!std::strcmp(scenario, "concurrent")) Concurrent();
    else if (!std::strcmp(scenario, "autonomous")) Autonomous();
    else if (!std::strcmp(scenario, "drain")) Drain();
    else if (!std::strcmp(scenario, "cancel")) Cancel();
    else if (!std::strcmp(scenario, "capacity")) Capacity();
    else if (!std::strcmp(scenario, "reference")) Reference();
    else if (!std::strcmp(scenario, "core")) Core();
    else if (!std::strcmp(scenario, "self_join")) SelfJoin();
    else if (!std::strcmp(scenario, "pool_failure")) PoolFailure();
    else if (!std::strcmp(scenario, "legacy_self_delete")) Legacy();
    else return 2;
    std::printf("PASS %s\n", scenario);
}

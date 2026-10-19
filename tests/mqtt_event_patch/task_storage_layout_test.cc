// 在未修改的 ESP-IDF 6.0.2 TLSF 堆上重放 MQTT 客户端替换。
// 分配大小取自固件 heap trace 中一个 TLS 客户端的内部分配，向上取整到 8 字节：
// IDF 的 TLSF 按 32 位目标设计，8 字节请求能让它在 64 位主机上保持头部对齐。
// 堆的形状与设备开机后一致：客户端之后有一块大空闲区，被之后创建的常驻对象隔开。
#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>

extern "C" {
#include "multi_heap.h"
}

namespace {

constexpr std::size_t kClientStruct = 320;
constexpr std::size_t kStack = 6144;
constexpr std::size_t kTcb = 104;
constexpr std::size_t kLargeRegion = 11776;
const std::vector<std::size_t> kClientObjects = {
    88, 104, 88, 144, 88, 160, 224, 144, 72, 48, 40, 32, 32, 32, 32, 24, 24, 24, 24, 24};

enum class Order { kUpstream, kReservedFirst };

struct Client {
    void* head = nullptr;
    void* stack = nullptr;
    void* tcb = nullptr;
    std::vector<void*> objects;
};

struct Heap {
    alignas(16) unsigned char memory[32 * 1024];
    multi_heap_handle_t handle = multi_heap_register(memory, sizeof(memory));

    void* Alloc(std::size_t bytes) {
        void* pointer = multi_heap_malloc(handle, bytes);
        assert(pointer != nullptr);
        return pointer;
    }
    void Free(void* pointer) { multi_heap_free(handle, pointer); }
    std::size_t Largest() {
        multi_heap_info_t info{};
        multi_heap_get_info(handle, &info);
        return info.largest_free_block;
    }
};

// 上游 init 先分配客户端对象，start 再创建栈和 TCB；overlay 在客户端结构体之后立即预留栈和 TCB。
Client Create(Heap& heap, Order order) {
    Client client;
    client.head = heap.Alloc(kClientStruct);
    if (order == Order::kReservedFirst) {
        client.stack = heap.Alloc(kStack);
        client.tcb = heap.Alloc(kTcb);
    }
    for (std::size_t bytes : kClientObjects) client.objects.push_back(heap.Alloc(bytes));
    if (order == Order::kUpstream) {
        client.stack = heap.Alloc(kStack);
        client.tcb = heap.Alloc(kTcb);
    }
    return client;
}

Client Replace(Heap& heap, const Client& retired, Order order) {
    for (void* object : retired.objects) heap.Free(object);
    heap.Free(retired.head);
    if (order == Order::kUpstream) {
        // 自删除任务的 TCB 和栈由 idle 任务释放，而 idle 只在刷新任务创建完替换客户端后才运行。
        Client replacement = Create(heap, order);
        heap.Free(retired.tcb);
        heap.Free(retired.stack);
        return replacement;
    }
    // destroy 删除停住的任务，并在返回前释放其存储。
    heap.Free(retired.tcb);
    heap.Free(retired.stack);
    return Create(heap, order);
}

struct Result {
    std::size_t initial = 0;
    std::size_t smallest = 0;
    std::size_t final = 0;
    bool stack_reused = true;
};

Result Run(Order order, int replacements) {
    Heap heap;
    for (int index = 0; index < 8; ++index) heap.Alloc(48);
    Client client = Create(heap, order);
    heap.Alloc(88);  // 客户端之后创建的常驻对象，例如 WiFi 信号量。
    heap.Alloc((heap.Largest() - kLargeRegion) & ~std::size_t{7});
    Result result;
    result.initial = heap.Largest();
    result.smallest = result.initial;
    void* const first_stack = client.stack;
    for (int round = 0; round < replacements; ++round) {
        client = Replace(heap, client, order);
        result.stack_reused = result.stack_reused && client.stack == first_stack;
        if (heap.Largest() < result.smallest) result.smallest = heap.Largest();
    }
    result.final = heap.Largest();
    return result;
}

}  // namespace

int main() {
    const Result upstream = Run(Order::kUpstream, 6);
    const Result reserved = Run(Order::kReservedFirst, 6);
    std::cout << "largest free block initial/min/final: upstream=" << upstream.initial << "/"
              << upstream.smallest << "/" << upstream.final << " reserved=" << reserved.initial
              << "/" << reserved.smallest << "/" << reserved.final << "\n";
    assert(upstream.initial >= kLargeRegion && reserved.initial >= kLargeRegion);
    // 负对照：延迟释放让栈换位置，并切开更多的堆。
    assert(!upstream.stack_reused);
    // 预留的栈每次都回到旧位置，反复替换只占固定的一块，不会越切越碎。
    assert(reserved.stack_reused);
    assert(reserved.smallest == reserved.initial && reserved.final == reserved.initial);
    // 上游顺序让最大块至少少掉半个任务栈。
    assert(upstream.smallest + kStack / 2 <= upstream.initial);
    std::cout << "MQTT task storage layout scenario passed (including upstream negative control)\n";
}

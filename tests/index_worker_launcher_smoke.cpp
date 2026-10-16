#include <such/runtime/IndexWorkerLauncher.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main() {
    std::string error;
    if (!such::runtime::launch_index_worker(
            {such::runtime::IndexWorkerOperation::Reindex, {}}, &error)) {
        std::fprintf(stderr, "detached index worker launch failed: %s\n", error.c_str());
        return 1;
    }

    such::runtime::IndexWorkerTicket ticket{};
    if (!such::runtime::launch_index_worker_tracked(
            {such::runtime::IndexWorkerOperation::Reindex, {}}, &ticket, &error)) {
        std::fprintf(stderr, "tracked index worker launch failed: %s\n", error.c_str());
        return 2;
    }

    for (int poll = 0; poll < 200; ++poll) {
        int exit_code = 0;
        const auto state = such::runtime::poll_index_worker(&ticket, &exit_code);
        if (state == such::runtime::IndexWorkerPoll::Succeeded) return 0;
        if (state == such::runtime::IndexWorkerPoll::Failed) {
            std::fprintf(stderr, "tracked index worker failed with exit %d\n", exit_code);
            return 3;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    such::runtime::close_index_worker_ticket(&ticket);
    std::fprintf(stderr, "tracked index worker did not finish in time\n");
    return 4;
}

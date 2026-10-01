#include "ISOTP.hpp"
#include "ISOTPFD.hpp"
#include <cstdlib>
#include <iostream>
#include <new>

// Keep fault injection out of the XCTest process and production allocators.
static bool rejectReceiveBuffer = false;
static unsigned rejectedAllocations = 0;
void* operator new(std::size_t size) {
    if (rejectReceiveBuffer && size == CANyonero::ISOTP::maximumTransferSize) {
        ++rejectedAllocations;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size ? size : 1)) { return memory; }
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

using namespace CANyonero;
using namespace CANyonero::ISOTP;

template <class T>
bool check(T sender, T receiver, bool defensive) {
    using Action = typename T::Action;
    Bytes payload(maximumTransferSize, 0xA5);
    auto first = sender.writePDU(payload);
    rejectedAllocations = 0;
    rejectReceiveBuffer = true;
    try {
        auto failure = receiver.didReceiveFrame(first.frames.front().bytes);
        rejectReceiveBuffer = false;
        const auto expected = defensive ? Action::Type::waitForMore : Action::Type::protocolViolation;
        if (!rejectedAllocations || failure.type != expected || receiver.machineState() != T::State::idle) {
            std::cerr << "Rejected allocation did not leave an idle receiver without CTS\n";
            return false;
        }
    } catch (const std::bad_alloc&) {
        rejectReceiveBuffer = false;
        std::cerr << "First-frame reserve leaked bad_alloc to the caller\n";
        return false;
    }
    // Recovery must work on the same receiver without an external reset.
    auto fc = receiver.didReceiveFrame(first.frames.front().bytes);
    if (fc.type != Action::Type::writeFrames) { return false; }
    auto frames = sender.didReceiveFrame(fc.frames.front().bytes);
    Action received;
    for (const auto& frame : frames.frames) { received = receiver.didReceiveFrame(frame.bytes); }
    return received.type == Action::Type::process && received.data == payload;
}

int main() {
    bool success = true;
    for (bool defensive : {false, true}) {
        auto behavior = defensive ? Transceiver::Behavior::defensive : Transceiver::Behavior::strict;
        for (auto mode : {Transceiver::Mode::standard, Transceiver::Mode::extended}) {
            success = check(Transceiver(behavior, mode), Transceiver(behavior, mode), defensive) && success;
            success = check(TransceiverFD(behavior, mode, 0, 0, 0, 64),
                            TransceiverFD(behavior, mode, 0, 0, 0, 64), defensive) && success;
        }
    }
    if (!success) { return 1; }
    std::cout << "8 allocation-failure/recovery cases passed (classic/FD, strict/defensive, standard/extended)\n";
}

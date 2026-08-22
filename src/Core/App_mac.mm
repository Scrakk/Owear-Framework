// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App_mac.mm — main loop NSApplication.
//
#include "App.hpp"

#import <Cocoa/Cocoa.h>

#include <functional>
#include <memory>
#include <mutex>
#include <queue>

namespace ow::internal {

namespace {
std::mutex g_pendingMu;
std::queue<std::function<void()>> g_pending;

id g_observer = nil;
} // namespace

bool PlatformInit(int argc, char** argv) {
    (void)argc;
    (void)argv;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    return true;
}

void PlatformPost(std::function<void()> fn) {
    {
        std::lock_guard lock(g_pendingMu);
        g_pending.push(std::move(fn));
    }
    // despierta el runloop desde cualquier hilo
    dispatch_async(dispatch_get_main_queue(), ^{}); // noop que despierta
}

int RunMainLoop() {
    // drena callbacks periódicamente mientras corre el loop
    g_observer = [NSEvent
        addGlobalMonitorForEventsMatchingMask:NSEventMaskAny
                                      handler:^(NSEvent*) {}];
    NSTimer* timer = [NSTimer timerWithTimeInterval:0.016
                                            repeats:YES
                                              block:^(NSTimer*) {
        std::queue<std::function<void()>> batch;
        {
            std::lock_guard lock(g_pendingMu);
            batch.swap(g_pending);
        }
        while (!batch.empty()) {
            auto fn = std::move(batch.front());
            batch.pop();
            fn();
        }
    }];
    [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];

    [NSApp run];
    return 0;
}

void PlatformQuit() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp terminate:nil];
    });
}

void PlatformDelay(int ms, std::function<void()> fn) {
    auto* boxed = new std::function<void()>(std::move(fn));
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, int64_t(ms) * int64_t(NSEC_PER_MSEC)),
        dispatch_get_main_queue(), ^{
            std::unique_ptr<std::function<void()>> f(boxed);
            try {
                (*f)();
            } catch (...) {
            }
        });
}

} // namespace ow::internal

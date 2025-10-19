#include <emscripten/fiber.h>
#include <emscripten/console.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

// Stack sizes for fibers (2MB each)
#define STACK_SIZE (2 * 1024 * 1024)
#define NUM_FIBERS 2

// Fiber contexts
emscripten_fiber_t scheduler_fiber;
emscripten_fiber_t fibers[NUM_FIBERS];

// Current fiber index
int current_fiber_index = -1;
bool fibers_done[NUM_FIBERS] = {false, false};
int total_yields = 0;

// Yield function - transparently switches control back to scheduler
void yield() {
    emscripten_fiber_swap(&fibers[current_fiber_index], &scheduler_fiber);
}

// Fiber 1 entry point - prints 1 and 3
void fiber1_func(void* arg) {
    printf("1 ");
    fflush(stdout);
    yield();

    printf("3 ");
    fflush(stdout);
    yield();

    // Mark as done
    fibers_done[0] = true;
    yield();
}

// Fiber 2 entry point - prints 2 and 4
void fiber2_func(void* arg) {
    printf("2 ");
    fflush(stdout);
    yield();

    printf("4");
    fflush(stdout);
    yield();

    // Mark as done
    fibers_done[1] = true;
    yield();
}

int main() {
    printf("Starting cooperative multithreading example...\n");

    // Allocate stacks for fiber 1
    void* fiber1_c_stack = malloc(STACK_SIZE);
    void* fiber1_asyncify_stack = malloc(STACK_SIZE);

    // Allocate stacks for fiber 2
    void* fiber2_c_stack = malloc(STACK_SIZE);
    void* fiber2_asyncify_stack = malloc(STACK_SIZE);

    // Allocate stack for scheduler fiber
    void* scheduler_asyncify_stack = malloc(STACK_SIZE);

    // Initialize scheduler fiber from current context
    emscripten_fiber_init_from_current_context(&scheduler_fiber, scheduler_asyncify_stack, STACK_SIZE);

    // Initialize fiber 1
    emscripten_fiber_init(&fibers[0], fiber1_func, NULL,
                          fiber1_c_stack, STACK_SIZE,
                          fiber1_asyncify_stack, STACK_SIZE);

    // Initialize fiber 2
    emscripten_fiber_init(&fibers[1], fiber2_func, NULL,
                          fiber2_c_stack, STACK_SIZE,
                          fiber2_asyncify_stack, STACK_SIZE);

    printf("Output: ");
    fflush(stdout);

    // Simple round-robin scheduler
    // Run fibers in alternating fashion: fiber1 -> fiber2 -> fiber1 -> fiber2
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < NUM_FIBERS; i++) {
            if (!fibers_done[i]) {
                current_fiber_index = i;
                emscripten_fiber_swap(&scheduler_fiber, &fibers[i]);
            }
        }

        // Check if all fibers are done
        if (fibers_done[0] && fibers_done[1]) {
            break;
        }
    }

    printf("\nDone!\n");

    // Clean up
    free(fiber1_c_stack);
    free(fiber1_asyncify_stack);
    free(fiber2_c_stack);
    free(fiber2_asyncify_stack);
    free(scheduler_asyncify_stack);

    return 0;
}

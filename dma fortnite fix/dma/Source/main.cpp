#include <Pch.h>
#include <game.hpp>

int main()
{
    SetConsoleTitleA("Jouh DMA");
    if (!mem.Init("FortniteClient-Win64-Shipping.exe", true, true)) {
        std::cout << "Failed to initialize DMA" << std::endl;
        return 1;
    }
    system("CLS");
    cache::base = mem.GetBaseAddress("FortniteClient-Win64-Shipping.exe");
    if (!init()) {
        printf("The gui was not initialized");
        Sleep(3000);
        exit(0);
    }

    system("CLS");
    create_overlay();
    std::thread base_thread(bases);
    std::thread actor_thread(actorloop);
    HANDLE base_thread_handle = base_thread.native_handle();
    HANDLE actor_thread_handle = actor_thread.native_handle();

    SetThreadPriority(base_thread_handle, THREAD_PRIORITY_HIGHEST);
    SetThreadPriority(actor_thread_handle, THREAD_PRIORITY_HIGHEST);

    base_thread.detach();
    actor_thread.detach();
    directx_init();
    WPARAM result = render_loop();
    exit(0);
}
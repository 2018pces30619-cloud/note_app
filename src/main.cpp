#include "branchnotes/platform/main_window.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    return branchnotes::platform::RunMainWindow(instance, showCommand);
}

// Host main() of the editor programs (menu, utility, palchos, ...). The program itself is reached through
// gm_main_entry, generated per program (see cmake/Editors.cmake and edit_entry.h).
#include <stdint.h>

extern "C" int32_t gm_main_entry(int32_t argc, char **argv);

int main(int argc, char *argv[])
{
    return (int)gm_main_entry((int32_t)argc, argv);
}

// cxxmain.cpp -- for a plugin whose main() is C++ (the NES plugin's
// nes.cpp): CMakeLists.txt renames it plugin_main_cxx there, and the
// models, which are C, call it as plugin_main.
int plugin_main_cxx(void);
extern "C" int plugin_main(void);
extern "C" int plugin_main(void) { return plugin_main_cxx(); }

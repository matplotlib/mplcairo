namespace mplcairo::os {

void* dlopen(char const* filename);
bool dlclose(void* handle);
void* dlsym(void* handle, char const* symbol);
char const* dlerror();

}

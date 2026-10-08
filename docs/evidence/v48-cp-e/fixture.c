#include <stdint.h>
static unsigned char *saved;
void keep(void*p){saved=p;}
int64_t read_saved(void){return saved?*saved:-1;}
void *identity(void*p){return p;}
void mutate(void*p){*(unsigned char*)p=77;}
int64_t callback(int64_t(*f)(int64_t),int64_t x){return f(x);}

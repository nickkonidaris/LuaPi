

#ifndef picam_h
#define picam_h

#include "lua.h"
#include "picam.h"

int picam_start(lua_State *L);
int picam_list(lua_State *L);
int picam_acquire(lua_State *L);



#endif
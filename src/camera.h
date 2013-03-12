

#ifndef picam_h
#define picam_h

#include "lua.h"
#include "picam.h"

/* pi_start() to initialize camera */
int picam_start(lua_State *L);

/* available = pi_list() to list available cameras */
int picam_list(lua_State *L);

/* pi_acquire(avail) */
int picam_acquire(lua_State *L);

/* pi_set(avail, exptime, gain, ??) */
int picam_set(lua_State *L);



#endif
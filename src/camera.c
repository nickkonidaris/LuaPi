/*

	LUA -- Princeton Camera software bridge

	Copyright 2012 -- Nick Konidaris
						nick.konidaris@gmail.com


*/


#include <time.h>
#include <string.h>
#include <stdio.h>
#include "camera.h"
#include "picam.h"
#include "pil_platform.h"


/* Local function declarations */
static void camera_to_lua_table(lua_State *L,  PicamCameraID available);
static PicamCameraID lua_table_to_camera(lua_State *L);


static PicamCameraID lua_table_to_camera(lua_State *L)
{
	PicamCameraID id = {0};
	pichar  *str;

	size_t len;

	lua_pushstring(L, "interface");
	lua_gettable(L, -2);
	id.computer_interface = lua_tointeger(L,-1);
	lua_pop(L, 1);

	lua_pushstring(L, "model");
	lua_gettable(L, -2);
	id.model = lua_tointeger(L,-1);
	lua_pop(L,1);

	///////
	lua_pushstring(L, "serial");
	lua_gettable(L, -2);
	str = lua_tolstring(L, -1, &len);
	lua_pop(L,1);
	if(len > PicamStringSize_SerialNumber) {
		lua_pushstring(L, "Serial number string length is too large");
		lua_error(L);
	}
	strncpy(id.serial_number, str, len);

	///////
	lua_pushstring(L, "sensor");
	lua_gettable(L, -2);
	str = lua_tolstring(L, -1, &len);
	lua_pop(L,1);
	if(len > PicamStringSize_SensorName) {
		lua_pushstring(L, "Sensor name string length is too large");
		lua_error(L);
	}
	strncpy(id.sensor_name, str, len);

	printf("Interface: %i, model: %i, serial: %s, sensor: %s\n", 
		id.computer_interface, id.model, id.serial_number, id.sensor_name);
	return id;
}

static void camera_to_lua_table(lua_State *L,  PicamCameraID available)
{
	lua_newtable(L);
	lua_pushstring(L, "interface");
	lua_pushinteger(L, available.computer_interface);
	lua_rawset(L, -3);
	lua_pushstring(L, "model");
	lua_pushinteger(L, available.model);
	lua_rawset(L, -3);
	lua_pushstring(L, "sensor");
	lua_pushstring(L, available.sensor_name);
	lua_rawset(L, -3);
	lua_pushstring(L, "serial");
	lua_pushstring(L, available.serial_number);
	lua_rawset(L, -3);
}

/* Global function declarations */

int picam_start(lua_State *L)
{
	piint major, minor, distribution, released;
	PicamError error = 0
		;

	pibln initialized;

	Picam_IsLibraryInitialized(&initialized);

	if(! initialized) {
		error = Picam_InitializeLibrary();
		
	} else {
		printf("Library already initalized\n");
	}

	if(error) {
		printf("Failed to initalize library\n");
		return 0;
	}

	Picam_GetVersion(&major, &minor, &distribution, &released);
	printf("Library Initalized. Version: %i.%i.%i.%i\n", 
		major, minor, distribution, released);

	return 0;
}


int picam_list(lua_State *L)
{
	pibln initialized;
	const PicamCameraID* available;
	piint availableCount, i;
	PicamError error;


	// Check and report if library is not initalized
	Picam_IsLibraryInitialized(&initialized);

	if(! initialized) {
		printf("Library not initalized\n");
		return 0;
	}

	// Count out the number of available cameras
	error = Picam_GetAvailableCameraIDs( &available, &availableCount );
    if( error != PicamError_None )
    {
        printf("Failed to get available cameras.\n", error );
        return 0;
    }
    
	if(availableCount == 0) {
		return 0;
	}

	// First createtable makes an "array"
	lua_createtable(L, availableCount, 0);
	for(i = 0; i < availableCount; i++) {
		printf("%i, %i, %s, %s\n", available[i].computer_interface, 
									available[i].model, 
									available[i].sensor_name, 
									available[i].serial_number);

		camera_to_lua_table(L, available[i]);
		lua_rawseti(L, -2, i);
	}

	Picam_DestroyCameraIDs( available );
	return 1;
}



int picam_acquire(lua_State *L)
{
	
	PicamCameraID id;
	PicamHandle handle, model;
	PicamError error;
	PicamAvailableData data;
    PicamAcquisitionErrorsMask errors;
	piflt exptime = 1000.0;
	clock_t tick, tock;




	tick = clock();
	id = lua_table_to_camera(L);
	error = Picam_OpenCamera(&id, &handle);
	if( error != PicamError_None )
	{
		lua_pushstring(L, "Failed to open camera.");
		lua_error(L);
		return 0;
	}

	error = PicamAdvanced_GetCameraModel( handle, &model );
	if( error != PicamError_None )
	{
		lua_pushstring(L, "Failed to get camera model.");
		lua_error(L);
		return 0;
	}
	error = Picam_SetParameterFloatingPointValue(
		model, 
		PicamParameter_ExposureTime, 
		exptime );
	if( error != PicamError_None )
	{
		lua_pushstring(L, "Failed to set exposure time.");
		lua_error(L);
		return 0;
	}
	error = PicamAdvanced_CommitParametersToCameraDevice( model );
    if( error != PicamError_None )
    {
        lua_pushstring(L,"Failed to commit to camera device." );
        lua_error(L);
		return 0;
    }

	tock = clock();

	printf("open and set took %f seconds\n", ((float) tock-tick)/CLOCKS_PER_SEC);

	printf ("Starting acquisition with exptime: %3.1f s\n", exptime/1000);
	#define NUM_FRAMES  1
	#define NO_TIMEOUT  -1	
	Picam_Acquire(handle,	NUM_FRAMES, // Readout count
							NO_TIMEOUT, // Readout timeout, if 0 not relevant
							&data,
							&errors);


	error = Picam_CloseCamera(handle);
	if( error != PicamError_None )
	{
		lua_pushstring(L, "Failed to close camera.");
		lua_error(L);
		return 0;
	}

	return 0;
}
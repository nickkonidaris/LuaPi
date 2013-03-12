/*

	LUA -- Princeton Camera software bridge

	Copyright 2012 -- Nick Konidaris
						nick.konidaris@gmail.com


*/


#include <time.h>
#include <string.h>
#include <stdio.h>
#include <Windows.h>
#include <direct.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fitsio.h"
#include "camera.h"
#include "picam.h"
#include "picam_advanced.h"
#include "pil_platform.h"


/* Local function declarations */
#define STR_BUF_SIZE 2048

static char months[13][15] = {"", "jan", "feb", "mar", 
	"apr", "may", "jun", "jul", "aug", "sep", 
	"oct", "nov", "dec"};

static char path_prefix[STR_BUF_SIZE] = "\\sedm";

struct metadata {
	piflt exptime, adcspeed, temp;
	piint bitdepth, gain, adc;
	PicamCameraID *id;
};




static void camera_to_lua_table(lua_State *L,  PicamCameraID available);
static PicamCameraID lua_table_to_camera(lua_State *L, int index, PicamHandle *handle);
static void set_exposure_time(PicamHandle model, piflt exptime_s, lua_State *L);
static void set_exposure_time(PicamHandle model, piflt exptime_s, lua_State *L);
static void set_gain(PicamHandle model, PicamAdcAnalogGain gain, lua_State *L);
static void set_amplifier(PicamHandle model, PicamAdcQuality amplifier, lua_State *L);
static void set_adc_speed(PicamHandle model, piflt adc_speed, lua_State *L);
static void write_data_to_file(pi16u * buf, struct metadata * md, lua_State *L);
static BOOL DirectoryExists(LPCTSTR szPath);


static BOOL DirectoryExists(LPCTSTR szPath)
{
  DWORD dwAttrib = GetFileAttributes(szPath);

  return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
         (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}


static void write_data_to_file(pi16u * buf, struct metadata * md, lua_State *L)
{
	fitsfile *ff;
	int status = 0, retcode = 0;
	long naxes[2] = {2048, 2048};
	char outdir[STR_BUF_SIZE];
	char outfile[STR_BUF_SIZE];
	float bscale1 = 1.0, bzero32768 = 32768.0;
	SYSTEMTIME str_t;



	/* Create output directory */
	GetLocalTime(&str_t);
	printf("Yr: %d, Month: %d/%s date: %d\n", str_t.wYear, str_t.wMonth, months[str_t.wMonth], str_t.wDay);
	
	sprintf_s(outdir, STR_BUF_SIZE, "%s\\%4d%s%2d", path_prefix,
		str_t.wYear, months[str_t.wMonth], str_t.wDay);

	printf("%s\n\n", outdir);
	if(!DirectoryExists((LPCTSTR) outdir)) {
		printf("Creating directory %s\n", outdir);
		
		if(!mkdir(outdir)) {
			lua_pushstring(L,"Could not create path\n");
			lua_error(L);
			return;
		}
	}


	sprintf_s(outfile, STR_BUF_SIZE, "!%s\\s%4.4d%2.2d%2.2d_%2.2i_%2.2i_%2.2i.fits", outdir,str_t.wYear,
		str_t.wMonth, str_t.wDay, str_t.wHour, str_t.wMinute, str_t.wSecond);


	/* FITS housekeeping */
	retcode = fits_create_file(&ff, outfile, &status);

	if(retcode) {
		lua_pushstring(L,"Could not create FITS file\n");
		fits_report_error(stderr, status);
		lua_error(L);
		return;
	}


	retcode = fits_create_img(ff, 
		SHORT_IMG , // bitpix
		2, // naxis
		naxes, // naxes
		&status);
	if(retcode) {
		lua_pushstring(L,"Could not create image \n");
		fits_report_error(stderr, status);
		lua_error(L);
		fits_close_file(ff, &status);
		return;
	}
		
	// Following line is required to handle ushort, see:
	// "Support for Unsigned Integers and Signed Bytes" in
	// cfitsio manual
	fits_write_key(ff, TFLOAT, "BSCALE", &bscale1, NULL, &status);
	fits_write_key(ff, TFLOAT, "BZERO", &bzero32768, NULL, &status);
	fits_set_bscale(ff, 1, //BSCALE Factor
						32768, // BZERO factor
						&status); 

	fits_write_key(ff, TDOUBLE, "EXPTIME", &md->exptime, "Exposure time in s", &status);
	fits_write_key(ff, TDOUBLE, "ADCSPEED", &md->adcspeed, "Readout speed in MHz", &status);
	fits_write_key(ff, TDOUBLE, "TEMP", &md->temp, "Detector temp in deg C", &status);
	fits_write_key(ff, TINT, "BITDEPTH", &md->bitdepth, "Bit depth", &status);
	fits_write_key(ff, TINT, "GAIN_SET", &md->gain, "Gain 1: low, 2: medium, 3: high ", &status);
	fits_write_key(ff, TINT, "ADC", &md->gain, "1: Low noise, 2: high capacity",  &status);
	fits_write_key(ff, TINT, "MODEL", &md->id->model, "PI Model #", &status);
	fits_write_key(ff, TINT, "INTERFC", &md->id->computer_interface, "PI Computer Interface", &status);
	fits_write_key(ff, TSTRING, "SNSR_NM", &md->id->sensor_name, "PI sensor name", &status);
	fits_write_key(ff, TSTRING, "SER_NO", &md->id->serial_number, "PI serial #", &status);

	printf("Reported BD is %i\n", md->bitdepth);
	retcode = fits_write_img(ff,
		TUSHORT, // (T)ype is unsigned short (USHORT)
		1, // Copy from [0, 0] but fits format is indexed by 1
		naxes[0] * naxes[1], // Number of elements
		buf,
		&status);
	if(retcode && status==412) {
		printf("Overflow\n");
	} else if(retcode) {
		lua_pushstring(L,"Could not copy data over \n");
		fits_report_error(stderr, status);
		lua_error(L);
		fits_close_file(ff, &status);
		return;
	}


	fits_close_file(ff, &status);

}

static PicamCameraID lua_table_to_camera(lua_State *L, int index, PicamHandle *handle)
{
	PicamCameraID id = {0};
	pichar  *str;

	size_t len;

	lua_pushstring(L, "interface");
	lua_gettable(L, index);
	id.computer_interface = lua_tointeger(L,-1);
	lua_pop(L, 1);

	lua_pushstring(L, "model");
	lua_gettable(L, index);
	id.model = lua_tointeger(L,-1);
	lua_pop(L,1);

	lua_pushstring(L, "handle");
	lua_gettable(L, index);
	*handle = (PicamHandle) lua_tointeger(L, -1);
	lua_pop(L,1);

	///////
	lua_pushstring(L, "serial");
	lua_gettable(L, index);
	str = lua_tolstring(L, -1, &len);
	lua_pop(L,1);
	if(len > PicamStringSize_SerialNumber) {
		lua_pushstring(L, "Serial number string length is too large");
		lua_error(L);
	}
	strncpy_s(id.serial_number, PicamStringSize_SerialNumber, str, len);

	///////
	lua_pushstring(L, "sensor");
	lua_gettable(L, index);
	str = lua_tolstring(L, -1, &len);
	lua_pop(L,1);
	if(len > PicamStringSize_SensorName) {
		lua_pushstring(L, "Sensor name string length is too large");
		lua_error(L);
	}
	strncpy_s(id.sensor_name, PicamStringSize_SensorName, str, len);

	printf("Interface: %i, model: %i, serial: %s, sensor: %s\n", 
		id.computer_interface, id.model, id.serial_number, id.sensor_name);
	return id;
}

static void camera_to_lua_table(lua_State *L,  PicamCameraID available)
{

	PicamHandle handle;

	Picam_OpenCamera(&available, &handle);

	lua_newtable(L);
	lua_pushstring(L, "handle");
	lua_pushinteger(L, (int) handle);
	lua_rawset(L, -3);

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

static void set_exposure_time(PicamHandle model, piflt exptime_s, lua_State *L)
{
	PicamParameter* failed_parameter_array;
	piint num_errors;
	PicamError error = 0;

	error = Picam_SetParameterFloatingPointValue(
		model, 
		PicamParameter_ExposureTime, 
		exptime_s*1000.0 );
	if( error != PicamError_None )
	{
		lua_pushstring(L, "Failed to set exposure time ");
		lua_error(L);
	}

	error = Picam_CommitParameters(model, &failed_parameter_array, &num_errors);
    if( error != PicamError_None )
    {
        lua_pushstring(L,"Failed to commit to camera device." );
        lua_error(L);
	} else {
		printf("Commited exposure time %3.1f.\n", exptime_s);
	}
}

static void set_gain(PicamHandle model, PicamAdcAnalogGain gain, lua_State *L)
{
	PicamParameter* failed_parameter_array;
	piint num_errors;
	PicamError error = 0;

	error = Picam_SetParameterIntegerValue(
		model, 
		PicamParameter_AdcAnalogGain, 
		gain );
	if( error != PicamError_None )
	{
		lua_pushstring(L, "Failed to set exposure time ");
		lua_error(L);
	}

	error = Picam_CommitParameters(model, &failed_parameter_array, &num_errors);
    if( error != PicamError_None )
    {
        lua_pushstring(L,"Failed to commit to camera device." );
        lua_error(L);
	} else {
		printf("Commited gain %i.\n", gain);
	}
}

static void set_adc_speed(PicamHandle model, piflt adc_speed, lua_State *L)
{
	PicamParameter* failed_parameter_array;
	piint num_errors;
	PicamError error = 0;

	error = Picam_SetParameterFloatingPointValue(
		model,
		PicamParameter_AdcSpeed, 
		adc_speed );
	if( error != PicamError_None )
	{
		lua_pushstring(L, "Failed to set adc speed");
		lua_error(L);
		return;
	}

	error = Picam_CommitParameters(model, &failed_parameter_array, &num_errors);
    if( error != PicamError_None )
    {
        lua_pushstring(L,"Failed to commit to camera device." );
        lua_error(L);
		return;
    }
	printf("Committed ADC Speed to %1.1f MHz.\n", adc_speed);
}


static void set_amplifier(PicamHandle model, PicamAdcQuality amplifier, lua_State *L)
{
	
	PicamParameter* failed_parameter_array;
	piint num_errors;
	PicamError error = 0;

	error = Picam_SetParameterIntegerValue(
		model,
		PicamParameter_AdcQuality, 
		amplifier );
	if( error != PicamError_None )
	{
		lua_pushstring(L, "Failed to set amplifier ");
		lua_error(L);
		return;
	}

	error = Picam_CommitParameters(model, &failed_parameter_array, &num_errors);
    if( error != PicamError_None )
    {
        lua_pushstring(L,"Failed to commit to camera device." );
        lua_error(L);
		return;
    }
	printf("Committed amplifier to %i.\n", amplifier);
}
/* Global function declarations */

int picam_start(lua_State *L)
{
	piint major, minor, distribution, released;
	PicamError error = 0;

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


/*
	Takes camera ID, exptime, 
	gain, 
	amplifier, 
	adcspeed

*/
int picam_set(lua_State *L)
{
	PicamCameraID id;
	PicamHandle handle, model;
	PicamError error;
	piflt exptime_s = 0.0, adcspeed;
	PicamAdcQuality amplifier;
	PicamAdcAnalogGain gain;
	clock_t tick, tock;
	pibln committed;

	tick = clock();
	id = lua_table_to_camera(L, 1, &handle);
	exptime_s = lua_tonumber(L, 2);
	gain = lua_tointeger(L, 3);
	amplifier = lua_tointeger(L, 4);
	adcspeed = lua_tonumber(L, 5);


	printf("Setting camera %s: exptime %3.1f s, gain %i, amp %i, adcspeed %1.1f MHz\n",
		id.sensor_name, exptime_s, gain, amplifier, adcspeed);
	

	error = PicamAdvanced_GetCameraModel( handle, &model );
	if( error != PicamError_None )
	{
		lua_pushstring(L, "Failed to get camera model.");
		lua_error(L);
		return 0;
	}

	set_exposure_time(model, exptime_s, L);
	set_gain(model, gain, L);
	set_amplifier(model, amplifier, L);
	set_adc_speed(model, adcspeed, L);
	
	tock = clock();

	printf("set took %f seconds\n", ((float) tock-tick)/CLOCKS_PER_SEC);

	Picam_AreParametersCommitted(handle, &committed);
	printf("The camera %s all values commited.\n", (committed ? "has" : "does not have"));

	lua_pushboolean(L, committed);
	return 1;
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
	PicamHandle handle = 0, model = 0;
	PicamError error = 0;
	PicamAvailableData data;
	PicamAcquisitionErrorsMask errors;
	clock_t tick, tock;
	struct metadata md;
	pi16u * buf;


	tick = clock();
	id = lua_table_to_camera(L, 1, &handle);

	Picam_GetParameterFloatingPointValue( handle, PicamParameter_ExposureTime, &md.exptime );
	md.exptime /= 1000;
	Picam_GetParameterFloatingPointValue( handle, PicamParameter_AdcSpeed, &md.adcspeed );
	Picam_GetParameterIntegerValue( handle, PicamParameter_AdcBitDepth, &md.bitdepth );
	Picam_GetParameterIntegerValue( handle, PicamParameter_AdcAnalogGain, &md.gain );
	Picam_GetParameterIntegerValue( handle, PicamParameter_AdcQuality, &md.adc );
	Picam_GetParameterFloatingPointValue( handle, PicamParameter_SensorTemperatureReading, &md.temp );
	
	md.id = &id;


	#define NUM_FRAMES  1
	#define NO_TIMEOUT  -1	
	Picam_Acquire(handle,	NUM_FRAMES, // Readout count
		NO_TIMEOUT, // Readout timeout, if 0 not relevant
		&data,
		&errors);

	tock = clock();

	buf = data.initial_readout;
	if(data.readout_count != 1) {
		lua_pushstring(L, "More than 1 count found");
		lua_error(L);
	}
	write_data_to_file(buf, &md, L);
	printf("High: %d\n", *(buf+2048*2000+1000));
	printf("Middle: %d\n", *(buf+2048*1000+1000));
	printf("Low: %d\n", *(buf+2048*10+1000));
	printf("Acquisition took %5.2f s\n", ((float) tock-tick)/CLOCKS_PER_SEC);
	return 0;
}
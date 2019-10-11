/**
 * Copyright (c) 2018, Helmholtz - Zentrum Dresden - Rossendorf
 * See LICENSE file.
 * 
 * At the meoment only USB spectrometers from OceanOptics are supported.
 * Implementation for RS232 and Etnernmet based spectrometers is still missing.
 * Currently supports only spectrometers with one optical bench.
 * Arun: January 18,2019 Added #include <stdlib.h>
 */

#include <iostream>
#include <stdlib.h>
#include <epicsTime.h>
#include <epicsExit.h>
#include <epicsExport.h>

#include "SeaBreezeSpec.h"

#include "api/seabreezeapi/SeaBreezeAPI.h"
#include "api/seabreezeapi/SeaBreezeAPIConstants.h"


/* define C99 standard __func__ to come from MS's __FUNCTION__ */
#if defined ( _MSC_VER )
#define __func__ __FUNCTION__
#endif

#define SEABREEZE_MAX_DEVICES 16

const char *SeaBreezeSpec::driverName = "SeaBreezeSpec";

static void SeaBreezeSpecHandleNewImageTaskC(void *drvPvt);


extern "C" {
    /** Configuration command for SeaBreezeSpec driver; creates a new SeaBreeceSpec object.
     * \param[in] portName The name of the asyn port driver to be created.
     * \param[in] maxBuffers The maximum number of NDArray buffers that the
     *            NDArrayPool for this driver is
     *            allowed to allocate. Set this to -1 to allow an unlimited number
     *            of buffers.
     * \param[in] maxMemory The maximum amount of memory that the NDArrayPool for
     *            this driver is allowed to allocate. Set this to -1 to allow an
     *            unlimited amount of memory.
     * \param[in] priority The thread priority for the asyn port driver thread if
     *            ASYN_CANBLOCK is set in asynFlags.
     * \param[in] stackSize The stack size for the asyn port driver thread if
     *            ASYN_CANBLOCK is set in asynFlags.
     */
    int SeaBreezeSpecConfig(const char *portName, int maxBuffers, size_t maxMemory, int priority, int stackSize, const char *id) {
        new SeaBreezeSpec(portName, maxBuffers, maxMemory, priority, stackSize, id);
        return (asynSuccess);
    }
                              
    /**
    * Callback function for exit hook
    */
    static void exitCallbackC(void *pPvt){
        SeaBreezeSpec *pSeaBreezeSpec = (SeaBreezeSpec*)pPvt;
        delete pSeaBreezeSpec;
    }
}

SeaBreezeSpec::SeaBreezeSpec(const char *portName, int maxBuffers, size_t maxMemory, int priority, int stackSize, const char *id) :
  //  ADDriver(portName, 1, int(NUM_SEABREEZESPEC_PARAMS), maxBuffers, maxMemory, asynDrvUserMask | asynFloat64ArrayMask, 0, ASYN_CANBLOCK, 1, priority, stackSize) {
ADDriver(portName, 1, 0, maxBuffers, maxMemory, asynDrvUserMask | asynFloat64ArrayMask, 0, ASYN_CANBLOCK, 1, priority, stackSize),imageThreadKeepAlive(true){
    device = 0;
    deviceId = (char*)calloc(1, strlen(id)+1);
    memcpy(deviceId,id, strlen(id)+1);
    
    createParam(SB_HardwareRevisionString, asynParamOctet, &sb_HardwareRevision);
    createParam(SB_HasLampString, asynParamInt32, &sb_HasLamp);
    createParam(SB_HasShutterString, asynParamInt32, &sb_HasShutter);
    createParam(SB_HasLightSourceString, asynParamInt32, &sb_HasLightSource);
    createParam(SB_HasAcquisitionDelayString, asynParamInt32, &sb_HasAcquisitionDelay);
    createParam(SB_HasTemperatureString, asynParamInt32, &sb_HasTemperature);
    createParam(SB_EnableLampString, asynParamInt32, &sb_EnableLamp);
    createParam(SB_MinimumIntegrationTimeString, asynParamFloat64, &sb_MinimumIntegrationTime);
    createParam(SB_IntegrationTimeString, asynParamFloat64, &sb_IntegrationTime);
    createParam(SB_UseFormatedSpectrumString, asynParamInt32, &sb_UseFormatedSpectrum);
    createParam(SB_FormatedSpectrumSizeString, asynParamInt32, &sb_FormatedSpectrumSize);
    createParam(SB_RawSpectrumSizeString, asynParamInt32, &sb_RawSpectrumSize);
    createParam(SB_MaxIntensityString, asynParamFloat64, &sb_MaxIntensity);
    createParam(SB_WaveLengthString, asynParamFloat64, &sb_WaveLength);
   
    sbapi_initialize();
    if(sbapi_probe_devices() == 0) {
        printf("%s\n", "No devices found!");
//        return;
		exit(0); //no reason to actually run things when it isn't going to actually work
    }
    
    HandleNewImageEvent = epicsEventCreate(epicsEventEmpty);
    /* Create the thread that updates the images */
    imageThreadId = epicsThreadCreate("SeaBreezeSpecHandleNewImageTaskC",
                                epicsThreadPriorityMedium,
                                epicsThreadGetStackSize(epicsThreadStackMedium),
                                (EPICSTHREADFUNC)SeaBreezeSpecHandleNewImageTaskC,
                                this);
    if(imageThreadId == 0) {
        std::cout << "Failed to start acquisition task" << std::endl;
        return;
    }
    
    initializeDetector();
    epicsAtExit(exitCallbackC, this); //this isn't working correctly at the instant
        
}

SeaBreezeSpec::~SeaBreezeSpec(void) {
    int error;
    
    if(device) {
        if(mAcquiringData)
            acquireStop();
        sbapi_close_device(device, &error);
        if(error != ERROR_SUCCESS) {
            printf("%s\n", sbapi_get_error_string(error));
        }
    }
    sbapi_shutdown();
}

void SeaBreezeSpec::report(FILE *fp, int details) {
    ADDriver::report(fp, details);
}

asynStatus SeaBreezeSpec::initializeDetector() {
    asynStatus status = asynError;
    int   connected_device_count = 0;       // how many spectrometers found
    int   serial_number_count = 0;          // how many serial numbers are implemented
    int   number_of_spectrometers = 0;
    int   i,j;                              // lop counters
    int   error = 0;                        // SeaBreeze error result code
    long *device_ids;
    long *serialNumbers;
    unsigned char serialNumberLength = 0;
    char *serialNumber;
    long *spectrometers;
    long *revisions;
    int pixels = 0;
    int tmp = 0;
    char buffer[80];
    long integrationTime = 0;
    double maxIntensity = 0;
    int dataType = 0;
    
    connected_device_count = sbapi_get_number_of_device_ids();
    if(connected_device_count == 0)
        return asynError;
    
    device_ids = (long *)calloc(connected_device_count, sizeof(long));
    connected_device_count = sbapi_get_device_ids(device_ids, connected_device_count);
    for(i = 0; i < connected_device_count; i++) {
        printf("%d: Device 0x%02lX:\n", i, device_ids[i]);
        if(sbapi_open_device(device_ids[i], &error) == 1) {
            printf("%s\n", sbapi_get_error_string(error));
            free(device_ids);
            return asynError;
        }
        serial_number_count = sbapi_get_number_of_serial_number_features(device_ids[i], &error);
        if((error != ERROR_SUCCESS) || (serial_number_count == 0)){
            printf("%s\n", sbapi_get_error_string(error));
            sbapi_close_device(device_ids[i], &error);
            free(device_ids);
            return asynError;
        }
        serialNumbers = (long *)calloc(serial_number_count, sizeof(long));
        serial_number_count = sbapi_get_serial_number_features(device_ids[i], &error, serialNumbers, serial_number_count);
        if(error != ERROR_SUCCESS) {
            printf("%s\n", sbapi_get_error_string(error));
            sbapi_close_device(device_ids[i], &error);
            free(device_ids);
            free(serialNumbers);
            return asynError;
        }
        for(j=0;j<serial_number_count;j++) {
            serialNumberLength = sbapi_get_serial_number_maximum_length(device_ids[i], serialNumbers[j], &error);
            if(error != ERROR_SUCCESS) {
                printf("%s\n", sbapi_get_error_string(error));
                sbapi_close_device(device_ids[i], &error);
                free(device_ids);
                free(serialNumbers);
                return asynError;
            }
            serialNumber = (char *)calloc(serialNumberLength+1, sizeof(char));
            serialNumberLength = sbapi_get_serial_number(device_ids[i], serialNumbers[j], &error, serialNumber, serialNumberLength);
            if(error != ERROR_SUCCESS) {
                printf("%s", sbapi_get_error_string(error));
                sbapi_close_device(device_ids[i], &error);
                free(device_ids);
                free(serialNumbers);
                free(serialNumber);
                return asynError;
            }
            if(strcmp(serialNumber, deviceId) == 0) {
                device = device_ids[i];
                setStringParam(ADSerialNumber, serialNumber);
                setStringParam(ADManufacturer, "OceanOptics");
                setStringParam(ADSDKVersion, "3.0.11");
                free(serialNumber);
                free(serialNumbers);
                free(device_ids);
                // get device type
                if((sbapi_get_device_type(device, &error, buffer, 80) <= 80) && (error == ERROR_SUCCESS)) {
                    setStringParam(ADModel, buffer);
                }
                else {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
                    return asynError;
                }
                // get hardware and firmware revision numbers
                tmp = sbapi_get_number_of_revision_features(device, &error);
//				printf("Revision features %d, error %d \n", tmp, error);
                if(error == ERROR_SUCCESS) {
                    if(tmp > 0) {
                        revisions = (long*)calloc(tmp, sizeof(long));
                        if(sbapi_get_revision_features(device, &error, revisions, tmp) != tmp) {
                            printf("%s\n", sbapi_get_error_string(error));
                            free(revisions);
                            sbapi_close_device(device_ids[i], &error);
                            return asynError;
                        }
                        for(i=0;i<tmp;i++) {
                            memset(buffer,0, 80);
                            buffer[0] = sbapi_revision_hardware_get(device, revisions[j], &error);
							printf("%s\n", buffer);
                            if(error == ERROR_SUCCESS) {
                                setStringParam(sb_HardwareRevision, &buffer[0]);
                            }
                            buffer[10] = sbapi_revision_firmware_get(device, revisions[j], &error);
                            if(error == ERROR_SUCCESS) {
                                setStringParam(ADFirmwareVersion, &buffer[10]);
                            }
                        }
                        free(revisions);
                    }
                    else {
                        // naot available for this device
                        setStringParam(sb_HardwareRevision, "unknown");
                        setStringParam(ADFirmwareVersion, "unknown");
                    }
                }
                else {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
                    return asynError;
                }
                // get access to optica bench in spectrometer
                number_of_spectrometers = sbapi_get_number_of_spectrometer_features(device, &error);
                printf("\tNumber of spectrometers: %d\n", number_of_spectrometers);
                spectrometers = (long*)calloc(number_of_spectrometers, sizeof(long));
                // Get number of pixels
                number_of_spectrometers = sbapi_get_spectrometer_features(device, &error, spectrometers, number_of_spectrometers);
                spectrometer = spectrometers[0];
                pixels = sbapi_spectrometer_get_formatted_spectrum_length(device, spectrometer, &error);
                if(pixels >0) {
                    setIntegerParam(sb_FormatedSpectrumSize, pixels);
                    setIntegerParam(sb_UseFormatedSpectrum, 1);
                    setIntegerParam(ADMaxSizeX, pixels);
                    setIntegerParam(ADMaxSizeY, 1);
                    setIntegerParam(ADSizeX, pixels);
                    setIntegerParam(ADSizeY, 1);
                    setIntegerParam(ADMinX, 0);
                    setIntegerParam(ADMinY, 0);
                    setIntegerParam(ADReverseX, 0);
                    setIntegerParam(ADReverseY, 0);
                    setDoubleParam(ADGain, 1.);
                    setIntegerParam(ADImageMode, NDColorModeMono);
                    setIntegerParam(ADFrameType, ADFrameNormal);
                    setIntegerParam(ADNumExposures, 1);
                    setIntegerParam(ADNumImages, 1);
                   
                    wavelength = (double *)calloc(pixels, sizeof(double));
                    tmp = sbapi_spectrometer_get_wavelengths(device, spectrometer, &error, wavelength, pixels);
                    if(error != ERROR_SUCCESS)
                    {
                        printf("%s\n", sbapi_get_error_string(error));
                        sbapi_close_device(device_ids[i], &error);
                        return asynError;
                    }
                    printf("\tNumber of wavelength read: %d [%.2fnm - %.2fnm]\n", tmp, wavelength[0], wavelength[tmp-1]);
                    maxIntensity = sbapi_spectrometer_get_maximum_intensity(device,spectrometer, &error);
                    if(error != ERROR_SUCCESS) {
                        printf("%s\n", sbapi_get_error_string(error));
                        sbapi_close_device(device_ids[i], &error);
                        return asynError;
                    }
                    setDoubleParam(sb_MaxIntensity, maxIntensity);
                    if(maxIntensity < 0xff)
                        dataType = NDInt8;
                    else
                     if(maxIntensity < 0x1ff)
                         dataType = NDUInt8;
                     else
                         if(maxIntensity < 0xffff)
                             dataType = NDInt16;
                         else
                             if(maxIntensity < 0x1ffff)
                                 dataType = NDUInt16;
                             else
                                 if(maxIntensity < 0xffffffff)
                                     dataType = NDInt32;
                                 else
                                     dataType = NDUInt32;
					// this is wrong since the spectra data is all float32 or char, default float
                    setIntegerParam(NDDataType, dataType);
					setIntegerParam(NDDataType, NDFloat32);
                }
                else {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
                    free(spectrometers);
                    return asynError;
                }
                pixels = sbapi_spectrometer_get_unformatted_spectrum_length(device, spectrometer, &error);
                if(error != ERROR_SUCCESS) {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
					free(spectrometers);
                    return asynError;
                }
                setIntegerParam(sb_RawSpectrumSize, pixels);
                printf("\tNumber of pixels in unformated spectrum: %d\n", pixels);
                free(spectrometers);
                // get minimum integration time
                integrationTime = sbapi_spectrometer_get_minimum_integration_time_micros(device, spectrometer, &error);
                if(error == ERROR_SUCCESS) {
                    setDoubleParam(sb_MinimumIntegrationTime, integrationTime);
                    setDoubleParam(sb_IntegrationTime, integrationTime);
                }
                //get pixel binning
                tmp = sbapi_get_number_of_pixel_binning_features(device, &error);
                if(error == ERROR_SUCCESS) {
                    printf("\tNumber of pixel binning features: %d\n", tmp);
                    if(tmp > 0) {
                        // TODO: Implement reading of binning factor
                    }
                    else
                    {
                        setIntegerParam(ADBinX, 1);
                        setIntegerParam(ADBinY, 1);
                    }
                }
                else {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
                    return asynError;
                }
                // get shutter
                tmp = sbapi_get_number_of_shutter_features(device, &error);
                if(error == ERROR_SUCCESS) {
                    printf("\tNumber of shutter features: %d\n", tmp);
                    if(tmp > 0) {
                        // TODO: Implement reading of shutter feature
                        setIntegerParam(sb_HasShutter, 1);
                    }
                    else
                        setIntegerParam(sb_HasShutter, 0);
                }
                else {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
                    return asynError;
                }
                // get light source
                tmp = sbapi_get_number_of_light_source_features(device, &error);
                if(error == ERROR_SUCCESS) {
                    printf("\tNumber of light source features: %d\n", tmp);
                    if(tmp > 0) {
                        // TODO: Implement reading of binning factor
                        setIntegerParam(sb_HasLightSource, 1);
                    }
                    else
                        setIntegerParam(sb_HasLightSource, 0);
                }
                else {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
                    return asynError;
                }
                // get lamp
                tmp = sbapi_get_number_of_lamp_features(device, &error);
                if(error == ERROR_SUCCESS) {
                    printf("\tNumber of lamp features: %d\n", tmp);
                    if(tmp > 0) {
                        lamps = (long*)calloc(tmp, sizeof(long));
                        if(sbapi_get_lamp_features(device, &error, lamps, tmp) != tmp) {
                            printf("%s\n", sbapi_get_error_string(error));
                            sbapi_close_device(device_ids[i], &error);
                            return asynError;
                        }
                        setIntegerParam(sb_HasLamp, 1);
                        for(j=0;j<tmp;j++) {
                            sbapi_lamp_set_lamp_enable(device, lamps[j], &error, 0);
                        }
                        setIntegerParam(sb_EnableLamp, 0);
                    }
                    else {
                        setIntegerParam(sb_HasLamp, 0);
                        setIntegerParam(sb_EnableLamp, 0);
                    }
                }
                else {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
                    return asynError;
                }
                // get acquisition delay
                tmp = sbapi_get_number_of_acquisition_delay_features(device, &error);
                if(error == ERROR_SUCCESS) {
                    printf("\tNumber of acquisition deley features: %d\n", tmp);
                    if(tmp > 0) {
                        // TODO: Implement reading of binning factor
                        setIntegerParam(sb_HasAcquisitionDelay, 1);
                    }
                    else
                        setIntegerParam(sb_HasAcquisitionDelay, 0);
                }
                else {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
                    return asynError;
                }
                // get temperature feature
                tmp = sbapi_get_number_of_temperature_features(device, &error);
                if(error == ERROR_SUCCESS) {
                    printf("\tNumber of temperature features: %d\n", tmp);
                    if(tmp > 0) {
                        // TODO: Implement reading of binning factor
                        setIntegerParam(sb_HasTemperature, 1);
                    }
                    else
                        setIntegerParam(sb_HasTemperature, 0);
                }
                else {
                    printf("%s\n", sbapi_get_error_string(error));
                    sbapi_close_device(device_ids[i], &error);
                    return asynError;
                }
                return asynSuccess;
            }
        }
        sbapi_close_device(device_ids[i], &error);
        device = 0;
        free(serialNumbers);
        free(device_ids);
    }
    return status;
}

asynStatus SeaBreezeSpec::writeInt32(asynUser *pasynUser, epicsInt32 value) {
    asynStatus status = asynSuccess;
    int function = pasynUser->reason;
    int error;
    int acquiring;
    int tmp;
    
    getIntegerParam(ADAcquire, &acquiring);
    
    setIntegerParam(function, value);
    
    if(function == sb_EnableLamp) {
        sbapi_lamp_set_lamp_enable(device, lamps[0], &error, value);
        if(error != ERROR_SUCCESS) {
            printf("%s\n", sbapi_get_error_string(error));
            return asynError;
        }
    }
    else {
        if(function == sb_UseFormatedSpectrum) {
            setIntegerParam(function, value);
            if(value == 0) {
                getIntegerParam(sb_RawSpectrumSize, &tmp);
                setIntegerParam(ADMaxSizeX, tmp);
                setIntegerParam(ADSizeX, tmp);
            }
            else {
                getIntegerParam(sb_FormatedSpectrumSize, &tmp);
                setIntegerParam(ADMaxSizeX, tmp);
                setIntegerParam(ADSizeX, tmp);
            }
                
        }
        else {
            if (function == ADAcquire) {
                if (value && !acquiring){
                    mAcquiringData = true;
                    acquireStart();
                }
                else {
                    if (!value && acquiring){
                        mAcquiringData = false;
                        acquireStop();
                    }
                }
            }
            else {
                if (function < FIRST_SEABREEZE_PARAM) {
                    status = ADDriver::writeInt32(pasynUser, value);
                }
            }
            
        }
    }
    return status;
}

asynStatus SeaBreezeSpec::readInt32(asynUser *pasynUser, epicsInt32 *value) {
    asynStatus status = asynSuccess;
    int function = pasynUser->reason;
    
    if(
		(function == sb_HasLamp) || 
		(function == sb_EnableLamp) || 
		(function == sb_HasAcquisitionDelay) || 
		(function == sb_HasLightSource) || 
		(function == sb_HasShutter) || 
		(function == sb_HasTemperature) ||
        (function == sb_UseFormatedSpectrum) || 
		(function == sb_FormatedSpectrumSize) || 
		(function == sb_RawSpectrumSize)
		) {
        return getIntegerParam(function, value);
        
    }
    
    /* If this parameter belongs to a base class call its method */
    if (function < FIRST_SEABREEZE_PARAM) {
        status = ADDriver::readInt32(pasynUser, value);
    }

    
//    setIntegerParam(function, *value); // why do this when it should have returned by now
    return status;
}

asynStatus SeaBreezeSpec::writeFloat64(asynUser *pasynUser, epicsFloat64 value) {
    asynStatus status = asynSuccess;
    int function = pasynUser->reason;
    double minimum;
    int error;
    
    if(function == sb_IntegrationTime) {
        getDoubleParam(sb_MinimumIntegrationTime, &minimum);
        if(value < minimum)
            value = minimum;
        sbapi_spectrometer_set_integration_time_micros(device, spectrometer, &error, (long)value);
        if(error != ERROR_SUCCESS) {
            printf("%s\n", sbapi_get_error_string(error));
            return asynError;
        }
    }
    else {
        if (function < FIRST_SEABREEZE_PARAM) {
            status = ADDriver::writeFloat64(pasynUser, value);
        }
        
    }
    setDoubleParam(function, value);
    
    return status;    
}

asynStatus SeaBreezeSpec::readFloat64(asynUser *pasynUser, epicsFloat64 *value) {
    asynStatus status = asynSuccess;
    int function = pasynUser->reason;
    
    if(
		(function == sb_MinimumIntegrationTime) || 
		(function == sb_IntegrationTime) || 
		(function == sb_MaxIntensity)
		)
        return getDoubleParam(function, value);
 
    if (function < FIRST_SEABREEZE_PARAM) {
        status = ADDriver::readFloat64(pasynUser, value);
    }
    
    return status;    
}

asynStatus SeaBreezeSpec::readFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements, size_t *nIn)
{
    asynStatus status = asynSuccess;
    int function = pasynUser->reason;
    int pixels;
    
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Enter\n", driverName, __func__);
    if(function == sb_WaveLength) {
        getIntegerParam(ADMaxSizeX, &pixels);
        memcpy(value, wavelength, pixels*sizeof(double));
        *nIn = pixels;
    }
    else {
        if (function < FIRST_SEABREEZE_PARAM) {
            status = ADDriver::readFloat64Array(pasynUser, value, nElements, nIn);
        }
    }
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n", driverName, __func__);
    return status;
}

asynStatus SeaBreezeSpec::acquireStart() {
    int imageMode;
    int triggerMode;
    int formatedSpectrumMode;
    int numX;
    int numY;
    int minX;
    int minY;
    int error;
    double integrationTime;
    int bufferSize;
    
    
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Enter\n", driverName, __func__);
    lock();
    setIntegerParam(ADStatus, ADStatusInitializing);
    // reset Image counter
    setIntegerParam(ADNumImagesCounter, 0);
    callParamCallbacks();
    unlock();
    getIntegerParam(ADImageMode, &imageMode);
    
    /* Get Image size for use by acquisition handling*/
    getIntegerParam(ADSizeX, &numX);
    getIntegerParam(ADSizeY, &numY);
    getIntegerParam(ADMinX,  &minX);
    getIntegerParam(ADMinY,  &minY);
    getIntegerParam(ADTriggerMode, &triggerMode);
    getIntegerParam(sb_UseFormatedSpectrum, &formatedSpectrumMode);
    getDoubleParam(sb_IntegrationTime, &integrationTime);
    getIntegerParam(ADSizeX, &bufferSize);
    
    imageDataType = NDFloat32;
    
    imageDims[0] = numX-minX;
    imageDims[1] = numY-minY;
    
    this->lock();
    sbapi_spectrometer_set_trigger_mode(device, spectrometer, &error, triggerMode == ADTriggerInternal?0:triggerMode == ADTriggerExternal?3:0);
    if(error != ERROR_SUCCESS) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s %s: %s\n", driverName, __func__, "set triggermode error: ", sbapi_get_error_string(error));
    }
    sbapi_spectrometer_set_integration_time_micros(device,spectrometer, &error, (long)integrationTime);
    if(error != ERROR_SUCCESS) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s %s: %s\n", driverName, __func__, "set integration time erro: ", sbapi_get_error_string(error));
    }

// quick and dirty patch to correct the original issue of releasing memory while still acquiring

	if (buffer != NULL) {
		free(buffer);
//		buffer = 0;
//	printf("Buffer Freed \n");	
	}
	
    if(formatedSpectrumMode) {
        buffer = calloc(bufferSize, sizeof(double));
//		printf("Double buffer allocated \n");
     }
    else {
        buffer = calloc(bufferSize, sizeof(unsigned char));
//		printf("short char Buffer Allocated \n");
    }
	
    this->unlock();
    epicsEventSignal(HandleNewImageEvent);
    
    switch(imageMode) {
        case ADImageSingle:
            presetImages = 1;
            break;
        case ADImageMultiple:
            getIntegerParam(ADNumImages, &presetImages);
            break;
        case ADImageContinuous:
            presetImages = 0;
            break;
    }
    
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n", driverName, __func__);
    return asynSuccess;
}

asynStatus SeaBreezeSpec::acquireStop() {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Enter\n", driverName, __func__);
    setIntegerParam(ADAcquire,0);
    mAcquiringData = false;
  
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Exit\n", driverName, __func__);
    
    return asynSuccess;
}


/**
 * Callback when a new Image event is seen.  Call the driver's method
 * SeaBreezeHandleNewImageTask
 */
static void SeaBreezeSpecHandleNewImageTaskC(void *drvPvt)
{
    SeaBreezeSpec *pPvt = (SeaBreezeSpec *)drvPvt;
    
    pPvt->SeaBreezeSpecHandleNewImageTask();
}


/**
 * Handler class for recieving new images.  This runs in a thread separate
 * from the picam driver thread to avoid collisions.  Acquisition in the
 * picam thread will signal this thread as soon as possible when new images
 * are seen.
 */
void SeaBreezeSpec::SeaBreezeSpecHandleNewImageTask(void) {
    epicsEventWaitStatus newImageTimeoutStatus = epicsEventWaitTimeout;
    double imageTimeout = 0.000001;    int acquiring = 0;
    int formatedSpectrumMode;
    int imageMode;
    int imagesCounter;
    int numImages;
    int arrayCounter;
    int arrayCallbacks;
    int error;
    int bufferSize;
    size_t i;
    double *tmp;
    float *tmp2;
    NDArrayInfo arrayInfo;
    epicsTimeStamp currentTime;
    
    while (true) {
        this->unlock();
        dataLock.unlock();
        while ( newImageTimeoutStatus ) {
            newImageTimeoutStatus = epicsEventWaitWithTimeout(HandleNewImageEvent, imageTimeout);
            if (!imageThreadKeepAlive){
                asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Image handling thread has been terminated.\n", driverName, __func__);
                return;
            }
        }
        newImageTimeoutStatus = epicsEventWaitTimeout;
        //Sanity check that main thread thinks we are acquiring data
        if (mAcquiringData) {
            acquiring = 1;
        }
        else {
            acquiring = 0;
        }
        // Reset the counters
        setIntegerParam(ADNumImagesCounter, 0);
        setIntegerParam(ADNumExposuresCounter, 0);
        callParamCallbacks();
        getIntegerParam(sb_UseFormatedSpectrum, &formatedSpectrumMode);
        getIntegerParam(ADSizeX, &bufferSize);


        //dataLock.lock();
        //this->lock();
        
        while(acquiring) {
            if(!mAcquiringData)
                acquiring = 0;
            this->unlock();
//			printf("Image memory buffer %d \n", buffer);

            if(formatedSpectrumMode) {
                sbapi_spectrometer_get_formatted_spectrum(device, spectrometer, &error,(double *)buffer, bufferSize);
				imageDataType = NDFloat32;
				setIntegerParam(NDDataType, NDFloat32);
//				setIntegerParam(NDDataType, NDFloat64);
            }
            else {
                 sbapi_spectrometer_get_unformatted_spectrum(device, spectrometer, &error,(unsigned char *) buffer, bufferSize);
				 imageDataType = NDUInt8;
				 setIntegerParam(NDDataType, NDUInt8);
            }
//			sbapi_spectrometer_getErrorString(error, newbuffer, 80);
//			printf("Camera Error Code is %d bufferSize %d\n", error,bufferSize);

            this->lock();
            getIntegerParam(ADImageMode, &imageMode);
            getIntegerParam(ADNumImages, &numImages);
            getIntegerParam(ADNumImagesCounter, &imagesCounter);
            imagesCounter++;
            setIntegerParam(ADNumImagesCounter, imagesCounter);
            callParamCallbacks();
            /* Update the image */
            /* First release the copy that we held onto last time */
            if (this->pArrays[0]) {
                this->pArrays[0]->release();
            }
            
            getIntegerParam(NDArrayCallbacks, &arrayCallbacks);
            if (arrayCallbacks) {
                /* Allocate a new array */
                this->pArrays[0] = pNDArrayPool->alloc(2, imageDims, imageDataType, 0, NULL);
                if (this->pArrays[0] != NULL) {
                    pImage = this->pArrays[0];
                    pImage->getInfo(&arrayInfo);
                    // Copy data from the input to the output, correct 
                    if(formatedSpectrumMode) {
                        tmp = (double *)buffer;
                        tmp2 = (float *)pImage->pData;
                        for(i=0;i<imageDims[0]; i++) {
                            tmp2[i] = (float)tmp[i];
//						memcpy(pImage->pData, (float *)buffer, bufferSize);
//std::cout << ((float*)pImage->pData)[i] << " ";
                        }
                        //std::cout << std::endl;
                        //std::cout << std::endl;
                    }
                    else {
                        memcpy(pImage->pData, buffer, bufferSize);
                    }
                    
                    getIntegerParam(NDArrayCounter, &arrayCounter);
                    arrayCounter++;
                    setIntegerParam(NDArrayCounter, arrayCounter);
                    
                    epicsTimeGetCurrent(&currentTime);
                    pImage->timeStamp = currentTime.secPastEpoch + currentTime.nsec / 1.e9;
                    
                    pImage->uniqueId = arrayCounter;
                    updateTimeStamp(&pImage->epicsTS);
                    
                    /* Get attributes that have been defined for this driver */
                    getAttributes(pImage->pAttributeList);
                    
                    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW,"%s:%s: calling imageDataCallback\n", driverName, __func__);
                    
                    doCallbacksGenericPointer(pImage, NDArrayData, 0);
                }
                else {
                    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s error allocating buffer\n", driverName, __func__);
                    acquireStop();
                    setIntegerParam(ADStatus, ADStatusError);
                    callParamCallbacks();
                }
            }
//			printf("imageCounter %d numImages %d \n", imagesCounter, numImages);
            if (((imageMode == ADImageMultiple) && (imagesCounter == numImages)) || ((imageMode == ADImageSingle) && (imagesCounter == 1))) {
                asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Calling acquireStop()\n", driverName, __func__);
                lock();
                acquireStop();
                unlock();
				acquiring = 0; // so we stop this scan loop
            }
            
        }
   }
}


/* Code for iocsh registration */

/* uEyeCamConfig */
static const iocshArg SeaBreezeConfigArg0 = { "Port name", iocshArgString };
static const iocshArg SeaBreezeConfigArg1 = { "maxBuffers", iocshArgInt };
static const iocshArg SeaBreezeConfigArg2 = { "maxMemory", iocshArgInt };
static const iocshArg SeaBreezeConfigArg3 = { "priority", iocshArgInt };
static const iocshArg SeaBreezeConfigArg4 = { "stackSize", iocshArgInt };
static const iocshArg SeaBreezeConfigArg5 = { "serial number", iocshArgString };
static const iocshArg * const SeaBreezeConfigArgs[] = { &SeaBreezeConfigArg0,
    &SeaBreezeConfigArg1, &SeaBreezeConfigArg2, &SeaBreezeConfigArg3, &SeaBreezeConfigArg4, &SeaBreezeConfigArg5 };
    
static const iocshFuncDef configSeaBreeze = { "SeaBreezeSpecConfig", 6, SeaBreezeConfigArgs };

static void configSeaBreezeSpecCallFunc(const iocshArgBuf *args) {
    SeaBreezeSpecConfig(args[0].sval, args[1].ival, args[2].ival, args[3].ival,
                    args[4].ival, args[5].sval);
}

static void SeaBreezeSpecRegister(void) {
    iocshRegister(&configSeaBreeze, configSeaBreezeSpecCallFunc);
}

extern "C" {
    epicsExportRegistrar(SeaBreezeSpecRegister);
}


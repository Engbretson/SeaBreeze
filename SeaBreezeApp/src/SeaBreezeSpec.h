/**
 * Copyright (c) 2018, Helmholtz - Zentrum Dresden - Rossendorf
 * See LICENSE file.
 */
/* uEye.h
 * 
 * This is an areaDetector driver Spectrometers from OceanOptics
 * That can be accesed via the SeaBreeze API.
 *
 */
#ifndef SEABREEZESPEC__H
#define SEABREEZESPEC__H

#include <iocsh.h>

#include <epicsString.h>
#include <epicsEvent.h>
#include <epicsThread.h>

#include "ADDriver.h"

#define SB_HardwareRevisionString           "SB_HARDWAREREVISION"           // r, string
#define SB_HasShutterString                 "SB_HASSHUTTER"                 // r, int
#define SB_HasLightSourceString             "SB_HASLIGHTSOURCE"             // r, int
#define SB_HasLampString                    "SB_HASLAMP"                    // r, int
#define SB_HasAcquisitionDelayString        "SB_HASACQISITIONDELAY"         // r, int
#define SB_HasTemperatureString             "SB_HASTEMPERATURE"             // r, int 
#define SB_EnableLampString                 "SB_ENABLELAMP"                 // r/w, int
#define SB_MinimumIntegrationTimeString     "SB_MINIMUMINTEGRATIONTIME"     // r, long
#define SB_IntegrationTimeString            "SB_INTEGRATIONTIME"            // r/w, long
#define SB_UseFormatedSpectrumString        "SB_USEFORMATEDSPECTRUM"        // r/w, int
#define SB_FormatedSpectrumSizeString       "SB_FORMATEDSPECTRUMSIZE"       // r, int
#define SB_RawSpectrumSizeString            "SB_RAWSPECTRUMSIZE"            // r, int
#define SB_MaxIntensityString               "SB_MAXINTENSITY"               // r, float
#define SB_WaveLengthString                 "SB_WAVELENGTH"                 // r, double

class epicsShareClass SeaBreezeSpec : public ADDriver {
public:
    static const char *notAvailable;
    static const char *driverName;
    
    SeaBreezeSpec(const char *portName, int maxBuffers, size_t maxMemory, int priority, int stackSize, const char *id);
    ~SeaBreezeSpec();
    
    /* These are the methods that we override from asynPortDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus readFloat64(asynUser *pasynUser, epicsFloat64 *value);
    virtual asynStatus readFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements, size_t *nIn);
    
    void report(FILE *fp, int details);
    void SeaBreezeSpecHandleNewImageTask(void);
    
private:
    epicsMutex dataLock;
    NDArray *pImage;
    NDDataType_t  imageDataType;
    size_t imageDims[2];
    char *deviceId;
    long device;
    double *wavelength;
    long spectrometer;
    long *lamps;
    bool mAcquiringData;
    void *buffer = 0;
    epicsThreadId imageThreadId;
    epicsEventId  HandleNewImageEvent;
    bool imageThreadKeepAlive;// = true;
    int presetImages;
    
    asynStatus initializeDetector();
    asynStatus acquireStart();
    asynStatus acquireStop();
    
    int sb_HardwareRevision;
#define FIRST_SEABREEZE_PARAM sb_HardwareRevision
    int sb_HasLamp;
    int sb_HasShutter;
    int sb_HasLightSource;
    int sb_HasAcquisitionDelay;
    int sb_HasTemperature;
    int sb_EnableLamp;
    int sb_MinimumIntegrationTime;
    int sb_IntegrationTime;
    int sb_UseFormatedSpectrum;
    int sb_FormatedSpectrumSize;
    int sb_RawSpectrumSize;
    int sb_MaxIntensity;
    int sb_WaveLength;
#define LAST_SEABREEZE_PARAM sb_WaveLength
};

#define NUM_SEABREEZESPEC_PARAMS LAST_SEABREEZE_PARAM - FIRST_SEABREEZE_PARAM +1

#endif

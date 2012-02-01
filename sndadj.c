/*
This algorithm computes a series of looped filters that overlap in time.  There
is a step size which is the number of samples between filters, which should be
at least 1, and not larger than the shortest pitch period we expect to see, so
we know we have good overlap.  Start playing the first filter from it's 0 index,
and fade it out while fading in the second filter, playing starting from the
negative step size.  Ramp down the first filter and ramp up the second according
to the playback speed.  If normal speed, ramp down the first and the second up
evenly over the step size samples.  If 2X speed, ramp down the first and up the
second in half the step size.  If half speed, ramp down the first and up the
second over twice the step size.  The actual playback point can be a floating
point number, in between samples.  Simply copy samples to the output as hey are
crossed by the playback point

A reasonble heuristic may be to take a step size that is half the period.  This
guarantees decent overlap.
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "wave.h"

#define MIN_FREQ 65
#define MAX_FREQ 400
static int minPeriod, maxPeriod;
static float speed;
static int inputPos = 0, outputPos = 0;
static short *inputSamples, *outputSamples;
static int inputLength, outputLength;
static int period, nextPeriod;
static float *filter, *nextFilter;
static int filterPos = 0, nextFilterPos = 0;
static int sampleRate, numChannels;

static inline short clamp(
    float value)
{
    int intVal = round(value);

    if(intVal > 32767) {
        intVal = 32767;
    } else if(intVal < -32767) {
        intVal = -32767;
    }
    return (short)intVal;
}

// Find the best frequency match in the range.
static int findPitchPeriod(
    short *samples)
{
    int period, bestPeriod = 0;
    short *s, *p, sVal, pVal;
    long long diff, minDiff = 1;
    int i;

    for(period = minPeriod; period <= maxPeriod; period++) {
	diff = 0;
	s = samples;
	p = samples + period;
	for(i = 0; i < period; i++) {
	    sVal = *s++;
	    pVal = *p++;
	    diff += sVal >= pVal? sVal - pVal : pVal - sVal;
	}
	if(diff*bestPeriod < minDiff*period) {
	    minDiff = diff;
	    bestPeriod = period;
	}
    }
    return bestPeriod;
}

// Compute the filter at the new filter point, one period in the future from the
// playbackPos.
static void computeNextFilter(void)
{
    float *f = nextFilter;
    short *p = inputSamples + inputPos;
    short *q = inputSamples + inputPos + nextPeriod;
    int i;
    float ratio;

    for(i = 0; i < nextPeriod; i++) {
        ratio = i/(float)nextPeriod;
        *f++ = (ratio)*(*p++) + (1.0f - ratio)*(*q++);
    }
    // Now compute the next filter position.
    nextFilterPos = filterPos - nextPeriod;
    if(nextFilterPos < 0) {
        nextFilterPos += nextPeriod;
    }
    while(nextFilterPos >= nextPeriod) {
        nextFilterPos -= nextPeriod;
    }
}

// Ramp down the current filter while ramping up the next.
static void playFilters()
{
    int length = nextPeriod/speed;
    int i;
    float ratio;

    //printf("Playing %d samples for period of %d.\n", length, nextPeriod);
    for(i = 0; i < length; i++) {
        ratio = i/(float)length;
        outputSamples[outputPos++] = clamp((1.0f - ratio)*filter[filterPos] +
            ratio*nextFilter[nextFilterPos]);
        if(++filterPos == period) {
            filterPos = 0;
        }
        if(++nextFilterPos == nextPeriod) {
            nextFilterPos = 0;
        }
    }
    inputPos += nextPeriod;
}

//  Generate samples until the current playback point has passed the next filter
//  location.
static void generateSamplesForOneStep(void)
{
    float *temp;

    period = nextPeriod;
    temp = filter;
    filter = nextFilter;
    nextFilter = temp;
    filterPos = nextFilterPos;
    nextPeriod = findPitchPeriod(inputSamples + inputPos);
    computeNextFilter();
    playFilters();
}

// Play until out of input data.
static void playAll(void)
{
    while(inputPos + 2*maxPeriod < inputLength) {
        generateSamplesForOneStep();
    }
}

// Read an input wave file.
static void readWaveFile(
    char *fileName)
{
    int inputSize = 1024;
    int samplesRead;
    waveFile inFile = openInputWaveFile(fileName, &sampleRate, &numChannels);

    inputSamples = (short *)calloc(inputSize, sizeof(short));
    do {
        samplesRead = readFromWaveFile(inFile, inputSamples + inputLength, 1024);
        inputLength += samplesRead;
        if(inputLength + 1024 >= inputSize) {
            inputSize *= 2;
            inputSamples = (short *)realloc(inputSamples, inputSize*sizeof(short));
        }
    } while(samplesRead > 0);
    closeWaveFile(inFile);
}

// Write to the output wave file.
static void writeWaveFile(
    char *fileName)
{
    waveFile outFile = openOutputWaveFile(fileName, sampleRate, numChannels);
    writeToWaveFile(outFile, outputSamples, outputPos);
    closeWaveFile(outFile);
}

int main(int argc, char **argv)
{
    if(argc != 4) {
        printf("Usage: sndadj speed inWavFile outWavFile\n");
        return 1;
    }
    speed = atof(argv[1]);
    readWaveFile(argv[2]);
    minPeriod = sampleRate/MAX_FREQ;
    maxPeriod = sampleRate/MIN_FREQ;
    printf("Length = %d, sample rate = %d Hz\n", inputLength, sampleRate);
    period = minPeriod;
    filter = (float *)calloc(maxPeriod, sizeof(float));
    nextFilter = (float *)calloc(maxPeriod, sizeof(float));
    outputLength = inputLength/speed + 4096;
    outputSamples = (short *)calloc(outputLength, sizeof(short));
    playAll();
    writeWaveFile(argv[3]);
    return 0;
}

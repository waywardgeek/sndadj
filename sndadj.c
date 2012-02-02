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
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "wave.h"

#define MIN_FREQ 65
#define MAX_FREQ 135
static int minPeriod, maxPeriod;
static double speed;
static int inputPos = 0, outputPos = 0;
static double exactInputPos = 0.0;
static short *inputSamples, *outputSamples;
static int inputLength, outputLength;
static int period, prevPeriod, stepSize;
static double *filter, *prevFilter;
static int filterPos = 0, prevFilterPos = 0;
static int sampleRate, numChannels;
static bool prevPeriodVoiced = false;

#define min(a, b) ((a) <= (b)? (a) : (b))
#define max(a, b) ((a) >= (b)? (a) : (b))

// Find the best frequency match.  This routine looks for a pitch period just
// prior to the samples pointer which matches one just after it, so samples
// should be valid for at least maxPeriod samples as a negative index, as
// well as a positive index.
static int findPitchPeriod(
    short *samples)
{
    int period, bestPeriod = 0;
    short *s, *p, sVal, pVal;
    long long diff, minDiff = 1;
    long long totalDiff = 0, aveDiff;
    int i, start, stop;

    if(prevPeriodVoiced) {
        start = max(minPeriod, (prevPeriod*2)/3);
        stop = min(maxPeriod, (prevPeriod*3)/2);
    } else {
        start = minPeriod;
        stop = maxPeriod;
    }
    for(period = start; period <= stop; period++) {
	diff = 0;
	s = samples - period;
	p = samples;
	for(i = 0; i < period; i++) {
	    sVal = *s++;
	    pVal = *p++;
	    diff += sVal >= pVal? sVal - pVal : pVal - sVal;
	}
        totalDiff += diff/period;
	if(diff*bestPeriod < minDiff*period) {
	    minDiff = diff;
	    bestPeriod = period;
	}
    }
    aveDiff = totalDiff/(stop - start);
    printf("Period %d, minDiff %lld, aveDiff %lld", bestPeriod,
        minDiff/bestPeriod, aveDiff);
    prevPeriodVoiced = minDiff/bestPeriod <= aveDiff/2 && aveDiff > 100;
    if(prevPeriodVoiced) {
        printf(", voiced\n");
    } else {
        printf("\n");
    }
    return bestPeriod;
}

// Compute the filter at the next filter point, one step in the future from
// inputPos.
static void computeFilter(
    short *samples,
    int period)
{
    double *f = filter;
    short *p = samples - period;
    short *q = samples;
    int i;
    double ratio;

    for(i = 0; i < period; i++) {
        ratio = i/(double)period;
        *f++ = (ratio)*(*p++) + (1.0 - ratio)*(*q++);
    }
    // Now compute the filter position.
    filterPos = prevFilterPos - stepSize;
    while(filterPos < 0) {
        filterPos += period;
    }
    while(filterPos >= period) {
        filterPos -= period;
    }
}

// Ramp down the previous filter while ramping up the next.
static void playFilters()
{
    double ratio;
    int numGenerated = 0;

    do {
        ratio = (exactInputPos - inputPos)/stepSize;
        if(ratio < 0.0 || ratio > 1.0) {
            printf("Bad ratio = %f\n", ratio);
            exit(1);
        }
        outputSamples[outputPos++] = (1.0 - ratio)*prevFilter[prevFilterPos] + ratio*filter[filterPos];
        if(++prevFilterPos == prevPeriod) {
            prevFilterPos = 0;
        }
        if(++filterPos == period) {
            filterPos = 0;
        }
        numGenerated++;
        exactInputPos += speed;
    } while(exactInputPos - inputPos < stepSize);
    //printf("Generated %d samples for period of %d.\n", numGenerated, period);
}

// Generate samples until the current playback point has passed the next filter
// location.  We assume we have already computed the current filter and it's
// period, and now need to compute the step size and compute the new one.
static void generateSamplesForOneStep(void)
{
    double *temp;

    //stepSize = period/2;
    stepSize = period;
    prevPeriod = period;
    temp = prevFilter;
    prevFilter = filter;
    filter = temp;
    prevFilterPos = filterPos;
    period = findPitchPeriod(inputSamples + inputPos + stepSize);
    computeFilter(inputSamples + inputPos + stepSize, period);
    playFilters();
    inputPos += stepSize;
}

// Play until out of input data.
static void playAll(void)
{
    inputPos = maxPeriod; // Skip initial zeros.
    exactInputPos = maxPeriod;
    period = minPeriod;
    while(inputPos < inputLength) {
        generateSamplesForOneStep();
    }
}

// Read an input wave file.  This function pads the beginning and end with
// maxPeriod zeros.
static void readWaveFile(
    char *fileName)
{
    int inputSize = 1024 + 2*maxPeriod;
    int samplesRead;
    waveFile inFile = openInputWaveFile(fileName, &sampleRate, &numChannels);

    inputSamples = (short *)calloc(inputSize, sizeof(short));
    inputLength = maxPeriod;
    do {
        samplesRead = readFromWaveFile(inFile, inputSamples + inputLength, 1024);
        inputLength += samplesRead;
        if(inputLength + 1024 + 2*maxPeriod >= inputSize) {
            inputSize *= 2;
            inputSamples = (short *)realloc(inputSamples, inputSize*sizeof(short));
        }
    } while(samplesRead > 0);
    closeWaveFile(inFile);
    memset(inputSamples + inputLength, 0, maxPeriod);
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
    stepSize = minPeriod/2;
    prevFilter = (double *)calloc(maxPeriod, sizeof(double));
    filter = (double *)calloc(maxPeriod, sizeof(double));
    outputLength = inputLength/speed + 4096;
    outputSamples = (short *)calloc(outputLength, sizeof(short));
    playAll();
    writeWaveFile(argv[3]);
    return 0;
}

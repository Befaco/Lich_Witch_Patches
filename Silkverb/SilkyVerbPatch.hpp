#ifndef __SilkyVerbPatch_hpp__
#define __SilkyVerbPatch_hpp__

#include "Patch.h"
#include "DcFilter.hpp"
#include "CircularBuffer.hpp"
#include "TapTempo.hpp"

/**
 
AUTHOR:
    (c) 1994-2012  Robert Bristow-Johnson
    rbj@audioimagination.com
    (c) 2020       Martin Klang
    martin@rebeltech.org

 
LICENSE:
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 
 
DESCRIPTION:
    The core of the reverb algorithm implemented here is contained in
    a feedback "matrix" and a set of eight delay lines. This structure
    represents a generalized feedback network in which each delay line
    input receives a linear combination of each of the delay outputs and
    of the input signal to the reverberator.  It is based on the published
    work of Jot:
 
    Digital Delay Networks for Designing Artificial Reverberators
 
    Within the framework of Schroeder's parallel comb filter reverberator,
    a method is proposed for controlling the decay characteristics (avoiding
    unnatural resonances) and for compensating the frequency response. The
    method is extended to any recursive delay network having a unitary
    feedback matrix, and allows selection of the reverberator structure
    irrespective of reverberation time control.
 
    Author: Jot, Jean-Marc
    Affiliation: Antoine Chaigne, Enst, departement SIGNAL, Paris, France
    AES Convention: 90 (February 1991)   Preprint Number:3030

UPDATES:
    2020 Martin Klang: Refactored. Cross-fade delay positions for smooth size changes. 
                       Tap tempo pre-delay.
*/

#define MAX_REVERB_TIME   16
#define MIN_REVERB_TIME   0.8
#define MAX_ROOM_SIZE     7552
#define MIN_ROOM_SIZE     192
#define MAX_CUTOFF        0.4975
#define MIN_CUTOFF        0.1134
#define MAX_PREDELAY_SIZE 32768
#define MIN_PREDELAY_SIZE 0

#define SQRT8             2.82842712474619  // sqrtf(8)
#define ONE_OVER_SQRT8    0.353553390593274 //  1/sqrtf(8)
#define ALPHA             0.943722057435498 //  powf(3/2, -1/(8-1))
//       of the 8 delay lines, the longest is 3/2 times longer than the shortest.
//       the longest delay is coupled to the room size.
//       the delay lines then decrease exponentially in length.

#define PRIME_NUMBER_TABLE_SIZE 7600
uint32_t  primeNumberTable[PRIME_NUMBER_TABLE_SIZE];
// the 7600th prime is 77351

#define BUFFER_LIMIT 8192
#define TRIGGER_LIMIT 65536

void BuildPrimeTable(uint32_t* prime_number_table){
  uint16_t max_stride = (uint16_t)sqrtf(PRIME_NUMBER_TABLE_SIZE);
  for(size_t i=0; i<PRIME_NUMBER_TABLE_SIZE; i++)
    prime_number_table[i] = 1;         // initial value of all entries is 1 
  prime_number_table[0] = 0;          // now we zero out any entry that is not prime
  prime_number_table[1] = 0;
  uint16_t stride = 2;             // start with stride set to the smallest prime
  while (stride <= max_stride){
    for(size_t i=2*stride; i<PRIME_NUMBER_TABLE_SIZE; i+=stride) // start at the 2nd multiple of this prime, NOT the prime number itself!!!
      prime_number_table[i] = 0;        // zero out table entries for all multiples of this prime number
    stride++;
    while (prime_number_table[stride] == 0)      // go to next non-zero entry which is the next prime
      stride++;
  }
}

uint32_t FindNearestPrime(uint32_t* prime_number_table, uint16_t number)
{
  while (prime_number_table[number] == 0)
    number--;
  return number;
}

class CrossFadeBuffer : public CircularBuffer {
private:
  int readIndex = 0;
public:
  CrossFadeBuffer(){}
  CrossFadeBuffer(FloatArray buf) : CircularBuffer(buf){}

  void fade(int readIndex, FloatArray destination){
    fade(readIndex, destination.getData(), destination.getSize());
  }
  void fade(int newReadIndex, float* destination, size_t len){
    for(size_t i=0; i<len; i++){
      float x1 = i/(float)len;
      float x0 = 1.0-x1;
      *destination++ = read(readIndex+len-i)*x0 + read(newReadIndex+len-i)*x1;
    }
    readIndex = newReadIndex;
  }
  static CrossFadeBuffer* create(int samples){
    return new CrossFadeBuffer(FloatArray::create(samples));
  }

  static void destroy(CrossFadeBuffer* buf){
    CircularBuffer::destroy(buf);
  }
};

class Node {
private:
  size_t delay_samples;
  float b0, a1, y1;
  FloatArray result;
  CrossFadeBuffer* buffer;
public:
  Node(size_t bufsize):
    a1(0), b0(-ONE_OVER_SQRT8), y1(0) {
    result = FloatArray::create(bufsize);
    buffer = CrossFadeBuffer::create(BUFFER_LIMIT);
  }
  ~Node(){
    FloatArray::destroy(result);
    CrossFadeBuffer::destroy(buffer);
  }
  float* getResult(){
    return result.getData();
  }
  void write(float sample){
    buffer->write(sample);
  }
  float filter(float x){
    y1 = b0*x + a1*y1; // b0*x[n] + a1*y[n-1]
    return y1;
  }
  void set(float beta, float fDelaySamples, float fCutoffCoef){
    float prime_value = FindNearestPrime(primeNumberTable, (int)fDelaySamples);
    // we subtract 1 CHUNK of delay, because this signal feeds back, causing an extra CHUNK delay
    delay_samples = prime_value - result.getSize();
    a1 = prime_value*fCutoffCoef;
    b0 = ONE_OVER_SQRT8*expf(beta*prime_value)*(a1-1);
  }
  void process(){
    buffer->fade(delay_samples, result);
    for(size_t i=0; i<result.getSize(); ++i)
      result[i] = filter(result[i]);
  }
};

class SilkyVerbPatch : public Patch {
  TapTempo<TRIGGER_LIMIT> tempo;
  int tempocounter;
  StereoDcFilter dc;
  CrossFadeBuffer* delayBufferL;
  CrossFadeBuffer* delayBufferR;
  FloatArray preL, preR;
  float fPreDelaySamples;

  float   dry_coef;
  float   wet_coef0;
  float   wet_coef1;
  float   left_reverb_state;
  float   right_reverb_state;

  Node node0;
  Node node1;
  Node node2;
  Node node3;
  Node node4;
  Node node5;
  Node node6;
  Node node7;

  FloatParameter size;
  FloatParameter time;
  FloatParameter cutoff;
  FloatParameter wet;

public:
  SilkyVerbPatch() : tempo(getSampleRate()*60/120),
		     node0(getBlockSize()),
		     node1(getBlockSize()),
		     node2(getBlockSize()),
		     node3(getBlockSize()),
		     node4(getBlockSize()),
		     node5(getBlockSize()),
		     node6(getBlockSize()),
		     node7(getBlockSize()) {
    delayBufferL = CrossFadeBuffer::create(MAX_PREDELAY_SIZE);
    delayBufferR = CrossFadeBuffer::create(MAX_PREDELAY_SIZE);
    preL = FloatArray::create(getBlockSize());
    preR = FloatArray::create(getBlockSize());

    static const float delta = 0.05;
    size = getFloatParameter("Size", MIN_ROOM_SIZE, MAX_ROOM_SIZE);
    time = getFloatParameter("Time", MIN_REVERB_TIME, MAX_REVERB_TIME);
    cutoff = getFloatParameter("Brightness", MIN_CUTOFF, MAX_CUTOFF);
    wet = getFloatParameter("Dry/Wet", 0, 1.0, 0.5);
    registerParameter(PARAMETER_E, "Pre-delay");
    registerParameter(PARAMETER_F, "LFO Sine>");
    registerParameter(PARAMETER_G, "LFO Ramp>");
    
    left_reverb_state = 0.0;
    right_reverb_state = 0.0;
 
    BuildPrimeTable(primeNumberTable);
  }

  ~SilkyVerbPatch(){
    CrossFadeBuffer::destroy(delayBufferL);
    CrossFadeBuffer::destroy(delayBufferR);
    FloatArray::destroy(preL);
    FloatArray::destroy(preR);
  }

  int delaySamples(){
    uint32_t time = tempo.getPeriod()*TRIGGER_LIMIT;
    while(time > MAX_PREDELAY_SIZE)
      time >>= 1;
    while(time < MIN_PREDELAY_SIZE)
      time <<= 1;
    return time;
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples){
    bool set = value != 0;
    switch(bid){
    case BUTTON_A:
      tempo.trigger(set, samples);
      setButton(PUSHBUTTON, value);
      tempocounter = 0;
      break;
    case BUTTON_B:
      if(set)
	tempo.setLimit(0); // set pre-delay to zero
      break;
    }
  }
    
  void processAudio(AudioBuffer &buffer){
    FloatArray left_input = buffer.getSamples(0);
    FloatArray right_input = buffer.getSamples(1);
    size_t len = buffer.getSize();
    tempo.clock(len);
    tempo.setSpeed(getParameterValue(PARAMETER_E)*4096);
    dc.process(buffer); // remove DC offset

    float fCutoffCoef  = expf(-6.28318530717959*cutoff);
    float fRoomSizeSamples = size;
    float fReverbTimeSamples = time*getSampleRate();
    
    dry_coef = 1.0 - wet;
    if(wet > 0){
      float dryWet = wet * SQRT8 * (1.0 - expf(-10*fRoomSizeSamples/(fReverbTimeSamples*0.125)));
      // additional attenuation for small room and long reverb time  <--  expf(-13.8155105579643) = 10^(-60dB/10dB)
      // gain compensation: toss in whatever fudge factor you need here to make the reverb louder
      wet_coef0 = dryWet;
      wet_coef1 = -fCutoffCoef*dryWet;
    }else{
      wet_coef0 = 0;
      wet_coef1 = 0;
    }
    
    fCutoffCoef /= (float)FindNearestPrime(primeNumberTable, fRoomSizeSamples);
 
    float fDelaySamples = fRoomSizeSamples;

    // 6.90775527898214 = logf(10^(60dB/20dB))  <-- fReverbTime is RT60
    float beta = -6.90775527898214/fReverbTimeSamples;
  
    node0.set(beta, fDelaySamples, fCutoffCoef);
    fDelaySamples *= ALPHA;
    node1.set(beta, fDelaySamples, fCutoffCoef);
    fDelaySamples *= ALPHA;
    node2.set(beta, fDelaySamples, fCutoffCoef);
    fDelaySamples *= ALPHA;
    node3.set(beta, fDelaySamples, fCutoffCoef);
    fDelaySamples *= ALPHA;
    node4.set(beta, fDelaySamples, fCutoffCoef);
    fDelaySamples *= ALPHA;
    node5.set(beta, fDelaySamples, fCutoffCoef);
    fDelaySamples *= ALPHA;
    node6.set(beta, fDelaySamples, fCutoffCoef);
    fDelaySamples *= ALPHA; 
    node7.set(beta, fDelaySamples, fCutoffCoef);

    fPreDelaySamples = delaySamples();
    delayBufferL->write(left_input);
    delayBufferR->write(right_input);
    delayBufferL->fade(fPreDelaySamples, preL);
    delayBufferR->fade(fPreDelaySamples, preR);

    tempocounter += len;
    if(fPreDelaySamples && tempocounter >= fPreDelaySamples){
      tempocounter -= fPreDelaySamples;
      setButton(PUSHBUTTON, 4095);
    }else if(tempocounter > fPreDelaySamples/4){
      setButton(PUSHBUTTON, 0);
    }
    
    float* x0 = node0.getResult(); // lpf output from previous block
    float* x1 = node1.getResult();
    float* x2 = node2.getResult();
    float* x3 = node3.getResult();
    float* x4 = node4.getResult();
    float* x5 = node5.getResult();
    float* x6 = node6.getResult();
    float* x7 = node7.getResult();

    for(size_t i=0; i<len; i++){
      float acc = preL[i];
      acc += x0[i];
      acc += x1[i];
      acc += x2[i];
      acc += x3[i];
      acc -= x4[i];
      acc -= x5[i];
      acc -= x6[i];
      acc -= x7[i];
      node0.write(acc); // delay input
    }

    for(size_t i=0; i<len; i++){
      float acc = preR[i];
      acc += x0[i];
      acc += x1[i];
      acc -= x2[i];
      acc -= x3[i];
      acc += x4[i];
      acc += x5[i];
      acc -= x6[i];
      acc -= x7[i];
      node1.write(acc);
    }
 
    for(size_t i=0; i<len; i++){
      float acc = preR[i];
      acc += x0[i];
      acc += x1[i];
      acc -= x2[i];
      acc -= x3[i];
      acc -= x4[i];
      acc -= x5[i];
      acc += x6[i];
      acc += x7[i];
      node2.write(acc);
    }

    for(size_t i=0; i<len; i++){
      float acc = preL[i];
      acc += x0[i];
      acc -= x1[i];
      acc += x2[i];
      acc -= x3[i];
      acc += x4[i];
      acc -= x5[i];
      acc += x6[i];
      acc -= x7[i];
      node3.write(acc);
    }
 
    for(size_t i=0; i<len; i++){
      float acc = preR[i];
      acc += x0[i];
      acc -= x1[i];
      acc += x2[i];
      acc -= x3[i];
      acc -= x4[i];
      acc += x5[i];
      acc -= x6[i];
      acc += x7[i];
      node4.write(acc);
    }

    for(size_t i=0; i<len; i++){
      float acc = preL[i];
      acc += x0[i];
      acc -= x1[i];
      acc -= x2[i];
      acc += x3[i];
      acc += x4[i];
      acc -= x5[i];
      acc -= x6[i];
      acc += x7[i];
      node5.write(acc);
    }

    for(size_t i=0; i<len; i++){
      float acc = preL[i];
      acc += x0[i];
      acc -= x1[i];
      acc -= x2[i];
      acc += x3[i];
      acc -= x4[i];
      acc += x5[i];
      acc += x6[i];
      acc -= x7[i];
      node6.write(acc);
    }
 
    for(size_t i=0; i<len; i++){
      float acc = preR[i];
      acc += x0[i];
      acc += x1[i];
      acc += x2[i];
      acc += x3[i];
      acc += x4[i];
      acc += x5[i];
      acc += x6[i];
      acc += x7[i];
      node7.write(acc);
    }
 
    float* input = left_input;
    float* output = left_input;
    float reverb_output_state = left_reverb_state;
    float rms = 0;
    for (int i=0; i<len; ++i){
      float output_acc = dry_coef * (*input++);
      float reverb_output = *(x0++) + *(x2++) + *(x4++) + *(x6++);
      output_acc += wet_coef0 * reverb_output;
      output_acc += wet_coef1 * reverb_output_state;
      *output++ = output_acc;
      reverb_output_state = reverb_output;
      rms += reverb_output*reverb_output;
    }
    left_reverb_state = reverb_output_state; 
    setParameterValue(PARAMETER_F, sqrtf(rms/len));
 
    input = right_input;
    output = right_input;
    reverb_output_state = right_reverb_state;
    rms = 0;
    for (int i=0; i<len; ++i){
      float reverb_output = *(x1++) + *(x3++) + *(x5++) + *(x7++);
      float output_acc = dry_coef * (*input++);
      output_acc += wet_coef0 * reverb_output;
      output_acc += wet_coef1 * reverb_output_state;
      *output++ = output_acc;
      reverb_output_state = reverb_output;
      rms += reverb_output*reverb_output;
    }
    right_reverb_state = reverb_output_state;
    setParameterValue(PARAMETER_G, sqrtf(rms/len));

    node0.process();
    node1.process();
    node2.process();
    node3.process();
    node4.process();
    node5.process();
    node6.process();
    node7.process();
  }
};

#endif // __SilkyVerbPatch_hpp__

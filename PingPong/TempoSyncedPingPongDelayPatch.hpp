#ifndef __TempoSyncedPingPongDelayPatch_hpp__
#define __TempoSyncedPingPongDelayPatch_hpp__

/**
 
AUTHOR:
    (c) 2020 Martin Klang
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
    Ping pong delay with tap tempo and adjustable ratio. The delay time is 
    the product of the current tempo and ratio. Delay times up to 2.73 seconds, 
    or about 20 BPM at 1/1.

    Tap button to set the tempo. Adjust with Tempo knob. Ratio sets a musical 
    divisor or multiplier, from 1/4 to 4. The left channel delay time is twice 
    as long as the right channel.
    Button A is used for tap tempo. Button B enables 'loop' mode.
    The trigger output clocks out the current tempo, while output 
    parameters F and G outputs a sine and ramp LFO respectively, both 
    synchronised to the current tempo.

*/

#include "Patch.h"
#include "DcFilter.hpp"
#include "BiquadFilter.h"
#include "CircularBuffer.hpp"
#include "TapTempo.hpp"
#include "SineOscillator.h"
#include "RampOscillator.h"
#include "SmoothValue.h"

static const int RATIOS_COUNT = 9;
static const float ratios[RATIOS_COUNT] = { 1.0/4, 
					    1.0/3, 
					    1.0/2, 
					    3.0/4, 
					    1.0, 
					    3.0/2, 
					    2.0,
					    3.0, 
					    4.0 };

static const uint32_t counters[RATIOS_COUNT] = { 1, 
						 1, 
						 1, 
						 1, 
						 1, 
						 3, 
						 2,
						 3, 
						 4 };

class TempoSyncedPingPongDelayPatch : public Patch {
private:
  static const int TRIGGER_LIMIT = (1<<17);
  CircularBuffer* delayBufferL;
  CircularBuffer* delayBufferR;
  int delayL, delayR, ratio;
  TapTempo<TRIGGER_LIMIT> tempo;
  StereoDcFilter dc;
  StereoBiquadFilter* lowpass;
  RampOscillator* lfo1;
  SineOscillator* lfo2;
  SmoothFloat time;
  SmoothFloat drop;
  SmoothFloat feedback;
public:
  TempoSyncedPingPongDelayPatch() : 
    delayL(0), delayR(0), tempo(getSampleRate()*60/120) {
    registerParameter(PARAMETER_A, "Tempo");
    registerParameter(PARAMETER_B, "Feedback");
    registerParameter(PARAMETER_C, "Ratio");
    registerParameter(PARAMETER_D, "Dry/Wet");
    registerParameter(PARAMETER_F, "LFO Sine>");
    registerParameter(PARAMETER_G, "LFO Ramp>");
    delayBufferL = CircularBuffer::create(TRIGGER_LIMIT);
    delayBufferR = CircularBuffer::create(TRIGGER_LIMIT*2);
    lowpass = StereoBiquadFilter::create(1);
    lowpass->setLowPass(18000/(getSampleRate()/2), FilterStage::BUTTERWORTH_Q);
    lfo1 = RampOscillator::create(getSampleRate()/getBlockSize());    
    lfo2 = SineOscillator::create(getSampleRate()/getBlockSize());    
  }

  ~TempoSyncedPingPongDelayPatch(){
    CircularBuffer::destroy(delayBufferL);
    CircularBuffer::destroy(delayBufferR);
    StereoBiquadFilter::destroy(lowpass);
    RampOscillator::destroy(lfo1);
    SineOscillator::destroy(lfo2);
  }

  float delayTime(int ratio){
    float time = tempo.getPeriod() * ratios[ratio];
    time = max(0.0001, min(0.9999, time));
    return time;
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples){
    bool set = value != 0;
    static uint32_t counter = 0;
    switch(bid){
    case BUTTON_A:
      tempo.trigger(set, samples);
      if(set && ++counter >= counters[ratio]){
	lfo1->reset();
	counter = 0;
      }
      break;
    }
  }
  
  void processAudio(AudioBuffer& buffer){
    int speed = getParameterValue(PARAMETER_A)*4096;
    if(isButtonPressed(BUTTON_B)){
      feedback = 1.0;
      drop = 0.0;
    }else{
      feedback = getParameterValue(PARAMETER_B);
      drop = 1.0;
    }
    ratio = (int)(getParameterValue(PARAMETER_C) * RATIOS_COUNT);
    int size = buffer.getSize();
    tempo.clock(size);
    tempo.setSpeed(speed);
    time = delayTime(ratio);
    int newDelayL = time*(delayBufferL->getSize()-1);
    int newDelayR = time*(delayBufferR->getSize()-1);
    float wet = getParameterValue(PARAMETER_D);
    float dry = 1.0-wet;
    FloatArray left = buffer.getSamples(LEFT_CHANNEL);
    FloatArray right = buffer.getSamples(RIGHT_CHANNEL);
    dc.process(buffer); // remove DC offset
    for(int n=0; n<size; n++){
      float x1 = n/(float)size;
      float x0 = 1.0-x1;
      float ldly = delayBufferL->read(delayL)*x0 + delayBufferL->read(newDelayL)*x1;
      float rdly = delayBufferR->read(delayR)*x0 + delayBufferR->read(newDelayR)*x1;
      // ping pong
      delayBufferR->write(feedback*ldly + drop*left[n]);
      delayBufferL->write(feedback*rdly + drop*right[n]);
      left[n] = ldly*wet + left[n]*dry;
      right[n] = rdly*wet + right[n]*dry;
    }
    lowpass->process(buffer);
    left.tanh();
    right.tanh();
    delayL = newDelayL;
    delayR = newDelayR;
    // Tempo synced LFO
    float lfoFreq = getSampleRate()/(time*TRIGGER_LIMIT);
    lfo1->setFrequency(lfoFreq);
    lfo2->setFrequency(lfoFreq);
    setParameterValue(PARAMETER_F, lfo1->getNextSample());
    setParameterValue(PARAMETER_G, lfo2->getNextSample()*0.5+0.5);
    setButton(PUSHBUTTON, lfo1->getPhase() < 0.5);
  }
};

#endif   // __TempoSyncedPingPongDelayPatch_hpp__

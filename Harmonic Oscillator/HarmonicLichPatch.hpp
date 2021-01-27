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
    Harmonic Lich is a CV and MIDI controlled harmonic oscillator.
    Eight sine wave oscillators are tuned to the harmonic series, and the 
    level of each can be controlled by parameters AA to AH (or MIDI).
    With parameters A and B you control the root note played and detune it.
    The left audio input gives 1v/oct input CV control of the frequency.
    Parameter C will change the harmonic center, and D controls how high 
    and low frequencies are attenuated. 
    Buttons A and B mute the odd and even harmonics respectively.
    MIDI Note messages control the root note.
    Output parameters F and G reflect the total signal level within the 
    oscillator at any time.
*/

#include "Patch.h"
#include "Envelope.h"
#include "VoltsPerOctave.h"
#include "SmoothValue.h"
#include "SineOscillator.h"

#define USE_FM
#define TONES 8
// Up to 16 harmonics supported
static const char* names[] = { "H1", "H2", "H3", "H4", "H5", "H6", "H7", "H8",
			       "H9", "H10", "H11", "H12", "H13", "H14", "H15", "H16" };


class HarmonicLichPatch : public Patch {
private:
  Oscillator* osc[TONES];
  float levels[TONES];
  bool mutes[TONES];
  FloatArray mix;
  FloatArray ramp;
  VoltsPerOctave hz;
  float gainadjust = 0.0f;
  StiffFloat semitone;
  int centernote = 0;
  const float NYQUIST;
public:
  HarmonicLichPatch() : hz(true), NYQUIST(getSampleRate()/2) {
    registerParameter(PARAMETER_A, "Semitone");
    registerParameter(PARAMETER_B, "Fine Tune");
    registerParameter(PARAMETER_C, "Centre");
    registerParameter(PARAMETER_D, "Peak");
    setParameterValue(PARAMETER_A, 0.5);
    setParameterValue(PARAMETER_B, 0.5);
    setParameterValue(PARAMETER_C, 0.5);
    setParameterValue(PARAMETER_D, 0.5);
#ifdef USE_FM
    registerParameter(PARAMETER_E, "FM Amount");
    setParameterValue(PARAMETER_E, 0.0);
#endif
    registerParameter(PARAMETER_F, "Overflow>");
    registerParameter(PARAMETER_G, "Intensity>");
    for(int i=0; i<TONES; i++){
      osc[i] = SineOscillator::create(getSampleRate());
      registerParameter(PatchParameterId(PARAMETER_AA+i), names[i]);
      setParameterValue(PatchParameterId(PARAMETER_AA+i), 0.50);
      levels[i] = 1;
      mutes[i] = false;
    }
    mix = FloatArray::create(getBlockSize());
    ramp = FloatArray::create(getBlockSize());
    semitone.delta = 0.5;
  }

  ~HarmonicLichPatch(){
    for(int i=0; i<TONES; i++)
      SineOscillator::destroy((SineOscillator*)osc[i]);
    FloatArray::destroy(mix);
    FloatArray::destroy(ramp);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples){
    switch(bid){
    case BUTTON_A:
      for(int i=0; i<TONES; i+= 2)
	mutes[i] = value;
      break;
    case BUTTON_B:
      for(int i=1; i<TONES; i+= 2)
	mutes[i] = value;
      break;
    }
  }
  void processMidi(MidiMessage msg){
    if(msg.isControlChange()){
      uint8_t id = msg.getControllerNumber() - PATCH_PARAMETER_AA;
      if(id < TONES)
	setParameterValue(PatchParameterId(PARAMETER_AA+id), msg.getControllerValue()/127.0f);
      else if(msg.getControllerNumber() == PATCH_BUTTON)
	buttonChanged(BUTTON_A, msg.getControllerValue(), 0);
    }else if(msg.isNoteOn()){
      centernote = msg.getNote()-60;
    }
  }

  void processAudio(AudioBuffer& buf){
    semitone = getParameterValue(PARAMETER_A)*56-56;
    float freq = round(semitone+centernote)/12 + getParameterValue(PARAMETER_B)/6;
    float centre = getParameterValue(PARAMETER_C)*(TONES-1);
    float a, r;
    float d = getParameterValue(PARAMETER_D);
    if(d < 0.20){       /* //.\\ */
      a = 1-d*5;
      r = 1;
    }else if(d < 0.45){ /* --.\\ */
      a = 0;
      r = 1-(d-0.20)*4;
    }else if(d < 0.55){ /* --.-- */
      a = 0;
      r = 0;
    }else if(d < 0.80){ /* --.-- */
      a = (d-0.55)*4;
      r = 0;
    }else{              /* //.-- */      
      a = 1;
      r = (d-0.80)*5;      
    }                   /* //.\\ */
    float fm = getParameterValue(PARAMETER_E)*0.2;
    FloatArray left = buf.getSamples(LEFT_CHANNEL);
    FloatArray right = buf.getSamples(RIGHT_CHANNEL);
    hz.setTune(freq);
    float fundamental = hz.getFrequency(left[0]);
    right.multiply(fm);
    float newgainadjust = 0;
    left.clear();
    for(int i=0; i<TONES; i++){
      float newlevel = getParameterValue(PatchParameterId(PARAMETER_AA+i));
      float distance = abs(centre - i);
      float duck = i < centre ? a*distance : r*distance;
      newlevel = mutes[i] ? 0 : max(0, min(1, newlevel*(1-duck)));
      ramp.ramp(levels[i], newlevel);
      levels[i] = newlevel;
      newgainadjust += newlevel;
      freq = fundamental*(i+1);
      if(freq > 10 && freq < NYQUIST){
	osc[i]->setFrequency(freq);
#ifdef USE_FM
	osc[i]->getSamples(mix, right);
#else
	osc[i]->getSamples(mix);
#endif
	mix.multiply(ramp);
	left.add(mix);
      }
    }
    newgainadjust = newgainadjust > 1 ? 1/newgainadjust : 1;
    ramp.ramp(gainadjust, newgainadjust);
    left.multiply(ramp);
    left.multiply(0.5);
    gainadjust = newgainadjust;
    setParameterValue(PARAMETER_F, gainadjust);
    setParameterValue(PARAMETER_G, 1-gainadjust);
    right.copyFrom(left);
  }

};

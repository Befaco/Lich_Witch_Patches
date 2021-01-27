#ifndef __MidiModularPatch_hpp__
#define __MidiModularPatch_hpp__
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
    CV to MIDI and MIDI to CV converter, with digital FM.

    CV to MIDI:
    - 1v/oct pitch on L in
    - +/-5V pitchbend on R in
    - button 1 to trigger Note
    - Parameter A converts to CC 1 Modulation
    - Parameter B converts to CC 11 Expression

    MIDI to CV:
    - pitch on L out
    - pitchbend on R out
    - gate on Gate Out
    - CC 1 Modulation on CV Out 1
    - CC 11 Expression to CV Out 2
    - Parameters C and D adds FM sine osc to pitch output
*/

#include "Patch.h"
#include "SineOscillator.h"
#include "VoltsPerOctave.h"

// #define ROOT_NOTE 69 // A4
#define ROOT_NOTE 33 // A1
#define ROOT_NOTE_OFFSET (ROOT_NOTE-69)

class MonoVoiceAllocator {
    float& freq;
    float& gain;
    float& gate;
    float& bend;
    uint8_t notes[16];
    uint8_t lastNote = 0;
public:
    MonoVoiceAllocator(float& fq, float& gn, float& gt, float& bd)
        : freq(fq)
        , gain(gn)
        , gate(gt)
        , bend(bd) {
    }
    float getFreq() {
        return freq;
    }
    float getGain() {
        return gain;
    }
    float getGate() {
        return gate;
    }
    float getBend() {
        return bend;
    }
    void processMidi(MidiMessage msg) {
        uint16_t samples = 0;
        if (msg.isNoteOn()) {
            noteOn(msg.getNote(), (uint16_t)msg.getVelocity() << 5, samples);
        }
        else if (msg.isNoteOff()) {
            noteOff(msg.getNote(), (uint16_t)msg.getVelocity() << 5, samples);
        }
        else if (msg.isPitchBend()) {
            setPitchBend(msg.getPitchBend());
        }
        else if (msg.isControlChange()) {
            if (msg.getControllerNumber() == MIDI_ALL_NOTES_OFF)
                allNotesOff();
        }
    }
    void setPitchBend(int16_t pb) {
        float fb = pb * (2.0f / 8192.0f);
        bend = exp2f(fb);
    }
    float noteToHz(uint8_t note) {
        return 440.0f * exp2f((note - ROOT_NOTE) / 12.0);
    }
    float velocityToGain(uint16_t velocity) {
        return exp2f(velocity / 4095.0f) - 1;
    }
    void noteOn(uint8_t note, uint16_t velocity, uint16_t delay) {
        if (lastNote < 16)
            notes[lastNote++] = note;
        freq = noteToHz(note);
        gain = velocityToGain(velocity);
        gate = 1.0f;
    }
    void noteOff(uint8_t note, uint16_t velocity, uint16_t delay) {
        int i;
        for (i = 0; i < lastNote; ++i) {
            if (notes[i] == note)
                break;
        }
        if (lastNote > 1) {
            lastNote--;
            while (i < lastNote) {
                notes[i] = notes[i + 1];
                i++;
            }
            freq = noteToHz(notes[lastNote - 1]);
        }
        else {
            gate = 0.0f;
            lastNote = 0;
        }
    }
    void allNotesOff() {
        lastNote = 0;
        bend = 0;
    }
};

static float fFreq, fGain, fGate;
static float fBend = 1.0f;

MonoVoiceAllocator allocator(fFreq, fGain, fGate, fBend);

class State {
public:
  int channel = 0;
  int note = ROOT_NOTE;
  int velocity = 0;
  float freq = 0;
  float pitchbend = 0;
  int modulation = 0;
  int expression = 0;
};

class MidiModularPatch : public Patch {
private:
  SineOscillator osc;
  FloatArray fm;
  VoltsPerOctave voltsOut;
  VoltsPerOctave voltsIn;
  State in;
  State out;
  float saveLeft = 0;
  float saveRight = 0;
public:
  MidiModularPatch() : voltsIn(true), voltsOut(false) {
    osc.setSampleRate(getSampleRate());
    fm = FloatArray::create(getBlockSize());
    registerParameter(PARAMETER_A, "Modulation");
    registerParameter(PARAMETER_B, "Expression");
    registerParameter(PARAMETER_C, "FM Freq");
    registerParameter(PARAMETER_D, "FM Amount");
    registerParameter(PARAMETER_F, "Modulation>");
    registerParameter(PARAMETER_G, "Expression>");
  }

  void processMidi(MidiMessage msg){
    switch(msg.getStatus()) {
    case NOTE_OFF:
      msg.data[3] = 0;
      // deliberate fall-through
    case NOTE_ON:
      if(!in.velocity){
	in.note = msg.getNote();
	in.velocity = msg.getVelocity();
      }else if(msg.getNote() == in.note){
	in.velocity = 0;
      }
      break;
    case PITCH_BEND_CHANGE:
      in.pitchbend = msg.getPitchBend()/8192.0f;
      break;
    case CONTROL_CHANGE:
      switch(msg.getControllerNumber()){
      case MIDI_CC_MODULATION:
	in.modulation = msg.getControllerValue();
	break;
      case MIDI_CC_EXPRESSION:
	in.expression = msg.getControllerValue();
	break;
      }
    }
  }
  
  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples){
    switch(bid){
    case BUTTON_A:
      out.velocity = value ? 80 : 0;
      sendMidi(MidiMessage::note(out.channel, out.note, out.velocity));
      break;
    case BUTTON_B:
      break;
    }
  }

  void processAudio(AudioBuffer &buffer) {
    FloatArray left = buffer.getSamples(LEFT_CHANNEL);
    FloatArray right = buffer.getSamples(RIGHT_CHANNEL);

    // CV to MIDI
    out.freq = voltsIn.getFrequency(left.getMean());
    if(out.velocity == 0)
      out.note = voltsIn.hertzToNote(out.freq) + ROOT_NOTE_OFFSET;
    int pb = right.getMean()*8192;
    if(pb != out.pitchbend){
      sendMidi(MidiMessage::pb(out.channel, pb));
      out.pitchbend = pb;
    }
    int cc = getParameterValue(PARAMETER_A)*127;
    if(cc != out.modulation){
      sendMidi(MidiMessage::cc(out.channel, MIDI_CC_MODULATION, cc));
      out.modulation = cc;
    }
    cc = getParameterValue(PARAMETER_B)*127;
    if(cc != out.expression){
      sendMidi(MidiMessage::cc(out.channel, MIDI_CC_EXPRESSION, cc));
      out.expression = cc;
    }

    // MIDI to CV
    in.freq = voltsOut.noteToHertz(in.note - ROOT_NOTE_OFFSET);
    float value = voltsOut.getSample(in.freq);
    left.ramp(saveLeft, value);
    saveLeft = value;
    right.ramp(saveRight, in.pitchbend);
    saveRight = in.pitchbend;
    setParameterValue(PARAMETER_F, in.modulation/127.0f);
    setParameterValue(PARAMETER_G, in.expression/127.0f);
    if(in.velocity)
      setButton(PUSHBUTTON, 4095, 0);      
    else
      setButton(PUSHBUTTON, 0, 0);

    // add a little oscillation
    osc.setFrequency(voltsOut.noteToHertz(in.note+round(getParameterValue(PARAMETER_C)*24)-12));
    osc.getSamples(fm);
    fm.multiply(getParameterValue(PARAMETER_D)*0.2);
    left.add(fm);
  }
};

#endif   // __MidiModularPatch_hpp__

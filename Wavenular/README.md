# Wavenular Beta 1
Patch by Death Whistle <br/>
Dual wavetable oscillator based on audio input with vca, decay envelope, state variable filter and midi implementation.

- Button_1: Create new WaveForm
- Button_2: Gate in 
- Pot_A: Frequency Osc_1
- Pot_B: Frequency Osc_2
- Pot_C: Decay envelope (5sec)
- Pot_D: State variable filter (LP->HP)
....
- Gate Out: Pass through of Gate in (Button_2 & Midi_Note_On)
- CV_OUT_1: Frequecy Osc_1 (patch to cvB and sync the freq of the two osc)
- CV_OUT_2: Midi_Note
	
ToDo:
- Improve Frequency tracking
- Improve Filters, add resonance
- Implement Several osc and wavetables to charge randomly if there is nothing in the input



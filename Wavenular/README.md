# Wavenular Beta 1
Patch by Death Whistle <br/>
Dual oscillator based on audio input with decay envelope, state variable filter and midi implementation.

- Button_1: Create new WaveForm
- Button_2: Gate in 
- Pot_A: Frequency Osc_1
- Pot_B: Frequency Osc_2
- Pot_C: Decay envelope (5sec)
- Pot_D: State variable filter (LP->HP)
....
- Gate Out: Pass through of Gate in
- CV_OUT_1: Pass through of Pot_A
- CV_OUT_2: Midi_Note_Freq
	
ToDo:
- CV_OUT_2: Midi_Note_Freq to CV
- Gate Out: Detect midi gate
- Implement several waveforms if there is nothing in the input. Already tried it with the attached waveforms.pd but the lich memory overloads even with only 2 waveforms. (for now if the input signal is not strong enough the button_1 does nothing) 


#include "daisy_seed.h"
#include "daisysp.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <random>
#include <ctime>

/*
	Includes:
	- Map is for the storage of the notes, key - value pairs for
	notes and their corresponding frequencies from low to high.
	- Vector = Array
	- Random is used for generating a random sequence of notes in a
	specified scale.
		- Time is for seeding the random generator
*/

using namespace daisy;
using namespace daisysp;
using namespace std;

DaisySeed hardware;
Oscillator osc;
MoogLadder flt; 
AdEnv synthVolEnv, synthPitchEnv;
Switch activate_sequence, random_sequence, switch_mode, select_note, change_pitch;
AdcChannelConfig pots[3];
Metro tick;
GPIO seq_button1, seq_button2, seq_button3, seq_button4, seq_button5, seq_button6, seq_button7, seq_button8;
vector<GPIO> leds(8);

/*
	- Hardware starts audio and controls daisy seed functionality.
	- Osc is the oscillator for the bass sound of the sequncer.
	- Envelopes for volume and for pitch of the bass.
	- Switches:
		- activate_sequence: Starting/stopping sequence
		- random_sequnce: Randomly generated sequence based of of 
		current scale.
		- switch_mode: Changes the modal character of the sound. I.e 
		from Ionian to Dorian. Basically means to increase specific notes
		by a half step. (read more: https://www.classical-music.com/features/articles/modes-in-music-what-they-are-and-how-they-are-used-in-music/)
		- select_note: select which note in the sequence that should have
		their pitch changed.
		- change_pitch: increase the pitch by one step, based on scale, 
		goes back around when reaching maximum
	- AdcChannelConfig (pots):
		- change_bpm: Gradualy change bpm from 30 - 300 (?)
	- Tick for keeping time using the tick.process() function, returning
	true when a tick is "active". The tick is set to a specific interval
	and is activated as long as the AudioCallback (infinite loop) is active.
	- seq_buttons: Buttons for each note in the sequence, used to control
	the pitch for each note.
*/

int steps = 8;
int active_step = 0;
int mode_int = 0;
int selected_note = 0;
int const LOW_RANGE_BPM = 60;
int const HIGH_RANGE_BPM = 290;
float tempo_bpm = 120.f;
string mode = "WWHWWWH"; // W = Whole step, H = Half step  
bool active = false;

/*
	- Steps: Number of steps in the sequence
	- Active_step: The current active step, is incremented for each played
	note
	- mode_int: An integer corresponding to the current mode, i.e:
	Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian or Locrian.
	Might not be needed in the current implementation.
	- selected_note: which note in the sequence is currently selected (0-7 range)
	- LOW_RANGE/HIGH_RANGE: Constants for BPM range for the BPM input potentiometer.
	- tempo_bpm: The current tempo of the sequencer.
	- mode: This string specifies the steps for the scale going from the 
	root note upwards. It starts from the major scale (Ionian), which for
	C is  "C", "D", "E", "F", "G", "A", "B". 
	This string will be left shifted to the left to change the mode.
	- active: True/False if the sequencer is active or not. 
*/

unordered_map<string, vector<double>> notes = {
    {"C", {16.35, 32.70, 65.41, 130.81, 261.63, 523.25, 1046.50, 2093.00, 4186.01}},
    {"Db", {17.32, 34.65, 69.30, 138.59, 277.18, 554.37, 1108.73, 2217.46, 4434.92}},
    {"D", {18.35, 36.71, 73.42, 146.83, 293.66, 587.33, 1174.66, 2349.32, 4698.64}},
    {"Eb", {19.45, 38.89, 77.78, 155.56, 311.13, 622.25, 1244.51, 2489.02, 4978.03}},
    {"E", {20.60, 41.20, 82.41, 164.81, 329.63, 659.26, 1318.51, 2637.02}},
    {"F", {21.83, 43.65, 87.31, 174.61, 349.23, 698.46, 1396.91, 2793.83}},
    {"Gb", {23.12, 46.25, 92.50, 185.00, 369.99, 739.99, 1479.98, 2959.96}},
    {"G", {24.50, 49.00, 98.00, 196.00, 392.00, 783.99, 1567.98, 3135.96}},
    {"Ab", {25.96, 51.91, 103.83, 207.65, 415.30, 830.61, 1661.22, 3322.44}},
    {"A", {27.50, 55.00, 110.00, 220.00, 440.00, 880.00, 1760.00, 3520.00}},
    {"Bb", {29.14, 58.27, 116.54, 233.08, 466.16, 932.33, 1864.66, 3729.31}},
    {"B", {30.87, 61.74, 123.47, 246.94, 493.88, 987.77, 1975.53, 3951.07}}
};
vector<string> scale = {"C", "D", "E", "F", "G", "A", "B", "C"}; // Major (Ionian)
//vector<string> sequence = {scale[0], scale[0], scale[0], scale[0], scale[0], scale[0], scale[0], scale[0]};
vector<string> sequence = vector<string>(scale);

/*
	For changing the pitch of the synth. (Could be done easier=)
*/

void setPitch(double freq){
    synthPitchEnv.SetMax(freq);
    synthPitchEnv.SetMin(freq);
}

/*
	Shifts the mode string to the left one step. "WWHWWWH" becomes "WHWWWHW"
*/

string circularShiftLeft(string mode) {
    char first = mode[0];
    mode.erase(0, 1);
    return mode += first;
}

/*
	Shifts the array of all notes if the root note is to be changed.
	Then the modes will be taken from a "new pool" starting with a new 
	root note.
*/

vector<string> circularShiftLeftArray(vector<string> array){
    vector<string> new_array(array);
    rotate(new_array.begin(), new_array.begin() + 1, new_array.end());
    return new_array;
}


/*
	Generates a new scale based on the current one. This function will
	insert notes into the global "scale" variable based on the steps in
	the "mode" string. If there is a "W" (whole-step) it will "jump" two
	steps, "semi-tones", in the all_notes array, otherwise just one step.
*/

vector<string> generateScale(){
    vector<string> all_notes = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
    vector<string> new_scale(scale.size());

    int index = 0;
    size_t notes_collected = 0;
    while (notes_collected < steps)
    {
        new_scale[notes_collected] = all_notes[index % all_notes.size()];
        index += (mode[notes_collected] == 'W') ? 2 : 1;
        notes_collected++;
    }  
    return new_scale;      
}

/*
	Returns a new sequence with the same size as the old one which has
	randomly generated notes taken from the "scale pool" of notes.
	The seed is set in "main" based on the current time.
*/

vector<string> randomizeSequence(){
    vector<string> resulting_sequence(sequence.size()); 

    for(int i = 0; i < static_cast<int>(resulting_sequence.size()); i++){    
        int random_num = rand();
        int randomIndex = random_num % scale.size();
        resulting_sequence[i] = scale[randomIndex];
    }

    return resulting_sequence;
}

float convertBPMtoFreq(float bpm){
	return (bpm / 60.f)*8.f;
}

void increasePitchForActiveNote(){
	for(int i = 0; i < 8; i++){
		if(sequence[selected_note] == scale[i] && i == 7){
			sequence[selected_note] = scale[1];
			break;
		}
		else if (sequence[selected_note] == scale[i]){
			sequence[selected_note] = scale[i+1];
			break;
		}
	}
}

void setLightForSelectedNote(){
	leds[selected_note].Write(false);
	selected_note = (selected_note + 1) % steps;
	leds[selected_note].Write(true);
}

/*
	AudioHandle::InterleavingInputBuffer  in,
	AudioHandle::InterleavingOutputBuffer out,??
*/

void inputHandler(){
	// Filters out noise from button-press.	
	activate_sequence.Debounce();
	random_sequence.Debounce();
	switch_mode.Debounce();
	select_note.Debounce();
	change_pitch.Debounce();

	if(activate_sequence.RisingEdge())
        active = !active;

	if(random_sequence.RisingEdge()){
        active_step = 0;
        sequence = randomizeSequence();
    }
	
	if(switch_mode.RisingEdge()){
        mode = circularShiftLeft(mode);

        if(mode_int == 7) mode_int = 0;
        else mode_int++;

        scale = generateScale();
        // Temporarily make sequence to scale
        sequence = vector<string>(scale);
    }

	if(select_note.RisingEdge())
		setLightForSelectedNote();

	if(change_pitch.RisingEdge())
		increasePitchForActiveNote();

	tempo_bpm = floor((hardware.adc.GetFloat(0) * (HIGH_RANGE_BPM - LOW_RANGE_BPM)) + LOW_RANGE_BPM); // BPM range from 30-300
	tick.SetFreq(convertBPMtoFreq(tempo_bpm));
	
	float cutoff = hardware.adc.GetFloat(1) * (20000 - 20) + 20;
	flt.SetFreq(cutoff);

	float resonance = hardware.adc.GetFloat(2) * (0.89); // 0.2 - 0.99
	flt.SetRes(resonance);
}	

/*
	Prepares the sample for the output audio. 
	This doesn't really make much sense to me yet.
*/

void prepareAudioBlock(size_t size, AudioHandle::InterleavingOutputBuffer out){
	float osc_out, synth_env_out, sig;
	for(size_t i = 0; i < size; i += 2) {
		//Get the next volume samples
		synth_env_out = synthVolEnv.Process();
		//Apply the pitch envelope to the synth
		osc.SetFreq(synthPitchEnv.Process());
		//Set the synth volume to the envelope's output
		osc.SetAmp(synth_env_out);
		//Process the next oscillator sample
		osc_out = osc.Process();

		//Signals can be mixed like this: sig = .5 * noise_out + .5 * osc_out;
		sig = flt.Process(osc_out);
		//Set the left and right outputs to the (mixed) signals
		out[i]     = sig;
		out[i + 1] = sig;
	}
}

/*
	Triggers a note in the sequence, and increases the active step.
	If the active step is at the last place, and the synth is at the first 
	mode it wants to access the C note one octave above (one place forward
	in the map with frequencies for each note).
*/

void triggerSequence(){
	if(tick.Process()){
		// Access the current note in the scale
		string note = sequence[active_step];
		if(active_step == 7)
			setPitch(notes[note][4]);
		else
			setPitch(notes[note][3]);
		synthVolEnv.Trigger();
		synthPitchEnv.Trigger();
		
		// Increase the step in sequence
		active_step = (active_step + 1) % steps;
	}
}



/* 
	Configure and Initialize the Daisy Seed
	These are separate to allow reconfiguration of any of the internal
	components before initialization.
	Block size refers to the number of samples handled per callback
*/

void configureAndInitHardware(){
	hardware.Configure();
	hardware.Init();
	hardware.SetAudioBlockSize(4); 
	//hardware.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
}

/*
	Initialize oscillator for synthesizer, and set initial amplitude
	to 1.
*/

void initOscillator(float samplerate){
    osc.Init(samplerate);
    osc.SetWaveform(Oscillator::WAVE_SAW);
    osc.SetAmp(1);
}

/*
	This envelope will control the kick oscillator's pitch
	Note that this envelope is much faster than the volume
*/

void initPitchEnv(float samplerate){	
    synthPitchEnv.Init(samplerate);
    synthPitchEnv.SetTime(ADENV_SEG_ATTACK, .01);
    synthPitchEnv.SetTime(ADENV_SEG_DECAY, .05);
    synthPitchEnv.SetMax(400);
    synthPitchEnv.SetMin(400);
}

/*
	This one will control the kick's volume
*/

void initVolEnv(float samplerate){
	synthVolEnv.Init(samplerate);
    synthVolEnv.SetTime(ADENV_SEG_ATTACK, .01);
    synthVolEnv.SetTime(ADENV_SEG_DECAY, 1);
    synthVolEnv.SetMax(1);
    synthVolEnv.SetMin(0);
}

 /*
	Initialize the buttons on pins 28, 27 and 25. (35, 34, 32 on the
	daisy seed.)
	The callback rate is samplerate / blocksize (48)
*/

void initButtons(float samplerate){
	activate_sequence.Init(hardware.GetPin(28), samplerate / 48.f); // 35
    random_sequence.Init(hardware.GetPin(27), samplerate / 48.f); // 34
    switch_mode.Init(hardware.GetPin(25), samplerate / 48.f); // 32
	select_note.Init(hardware.GetPin(23), samplerate / 48.f); // 30
	change_pitch.Init(hardware.GetPin(22), samplerate / 48.f); // 29
	pots[0].InitSingle(hardware.GetPin(21)); // 28, change bpm
	pots[1].InitSingle(hardware.GetPin(20)); // 27, change cut-off freq
	pots[2].InitSingle(hardware.GetPin(19)); // 26, change cut-off freq
	hardware.adc.Init(pots, 3); // Set ADC to use our configuration, and how many pots
	// More pots: https://forum.electro-smith.com/t/adc-reading/541
}

void initFilter(float samplerate){
	flt.Init(samplerate);
	flt.SetRes(0.7);
	flt.SetFreq(700);
}

/*
	Initialize Metro object at bpm (ex 120) divided by 60 resulting 
	in the freq for a note for each 4th beat. Multiply by 4 to get 
	for each beat.
*/

void initTick(float samplerate){
    tick.Init((tempo_bpm / 60.f)*4.f, samplerate);
}

void initLEDOutputs(){
	seq_button1.Init(daisy::seed::D7, GPIO::Mode::OUTPUT);
	seq_button2.Init(daisy::seed::D8, GPIO::Mode::OUTPUT);
	seq_button3.Init(daisy::seed::D9, GPIO::Mode::OUTPUT);
	seq_button4.Init(daisy::seed::D10, GPIO::Mode::OUTPUT);
	seq_button5.Init(daisy::seed::D11, GPIO::Mode::OUTPUT);
	seq_button6.Init(daisy::seed::D12, GPIO::Mode::OUTPUT);
	seq_button7.Init(daisy::seed::D13, GPIO::Mode::OUTPUT);
	seq_button8.Init(daisy::seed::D14, GPIO::Mode::OUTPUT);
}

void playSequence(size_t size, AudioHandle::InterleavingOutputBuffer out){
	if(active){
		prepareAudioBlock(size, out);
        triggerSequence();
    }
    else
        /*
			This part is not understood yet. Without it, the daisyseed 
			produces a clicking sound when the sequence is inactive.
		*/
        for(size_t i = 0; i < size; i += 2) {
            out[i] = 0;
            out[i + 1] = 0;
        }
}

void AudioCallback(AudioHandle::InterleavingInputBuffer in, AudioHandle::InterleavingOutputBuffer out, size_t size)
{
	inputHandler();
	playSequence(size, out);
}

int main(void) {
	configureAndInitHardware();
	
	float samplerate = hardware.AudioSampleRate();
	
	initOscillator(samplerate);
	initPitchEnv(samplerate);
	initVolEnv(samplerate);
	initButtons(samplerate);
	initFilter(samplerate);
	initTick(samplerate);
	initLEDOutputs();
    
    /* 
		Initialize random generator, and start callback.
	*/
    srand((unsigned int) time(NULL));
	hardware.adc.Start(); // Start ADC
    hardware.StartAudio(AudioCallback);
    // Loop forever
    for(;;) {}
}


// Enable logging
// hardware.StartLog(true);
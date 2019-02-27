#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "SineWaveVoice.h"
#include "sBMP4Synthesiser.h"
#include "Helpers.h"

//==============================================================================

class sBMP4AudioProcessor : public AudioProcessor
{
public:

    //==============================================================================
    sBMP4AudioProcessor();
    ~sBMP4AudioProcessor();

    void reset() override;

    bool getIsButtonEnabled (StringRef id) { return state.getParameter (id)->getValue(); }

    String getSelectedChoice (StringRef id) { return ((AudioParameterChoice*) state.getParameter (id))->getCurrentValueAsText(); }

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;
    void processBlock (AudioBuffer<double>&, MidiBuffer&) override;

    void process (AudioBuffer<float>& buffer, MidiBuffer& midiMessages);

    //==============================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
//==============================================================================
    const String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int /*index*/) override { }
    const String getProgramName (int /*index*/) override { return {}; }
    void changeProgramName (int /*index*/, const String& /*newName*/) override { }

    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    AudioProcessorValueTreeState state;

#if CPU_USAGE
    PerformanceCounter perfCounter;
#endif

private:

    //==============================================================================
    double lastSampleRate = {};

    sBMP4Synthesiser synth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (sBMP4AudioProcessor)
};

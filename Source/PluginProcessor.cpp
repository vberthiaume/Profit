/*
  ==============================================================================

   Copyright (c) 2019 - Vincent Berthiaume

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   To use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   sBMP4 IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <limits>

using namespace sBMP4AudioProcessorIDs;
using namespace sBMP4AudioProcessorNames;
using namespace sBMP4AudioProcessorChoices;
using namespace Constants;

//==============================================================================
sBMP4AudioProcessor::sBMP4AudioProcessor() :
    juce::AudioProcessor (BusesProperties().withInput  ("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
    state (*this, nullptr, "state",
    {
        std::make_unique<juce::AudioParameterInt>     (osc1FreqID, osc1FreqDesc, midiNoteRange.getRange().getStart(), midiNoteRange.getRange().getEnd(), defaultOscMidiNote),
        std::make_unique<juce::AudioParameterInt>     (osc2FreqID, osc2FreqDesc, midiNoteRange.getRange().getStart(), midiNoteRange.getRange().getEnd(), defaultOscMidiNote),

        std::make_unique<juce::AudioParameterFloat>   (osc1TuningID, osc1TuningDesc, tuningSliderRange, (float) defaultOscTuning),
        std::make_unique<juce::AudioParameterFloat>   (osc2TuningID, osc2TuningDesc, tuningSliderRange, (float) defaultOscTuning),

        std::make_unique<juce::AudioParameterFloat>   (oscSubID, oscSubOctDesc, sliderRange, (float) defaultSubOsc),
        std::make_unique<juce::AudioParameterFloat>   (oscMixID, oscMixDesc, sliderRange, (float) defaultOscMix),
        std::make_unique<juce::AudioParameterFloat>   (oscNoiseID, oscNoiseDesc, sliderRange, (float) defaultOscNoise),
        std::make_unique<juce::AudioParameterFloat>   (oscSlopID, oscSlopDesc, slopSliderRange, (float) defaultOscSlop),

        std::make_unique<juce::AudioParameterChoice>  (osc1ShapeID,  osc1ShapeDesc,  juce::StringArray {oscShape0, oscShape1, oscShape2, oscShape3, oscShape4}, defaultOscShape),
        std::make_unique<juce::AudioParameterChoice>  (osc2ShapeID,  osc2ShapeDesc,  juce::StringArray {oscShape0, oscShape1, oscShape2, oscShape3, oscShape4}, defaultOscShape),

        std::make_unique<juce::AudioParameterFloat>   (filterCutoffID, filterCutoffSliderDesc, cutOffRange, defaultFilterCutoff),
        std::make_unique<juce::AudioParameterFloat>   (filterResonanceID, filterResonanceSliderDesc, sliderRange, defaultFilterResonance),

        std::make_unique<juce::AudioParameterFloat>   (ampAttackID, ampAttackSliderDesc, attackRange, defaultAmpA),
        std::make_unique<juce::AudioParameterFloat>   (ampDecayID, ampDecaySliderDesc, decayRange, defaultAmpD),
        std::make_unique<juce::AudioParameterFloat>   (ampSustainID, ampSustainSliderDesc, sustainRange, defaultAmpS),
        std::make_unique<juce::AudioParameterFloat>   (ampReleaseID, ampReleaseSliderDesc, releaseRange, defaultAmpR),

        std::make_unique<juce::AudioParameterFloat>   (filterEnvAttackID, ampAttackSliderDesc, attackRange, defaultAmpA),
        std::make_unique<juce::AudioParameterFloat>   (filterEnvDecayID, ampDecaySliderDesc, decayRange, defaultAmpD),
        std::make_unique<juce::AudioParameterFloat>   (filterEnvSustainID, ampSustainSliderDesc, sustainRange, defaultAmpS),
        std::make_unique<juce::AudioParameterFloat>   (filterEnvReleaseID, ampReleaseSliderDesc, releaseRange, defaultAmpR),

        std::make_unique<juce::AudioParameterFloat>   (lfoFreqID,   lfoFreqSliderDesc, lfoRange, defaultLfoFreq),
        std::make_unique<juce::AudioParameterChoice>  (lfoShapeID, lfoShapeDesc,  juce::StringArray {lfoShape0, lfoShape1, /*lfoShape2,*/ lfoShape3, lfoShape4}, defaultLfoShape),
        std::make_unique<juce::AudioParameterChoice>  (lfoDestID, lfoDestDesc,  juce::StringArray {lfoDest0, lfoDest1, lfoDest2, lfoDest3}, defaultLfoDest),
        std::make_unique<juce::AudioParameterFloat>   (lfoAmountID, lfoAmountSliderDesc, sliderRange, defaultLfoAmount),

        std::make_unique<juce::AudioParameterFloat>   (effectParam1ID, effectParam1Desc, sliderRange, defaultEffectParam1),
        std::make_unique<juce::AudioParameterFloat>   (effectParam2ID, effectParam2Desc, sliderRange, defaultEffectParam2),

        std::make_unique<juce::AudioParameterFloat>   (masterGainID, masterGainDesc, sliderRange, defaultMasterGain)
    })

#if CPU_USAGE
    , perfCounter ("ProcessBlock")
#endif
{
    state.addParameterListener (osc1FreqID.getParamID(), &synth);
    state.addParameterListener (osc2FreqID.getParamID(), &synth);

    state.addParameterListener (osc1TuningID.getParamID(), &synth);
    state.addParameterListener (osc2TuningID.getParamID(), &synth);

    state.addParameterListener (osc1ShapeID.getParamID(), &synth);
    state.addParameterListener (osc2ShapeID.getParamID(), &synth);

    state.addParameterListener (oscSubID.getParamID(), &synth);
    state.addParameterListener (oscMixID.getParamID(), &synth);
    state.addParameterListener (oscNoiseID.getParamID(), &synth);
    state.addParameterListener (oscSlopID.getParamID(), &synth);

    state.addParameterListener (filterCutoffID.getParamID(), &synth);
    state.addParameterListener (filterResonanceID.getParamID(), &synth);
    state.addParameterListener (filterEnvAttackID.getParamID(), &synth);
    state.addParameterListener (filterEnvDecayID.getParamID(), &synth);
    state.addParameterListener (filterEnvSustainID.getParamID(), &synth);
    state.addParameterListener (filterEnvReleaseID.getParamID(), &synth);

    state.addParameterListener (ampAttackID.getParamID(), &synth);
    state.addParameterListener (ampDecayID.getParamID(), &synth);
    state.addParameterListener (ampSustainID.getParamID(), &synth);
    state.addParameterListener (ampReleaseID.getParamID(), &synth);

    state.addParameterListener (lfoShapeID.getParamID(), &synth);
    state.addParameterListener (lfoDestID.getParamID(), &synth);
    state.addParameterListener (lfoFreqID.getParamID(), &synth);
    state.addParameterListener (lfoAmountID.getParamID(), &synth);

    state.addParameterListener (effectParam1ID.getParamID(), &synth);
    state.addParameterListener (effectParam2ID.getParamID(), &synth);

    state.addParameterListener (masterGainID.getParamID(), &synth);
}

sBMP4AudioProcessor::~sBMP4AudioProcessor()
{
}

//==============================================================================

void sBMP4AudioProcessor::reset()
{
}

//==============================================================================
void sBMP4AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;

    synth.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
}

void sBMP4AudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool sBMP4AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

//============================================================================= =
void sBMP4AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    jassert (! isUsingDoublePrecision());
    process (buffer, midiMessages);
}

void sBMP4AudioProcessor::processBlock (juce::AudioBuffer<double>& /*ogBuffer*/, juce::MidiBuffer& /*midiMessages*/)
{
    jassertfalse;
    jassert (isUsingDoublePrecision());
    //process (ogBuffer, midiMessages);
}

void sBMP4AudioProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

#if CPU_USAGE
    perfCounter.start();
#endif
    //we're not dealing with any inputs here, so clear the buffer
    buffer.clear();

    synth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());

#if CPU_USAGE
    perfCounter.stop();
#endif
}

//==============================================================================
void sBMP4AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::unique_ptr<juce::XmlElement> xmlState (state.copyState().createXml());

    if (xmlState.get() != nullptr)
        copyXmlToBinary (*xmlState, destData);
}

void sBMP4AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        state.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
juce::AudioProcessorEditor* sBMP4AudioProcessor::createEditor()
{
    return new sBMP4AudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sBMP4AudioProcessor();
}

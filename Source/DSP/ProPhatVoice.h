/*
  ==============================================================================

   Copyright (c) 2019 - Vincent Berthiaume

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   To use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   ProPhat IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#pragma once
#include "../Utility/Helpers.h"
#include "PhatOscillators.h"

#include <mutex>
#include <set>

struct ProPhatSound : public juce::SynthesiserSound
{
    bool appliesToNote    (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================

/**
 * @brief The main voice for our synthesizer.
*/
template <std::floating_point T>
class ProPhatVoice : public juce::SynthesiserVoice
                   , public juce::AudioProcessorValueTreeState::Listener
{
public:
    enum class ProcessorId
    {
        filterIndex = 0,
        masterGainIndex,
    };

    ProPhatVoice (juce::AudioProcessorValueTreeState& processorState, int voiceId, std::set<int>* activeVoiceSet);

    void addParamListenersToState ();
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    void prepare (const juce::dsp::ProcessSpec& spec);

    void setAmpParam (juce::StringRef parameterID, float newValue);
    void setFilterEnvParam (juce::StringRef parameterID, float newValue);

    void setLfoShape (int shape);
    void setLfoDest (int dest);
    void setLfoFreq (float newFreq) { lfo.setFrequency (newFreq); }
    void setLfoAmount (float newAmount) { lfoAmount = newAmount; }

    void setFilterCutoff (float newValue);
    void setFilterTiltCutoff (float newValue);
    void setFilterResonance (float newAmount);

    void pitchWheelMoved (int newPitchWheelValue) override { oscillators.pitchWheelMoved (newPitchWheelValue); }

    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound* /*sound*/, int currentPitchWheelPosition) override;
    void stopNote (float /*velocity*/, bool allowTailOff) override;

    bool canPlaySound (juce::SynthesiserSound* sound) override { return dynamic_cast<ProPhatSound*> (sound) != nullptr; }

    //template <std::floating_point T>
    void renderNextBlockTemplate(juce::AudioBuffer<T>& outputBuffer, int startSample, int numSamples);

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;
    void renderNextBlock (juce::AudioBuffer<double>& outputBuffer, int startSample, int numSamples) override;

    void controllerMoved (int controllerNumber, int newValue) override;

    int getVoiceId() { return voiceId; }

private:
    juce::AudioProcessorValueTreeState& state;

    int voiceId;

    float lfoCutOffContributionHz { 0.f };
    void setFilterCutoffInternal (float curCutOff);
    void setFilterResonanceInternal (float curCutOff);

    /** Calculate LFO values. Called on the audio thread. */
    inline void updateLfo();
    void processRampUp (juce::dsp::AudioBlock<T>& block, int curBlockSize);
    void processKillOverlap (juce::dsp::AudioBlock<T>& block, int curBlockSize);
    void assertForDiscontinuities (juce::AudioBuffer<T>& outputBuffer, int startSample, int numSamples, juce::String dbgPrefix);
    void applyKillRamp (juce::AudioBuffer<T>& outputBuffer, int startSample, int numSamples);

    PhatOscillators<T> oscillators;

    std::unique_ptr<juce::AudioBuffer<T>> overlap;
    int overlapIndex = -1;
    //TODO replace this currentlyKillingVoice bool with a check in the bitfield that voicesBeingKilled will become
    bool currentlyKillingVoice = false;
    std::set<int>* voicesBeingKilled;

    juce::dsp::ProcessorChain<juce::dsp::LadderFilter<T>, juce::dsp::Gain<T>> processorChain;
    //TODO: use a slider for this
    static constexpr auto envelopeAmount { 2 };

    juce::ADSR ampADSR, filterADSR;
    juce::ADSR::Parameters ampParams, filterEnvParams;
    bool currentlyReleasingNote = false, justDoneReleaseEnvelope = false;

    float curFilterCutoff = Constants::defaultFilterCutoff;
    float curFilterResonance = Constants::defaultFilterResonance;

    //lfo stuff
    static constexpr auto lfoUpdateRate = 100;
    int lfoUpdateCounter = lfoUpdateRate;
    juce::dsp::Oscillator<T> lfo;
    std::mutex lfoMutex;
    T lfoAmount = static_cast<T> (Constants::defaultLfoAmount);
    LfoDest lfoDest;

    //for the random lfo
    juce::Random rng;
    T randomValue = 0.f;
    bool valueWasBig = false;

    bool rampingUp = false;
    int rampUpSamplesLeft = 0;

    float tiltCutoff { 0.f };

    int curPreparedSamples = 0;
};

template<std::floating_point T>
void ProPhatVoice<T>::renderNextBlockTemplate (juce::AudioBuffer<T>& outputBuffer, int startSample, int numSamples)
{
    if (! currentlyKillingVoice && ! isVoiceActive ())
        return;

    //reserve an audio block of size numSamples. Auvaltool has a tendency to _not_ call prepare before rendering
    //with new buffer sizes, so just making sure we're not taking more samples than the audio block was prepared with.
    numSamples = juce::jmin (numSamples, curPreparedSamples);
    auto currentAudioBlock { oscillators.prepareRender (numSamples) };

    for (int pos = 0; pos < numSamples;)
    {
        const auto subBlockSize = juce::jmin (numSamples - pos, lfoUpdateCounter);

        //render the oscillators
        auto oscBlock { oscillators.process (pos, subBlockSize) };

        //render our effects
        juce::dsp::ProcessContextReplacing<T> oscContext (oscBlock);
        processorChain.process (oscContext);

        //apply the enveloppes. We calculate and apply the amp envelope on a sample basis,
        //but for the filter env we increment it on a sample basis but only apply it
        //once per buffer, just like the LFO -- see below.
        auto filterEnvelope { 0.f };
        {
            const auto numChannels { oscBlock.getNumChannels () };
            for (auto i = 0; i < subBlockSize; ++i)
            {
                //calculate and atore filter envelope
                filterEnvelope = filterADSR.getNextSample ();

                //calculate and apply amp envelope
                const auto ampEnv = ampADSR.getNextSample ();
                for (int c = 0; c < numChannels; ++c)
                    oscBlock.getChannelPointer (c)[i] *= ampEnv;
            }

            if (currentlyReleasingNote && ! ampADSR.isActive ())
            {
                currentlyReleasingNote = false;
                justDoneReleaseEnvelope = true;
                stopNote (0.f, false);

#if DEBUG_VOICES
                DBG ("\tDEBUG ENVELOPPE DONE");
#endif
            }
        }

        if (rampingUp)
            processRampUp (oscBlock, (int) subBlockSize);

        if (overlapIndex > -1)
            processKillOverlap (oscBlock, (int) subBlockSize);

        //update our lfos at the end of the block
        lfoUpdateCounter -= subBlockSize;
        if (lfoUpdateCounter == 0)
        {
            lfoUpdateCounter = lfoUpdateRate;
            updateLfo ();
        }

        //apply our filter envelope once per buffer
        const auto curCutOff { (curFilterCutoff + tiltCutoff) * (1 + envelopeAmount * filterEnvelope) + lfoCutOffContributionHz };
        setFilterCutoffInternal (curCutOff);

        //increment our position
        pos += subBlockSize;
    }

    //add everything to the output buffer
    juce::dsp::AudioBlock<T> (outputBuffer).getSubBlock ((size_t) startSample, (size_t) numSamples).add (currentAudioBlock);

    if (currentlyKillingVoice)
        applyKillRamp (outputBuffer, startSample, numSamples);
#if DEBUG_VOICES
    else
        assertForDiscontinuities (outputBuffer, startSample, numSamples, {});
#endif
}

#include "../UI/ButtonGroupComponent.h"
#include "../Utility/Macros.h"

template class ProPhatVoice<float>;
template class ProPhatVoice<double>;

template <std::floating_point T>
ProPhatVoice<T>::ProPhatVoice (juce::AudioProcessorValueTreeState& processorState, int vId, std::set<int>* activeVoiceSet)
: state (processorState)
, voiceId (vId)
, oscillators (state)
, voicesBeingKilled (activeVoiceSet)
{
    addParamListenersToState ();

    processorChain.template get<(int)ProcessorId::masterGainIndex>().setGainLinear (static_cast<T> (Constants::defaultOscLevel));

    setFilterCutoffInternal (Constants::defaultFilterCutoff);
    setFilterResonanceInternal (Constants::defaultFilterResonance);

    lfoDest.curSelection = (int) defaultLfoDest;

    setLfoShape (LfoShape::triangle);
    lfo.setFrequency (Constants::defaultLfoFreq);
}

template <std::floating_point T>
void ProPhatVoice<T>::prepare (const juce::dsp::ProcessSpec& spec)
{
    curPreparedSamples = spec.maximumBlockSize;
    oscillators.prepare (spec);

    overlap = std::make_unique<juce::AudioBuffer<T>> (spec.numChannels, Constants::killRampSamples);
    overlap->clear();

    processorChain.prepare (spec);

    ampADSR.setSampleRate (spec.sampleRate);
    ampADSR.setParameters (ampParams);

    filterADSR.setSampleRate (spec.sampleRate);
    filterADSR.setParameters (filterEnvParams);

    lfo.prepare ({spec.sampleRate / lfoUpdateRate, spec.maximumBlockSize, spec.numChannels});
}

template <std::floating_point T>
void ProPhatVoice<T>::addParamListenersToState ()
{
    using namespace ProPhatParameterIds;

    //add our synth as listener to all parameters so we can do automations
    state.addParameterListener (filterCutoffID.getParamID (), this);
    state.addParameterListener (filterResonanceID.getParamID (), this);
    state.addParameterListener (filterEnvAttackID.getParamID (), this);
    state.addParameterListener (filterEnvDecayID.getParamID (), this);
    state.addParameterListener (filterEnvSustainID.getParamID (), this);
    state.addParameterListener (filterEnvReleaseID.getParamID (), this);

    state.addParameterListener (ampAttackID.getParamID (), this);
    state.addParameterListener (ampDecayID.getParamID (), this);
    state.addParameterListener (ampSustainID.getParamID (), this);
    state.addParameterListener (ampReleaseID.getParamID (), this);

    state.addParameterListener (lfoShapeID.getParamID (), this);
    state.addParameterListener (lfoDestID.getParamID (), this);
    state.addParameterListener (lfoFreqID.getParamID (), this);
    state.addParameterListener (lfoAmountID.getParamID (), this);
}

template <std::floating_point T>
void ProPhatVoice<T>::parameterChanged (const juce::String& parameterID, float newValue)
{
    using namespace ProPhatParameterIds;

    //DBG ("ProPhatVoice::parameterChanged (" + parameterID + ", " + juce::String (newValue));

    if (parameterID == ampAttackID.getParamID ()
        || parameterID == ampDecayID.getParamID ()
        || parameterID == ampSustainID.getParamID ()
        || parameterID == ampReleaseID.getParamID ())
        setAmpParam (parameterID, newValue);

    else if (parameterID == filterEnvAttackID.getParamID ()
             || parameterID == filterEnvDecayID.getParamID ()
             || parameterID == filterEnvSustainID.getParamID ()
             || parameterID == filterEnvReleaseID.getParamID ())
        setFilterEnvParam (parameterID, newValue);

    else if (parameterID == lfoShapeID.getParamID ())
        setLfoShape ((int) newValue);
    else if (parameterID == lfoDestID.getParamID ())
        setLfoDest ((int) newValue);
    else if (parameterID == lfoFreqID.getParamID ())
        setLfoFreq (newValue);
    else if (parameterID == lfoAmountID.getParamID ())
        setLfoAmount (newValue);

    else if (parameterID == filterCutoffID.getParamID ())
        setFilterCutoff (newValue);
    else if (parameterID == filterResonanceID.getParamID ())
        setFilterResonance (newValue);

    else
        jassertfalse;
}

template <std::floating_point T>
void ProPhatVoice<T>::setAmpParam (juce::StringRef parameterID, float newValue)
{
    if (newValue <= 0)
    {
        jassertfalse;
        newValue = std::numeric_limits<float>::epsilon();
    }

    if (parameterID == ProPhatParameterIds::ampAttackID.getParamID())
        ampParams.attack = newValue;
    else if (parameterID == ProPhatParameterIds::ampDecayID.getParamID())
        ampParams.decay = newValue;
    else if (parameterID == ProPhatParameterIds::ampSustainID.getParamID())
        ampParams.sustain = newValue;
    else if (parameterID == ProPhatParameterIds::ampReleaseID.getParamID())
        ampParams.release = newValue;

    ampADSR.setParameters (ampParams);
}

template <std::floating_point T>
void ProPhatVoice<T>::setFilterEnvParam (juce::StringRef parameterID, float newValue)
{
    if (newValue <= 0)
    {
        jassertfalse;
        newValue = std::numeric_limits<float>::epsilon();
    }

    if (parameterID == ProPhatParameterIds::filterEnvAttackID.getParamID())
        filterEnvParams.attack = newValue;
    else if (parameterID == ProPhatParameterIds::filterEnvDecayID.getParamID())
        filterEnvParams.decay = newValue;
    else if (parameterID == ProPhatParameterIds::filterEnvSustainID.getParamID())
        filterEnvParams.sustain = newValue;
    else if (parameterID == ProPhatParameterIds::filterEnvReleaseID.getParamID())
        filterEnvParams.release = newValue;

    filterADSR.setParameters (filterEnvParams);
}

//@TODO For now, all lfos oscillate between [0, 1], even though the random one (and only that one) should oscilate between [-1, 1]
template <std::floating_point T>
void ProPhatVoice<T>::setLfoShape (int shape)
{
    switch (shape)
    {
        case LfoShape::triangle:
        {
            std::lock_guard<std::mutex> lock (lfoMutex);
            lfo.initialise ([](T x) { return (std::sin (x) + 1) / 2; }, 128);
        }
            break;

        case LfoShape::saw:
        {
            std::lock_guard<std::mutex> lock (lfoMutex);
            lfo.initialise ([](T x)
                            {
                //this is a sawtooth wave; as x goes from -pi to pi, y goes from -1 to 1
                return juce::jmap (x, -juce::MathConstants<T>::pi, juce::MathConstants<T>::pi, T { 0 }, T { 1 });
            }, 2);
        }
            break;

            //TODO add this once we have more room in the UI for lfo destinations
            /*
             case LfoShape::revSaw:
             {
             std::lock_guard<std::mutex> lock (lfoMutex);
             lfo.initialise ([](float x)
             {
             return (float) juce::jmap (x, -juce::MathConstants<T>::pi, juce::MathConstants<T>::pi, 1.f, 0.f);
             }, 2);
             }
             break;
             */

        case LfoShape::square:
        {
            std::lock_guard<std::mutex> lock (lfoMutex);
            lfo.initialise ([](T x)
                            {
                if (x < 0)
                    return T { 0 };
                else
                    return T { 1 };
            });
        }
            break;

        case LfoShape::random:
        {
            std::lock_guard<std::mutex> lock (lfoMutex);
            lfo.initialise ([this](T x)
                            {
                if (x <= 0.f && valueWasBig)
                {
                    randomValue = rng.nextFloat()/* * 2 - 1*/;
                    valueWasBig = false;
                }
                else if (x > 0.f && ! valueWasBig)
                {
                    randomValue = rng.nextFloat()/* * 2 - 1*/;
                    valueWasBig = true;
                }

                return randomValue;
            });
        }
            break;

        default:
            jassertfalse;
            break;
    }
}

template <std::floating_point T>
void ProPhatVoice<T>::setLfoDest (int dest)
{
    //reset everything
    oscillators.resetLfoOscNoteOffsets ();

    //change the destination
    lfoDest.curSelection = dest;
}

template <std::floating_point T>
void ProPhatVoice<T>::setFilterCutoff (float newValue)
{
    curFilterCutoff = newValue;
    setFilterCutoffInternal (curFilterCutoff + tiltCutoff);
}

template <std::floating_point T>
void ProPhatVoice<T>::setFilterTiltCutoff (float newValue)
{
    tiltCutoff = newValue;
    setFilterCutoffInternal (curFilterCutoff + tiltCutoff);
}

template <std::floating_point T>
void ProPhatVoice<T>::setFilterResonance (float newAmount)
{
    curFilterResonance = newAmount;
    setFilterResonanceInternal (curFilterResonance);
}

//TODO For now, all lfos oscillate between [0, 1], even though the random one (and only that one) should oscillate between [-1, 1]
template <std::floating_point T>
void ProPhatVoice<T>::updateLfo()
{
    T lfoOut;
    {
        //TODO: LOCK IN AUDIO THREAD
        std::lock_guard<std::mutex> lock (lfoMutex);
        lfoOut = lfo.processSample (T (0)) * lfoAmount;
    }

    //TODO get this switch out of here, this is awful for performances
    switch (lfoDest.curSelection)
    {
        case LfoDest::osc1Freq:
            //lfoOsc1NoteOffset = lfoNoteRange.convertFrom0to1 (lfoOut);
            //oscillators.updateOscFrequencies();
            oscillators.setLfoOsc1NoteOffset (Constants::lfoNoteRange.convertFrom0to1 (static_cast<float> (lfoOut)));
            break;

        case LfoDest::osc2Freq:
            /*lfoOsc2NoteOffset = lfoNoteRange.convertFrom0to1 (lfoOut);
             oscillators.updateOscFrequencies();*/
            oscillators.setLfoOsc2NoteOffset (Constants::lfoNoteRange.convertFrom0to1 (static_cast<float> (lfoOut)));
            break;

        case LfoDest::filterCutOff:
            lfoCutOffContributionHz  = juce::jmap (lfoOut, T (0.0f), T (1.0f), T (10.0f), T (10000.0f));
            break;

        case LfoDest::filterResonance:
            setFilterResonanceInternal (curFilterResonance * (1 + envelopeAmount * static_cast<float> (lfoOut)));
            break;

        default:
            break;
    }
}

template <std::floating_point T>
void ProPhatVoice<T>::setFilterCutoffInternal (float curCutOff)
{
    const auto limitedCutOff { juce::jlimit (Constants::cutOffRange.start, Constants::cutOffRange.end, curCutOff) };
    processorChain.template get<(int) ProcessorId::filterIndex> ().setCutoffFrequencyHz (limitedCutOff);
}

template <std::floating_point T>
void ProPhatVoice<T>::setFilterResonanceInternal (float curResonance)
{
    const auto limitedResonance { juce::jlimit (0.f, 1.f, curResonance) };
    processorChain.template get<(int) ProcessorId::filterIndex> ().setResonance (limitedResonance);
}

template <std::floating_point T>
void ProPhatVoice<T>::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound* /*sound*/, int currentPitchWheelPosition)
{
#if DEBUG_VOICES
    DBG ("\tDEBUG start: " + juce::String (voiceId));
#endif

    ampADSR.setParameters (ampParams);
    ampADSR.reset();
    ampADSR.noteOn();

    filterADSR.setParameters (filterEnvParams);
    filterADSR.reset();
    filterADSR.noteOn();

    oscillators.updateOscFrequencies (midiNoteNumber, velocity, currentPitchWheelPosition);

    rampingUp = true;
    rampUpSamplesLeft = Constants::rampUpSamples;

    oscillators.updateOscLevels();
}

template <std::floating_point T>
void ProPhatVoice<T>::stopNote (float /*velocity*/, bool allowTailOff)
{
    if (allowTailOff)
    {
        currentlyReleasingNote = true;
        ampADSR.noteOff();
        filterADSR.noteOff();

#if DEBUG_VOICES
        DBG ("\tDEBUG tailoff voice: " + juce::String (voiceId));
#endif
    }
    else
    {
        if (getSampleRate() != 0.f && ! justDoneReleaseEnvelope)
        {
            rampingUp = false;

            overlap->clear();
            voicesBeingKilled->insert (voiceId);
            currentlyKillingVoice = true;
            renderNextBlock (*overlap, 0, Constants::killRampSamples);
            overlapIndex = 0;
        }

        justDoneReleaseEnvelope = false;
        clearCurrentNote();

#if DEBUG_VOICES
        DBG ("\tDEBUG kill voice: " + juce::String (voiceId));
#endif
    }
}

template <>
inline void ProPhatVoice<float>::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    renderNextBlockTemplate (outputBuffer, startSample, numSamples);
}

template <>
inline void ProPhatVoice<float>::renderNextBlock (juce::AudioBuffer<double>&, int, int)
{
    //trying to render doubles with a float voice!
    jassertfalse;
}

template <>
inline void ProPhatVoice<double>::renderNextBlock (juce::AudioBuffer<double>& outputBuffer, int startSample, int numSamples)
{
    renderNextBlockTemplate(outputBuffer, startSample, numSamples);
}

template <>
inline void ProPhatVoice<double>::renderNextBlock (juce::AudioBuffer<float>&, int, int)
{
    //trying to render floats with a double voice!
    jassertfalse;
}

template <std::floating_point T>
void ProPhatVoice<T>::controllerMoved (int controllerNumber, int newValue)
{
    //DBG ("controllerNumber: " + juce::String (controllerNumber) + ", newValue: " + juce::String (newValue));

    //1 == orba tilt. The newValue range [0-127] is converted to [curFilterCutoff, cutOffRange.end]
    if (controllerNumber == 1)
        setFilterTiltCutoff (juce::jmap (static_cast<float> (newValue), 0.f, 127.f, curFilterCutoff, Constants::cutOffRange.end));
}

template <std::floating_point T>
void ProPhatVoice<T>::processRampUp (juce::dsp::AudioBlock<T>& block, int curBlockSize)
{
#if DEBUG_VOICES
    DBG ("\tDEBUG RAMP UP " + juce::String (rampUpSamples - rampUpSamplesLeft));
#endif
    auto curRampUpLenght = juce::jmin ((int) curBlockSize, rampUpSamplesLeft);
    auto prevRampUpValue = (Constants::rampUpSamples - rampUpSamplesLeft) / (float) Constants::rampUpSamples;
    auto nextRampUpValue = prevRampUpValue + curRampUpLenght / (float) Constants::rampUpSamples;
    auto incr = (nextRampUpValue - prevRampUpValue) / (curRampUpLenght);

    jassert (nextRampUpValue >= 0.f && nextRampUpValue <= 1.0001f);

    for (int c = 0; c < block.getNumChannels (); ++c)
    {
        for (int i = 0; i < curRampUpLenght; ++i)
        {
            auto value = block.getSample (c, i);
            auto ramp = prevRampUpValue + i * incr;
            block.setSample (c, i, value * ramp);
        }
    }

    rampUpSamplesLeft -= curRampUpLenght;

    if (rampUpSamplesLeft <= 0)
    {
        rampingUp = false;
#if DEBUG_VOICES
        DBG ("\tDEBUG RAMP UP DONE");
#endif
    }
}

template <std::floating_point T>
void ProPhatVoice<T>::processKillOverlap (juce::dsp::AudioBlock<T>& block, int curBlockSize)
{
#if DEBUG_VOICES
    DBG ("\tDEBUG ADD OVERLAP" + juce::String (overlapIndex));
#endif

    auto curSamples = juce::jmin (Constants::killRampSamples - overlapIndex, (int) curBlockSize);

    for (int c = 0; c < block.getNumChannels (); ++c)
    {
        for (int i = 0; i < curSamples; ++i)
        {
            auto prev = block.getSample (c, i);
            auto overl = static_cast<T> (overlap->getSample (c, overlapIndex + i));
            auto total = prev + overl;

            jassert (total > -1 && total < 1);

            block.setSample (c, i, total);

#if PRINT_ALL_SAMPLES
            if (c == 0)
                DBG ("\tADD\t" + juce::String (prev) + "\t" + juce::String (overl) + "\t" + juce::String (total));
#endif
        }
    }

    overlapIndex += curSamples;

    if (overlapIndex >= Constants::killRampSamples)
    {
        overlapIndex = -1;
        voicesBeingKilled->erase (voiceId);
#if DEBUG_VOICES
        DBG ("\tDEBUG OVERLAP DONE");
#endif
    }
}

template <std::floating_point T>
void ProPhatVoice<T>::assertForDiscontinuities (juce::AudioBuffer<T>& outputBuffer, int startSample, int numSamples, juce::String dbgPrefix)
{
    auto prev = outputBuffer.getSample (0, startSample);
    auto prevDiff = abs (outputBuffer.getSample (0, startSample + 1) - prev);

    for (int c = 0; c < outputBuffer.getNumChannels (); ++c)
    {
        for (int i = startSample; i < startSample + numSamples; ++i)
        {
            //@TODO need some kind of compression to avoid values above 1.f...
            jassert (abs (outputBuffer.getSample (c, i)) < 1.5f);

            if (c == 0)
            {
#if PRINT_ALL_SAMPLES
                DBG (dbgPrefix + juce::String (outputBuffer.getSample (0, i)));
#endif
                auto cur = outputBuffer.getSample (0, i);
                jassert (abs (cur - prev) < .2f);

                auto curDiff = abs (cur - prev);
                jassert (curDiff - prevDiff < .08f);

                prev = cur;
                prevDiff = curDiff;
            }
        }
    }
}

template <std::floating_point T>
void ProPhatVoice<T>::applyKillRamp (juce::AudioBuffer<T>& outputBuffer, int startSample, int numSamples)
{
    outputBuffer.applyGainRamp (startSample, numSamples, 1.f, 0.f);
    currentlyKillingVoice = false;

#if DEBUG_VOICES
    DBG ("\tDEBUG START KILLRAMP");
    assertForDiscontinuities (outputBuffer, startSample, numSamples, "\tBUILDING KILLRAMP\t");
    DBG ("\tDEBUG stop KILLRAMP");
#endif
}


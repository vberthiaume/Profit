/*
  ==============================================================================

    sBMP4Synthesiser.h
    Created: 29 Jan 2019 10:59:23am
    Author:  Haake

  ==============================================================================
*/

#pragma once

#include "SineWaveVoice.h"
#include "Helpers.h"

class sBMP4Synthesiser : public Synthesiser
{
public:
    //static constexpr auto maxNumVoices = 4;

    sBMP4Synthesiser()
    {
        for (auto i = 0; i < numVoices; ++i)
            addVoice (new Voice());

        setNoteStealingEnabled (true);

        addSound (new SineWaveSound());
    }

    void prepare (const dsp::ProcessSpec& spec) noexcept
    {
        setCurrentPlaybackSampleRate (spec.sampleRate);

        for (auto* v : voices)
            dynamic_cast<Voice*> (v)->prepare (spec);

        fxChain.prepare (spec);
    }

private:
    void renderVoices (AudioBuffer<float>& outputAudio, int startSample, int numSamples) override
    {
        for (auto* voice : voices)
            if (voice->isKeyDown())
                voice->renderNextBlock (outputAudio, startSample, numSamples);

        auto block = dsp::AudioBlock<float> (outputAudio);
        auto blockToUse = block.getSubBlock ((size_t) startSample, (size_t) numSamples);
        auto contextToUse = dsp::ProcessContextReplacing<float> (blockToUse);
        fxChain.process (contextToUse);
    }

    enum
    {
        reverbIndex
    };

    dsp::ProcessorChain<dsp::Reverb> fxChain;
};
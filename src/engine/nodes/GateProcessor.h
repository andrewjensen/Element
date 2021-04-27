/*
This file is part of Element
Copyright (C) 2021  Kushview, LLC.  All rights reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#pragma once

#include "engine/nodes/BaseProcessor.h"
#include "ElementApp.h"

namespace Element {

/** Gate Processing */
class GateProcessor : public BaseProcessor
{
public:
    explicit GateProcessor (const int _numChannels = 2);

    const String getName() const override { return "Gate"; }

    void fillInPluginDescription (PluginDescription& desc) const override;

    void updateParams();
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override;
    float calcGainDB (float db);

    AudioProcessorEditor* createEditor() override   { return new GenericAudioProcessorEditor (this); }
    bool hasEditor() const override                 { return true; }

    double getTailLengthSeconds() const override    { return 0.0; };
    bool acceptsMidi() const override               { return false; }
    bool producesMidi() const override              { return false; }

    int getNumPrograms() override                                      { return 1; };
    int getCurrentProgram() override                                   { return 1; };
    void setCurrentProgram (int index) override                        { ignoreUnused (index); };
    const String getProgramName (int index) override                   { ignoreUnused (index); return "Parameter"; }
    void changeProgramName (int index, const String& newName) override { ignoreUnused (index, newName); }

    void getStateInformation (juce::MemoryBlock& destData) override;

    void setStateInformation (const void* data, int sizeInBytes) override;
    void numChannelsChanged() override;

protected:
    inline bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        // supports two input buses, one output
        if (layout.inputBuses.size() != 2 && layout.outputBuses.size() != 1)
            return false;

        // ins must equal outs
        if (layout.getMainInputChannels() != layout.getMainOutputChannels())
            return false;

        const auto nchans = layout.getMainInputChannels();
        return nchans >= 1 && nchans <= 2;
    }

    inline bool canApplyBusesLayout (const BusesLayout& layouts) const override { return isBusesLayoutSupported (layouts); }
    inline bool canApplyBusCountChange (bool isInput, bool isAddingBuses, BusProperties& outNewBusProperties) override
    {
        ignoreUnused (isInput, isAddingBuses, outNewBusProperties);
        return false;
    }

private:
    int numChannels = 0;
    AudioParameterFloat* threshDB  = nullptr;
    AudioParameterFloat* attackMs  = nullptr;
    AudioParameterFloat* releaseMs = nullptr;
    AudioParameterFloat* makeupDB  = nullptr; // FIXME: rename?

    SmoothedValue<float, ValueSmoothingTypes::Multiplicative> makeupGain = 1.0f;
    const int numSteps = 200;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateProcessor)
};

}

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

#include "GateProcessor.h"
// #include "gui/nodes/CompressorNodeEditor.h"

namespace Element {

GateProcessor::GateProcessor (const int _numChannels)
    : BaseProcessor (BusesProperties()
        .withInput ("Main", AudioChannelSet::canonicalChannelSet (jlimit (1, 2, _numChannels)))
        .withOutput ("Main", AudioChannelSet::canonicalChannelSet (jlimit (1, 2, _numChannels)))),
    numChannels (jlimit (1, 2, _numChannels))
{
    setBusesLayout (getBusesLayout());
    setRateAndBufferSizeDetails (44100.0, 1024);

    NormalisableRange<float> attackRange (0.1f, 1000.0f);
    attackRange.setSkewForCentre (10.0f);

    NormalisableRange<float> releaseRange (10.0f, 3000.0f);
    releaseRange.setSkewForCentre (100.0f);

    addParameter (threshDB  = new AudioParameterFloat ("thresh",    "Threshold [dB]", -30.0f, 0.0f, 0.0f));
    addParameter (attackMs  = new AudioParameterFloat ("attack",    "Attack [ms]",    attackRange, 10.0f));
    addParameter (releaseMs = new AudioParameterFloat ("release",   "Release [ms]",   releaseRange, 100.0f));
    addParameter (makeupDB  = new AudioParameterFloat ("makeup",    "Makeup [dB]",    -70.0f, 0.0f, -20.0f));

    makeupGain.reset (numSteps);
}

void GateProcessor::fillInPluginDescription (PluginDescription& desc) const
{
    desc.name = getName();
    desc.fileOrIdentifier   = EL_INTERNAL_ID_GATE;
    desc.descriptiveName    = "Gate";
    desc.numInputChannels   = numChannels * 2;
    desc.numOutputChannels  = numChannels;
    desc.hasSharedContainer = false;
    desc.isInstrument       = false;
    desc.manufacturerName   = "Element";
    desc.pluginFormatName   = "Element";
    desc.version            = "1.0.0";
    desc.uniqueId           = EL_INTERNAL_UID_GATE;
}

void GateProcessor::updateParams()
{
    makeupGain.setTargetValue (Decibels::decibelsToGain ((float) *makeupDB));
}

void GateProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    setBusesLayout (getBusesLayout());
    setRateAndBufferSizeDetails (sampleRate, maximumExpectedSamplesPerBlock);
}

void GateProcessor::releaseResources() {}

void GateProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    // FIXME: implement
}

// FIXME: define GUI???
// AudioProcessorEditor* GateProcessor::createEditor() { return new CompressorNodeEditor (*this); }

void GateProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    ValueTree state (Tags::state);
    state.setProperty ("thresh",    (float) *threshDB,  0);
    state.setProperty ("attack",    (float) *attackMs,  0);
    state.setProperty ("release",   (float) *releaseMs, 0);
    state.setProperty ("makeup",    (float) *makeupDB,  0);
    if (auto e = state.createXml())
        AudioProcessor::copyXmlToBinary (*e, destData);
}

void GateProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto e = AudioProcessor::getXmlFromBinary (data, sizeInBytes))
    {
        auto state = ValueTree::fromXml (*e);
        if (state.isValid())
        {
            *threshDB  = (float) state.getProperty ("thresh",    (float) *threshDB);
            *attackMs  = (float) state.getProperty ("attack",    (float) *attackMs);
            *releaseMs = (float) state.getProperty ("release",   (float) *releaseMs);
            *makeupDB  = (float) state.getProperty ("makeup",    (float) *makeupDB);
        }
    }
}

void GateProcessor::numChannelsChanged()
{
    numChannels = getTotalNumInputChannels();
}

}

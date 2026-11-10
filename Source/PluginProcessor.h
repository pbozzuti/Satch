/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SaturationEngine.h"      // the four saturation styles (Tube / Tape / Transistor / Transformer)

//==============================================================================
class SaturatorAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    SaturatorAudioProcessor();
    ~SaturatorAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Knobs (Drive / Mix / Output). Public so a custom editor can attach to them later.
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // 4x oversampling (2^2) keeps the saturation harmonics from aliasing.
    juce::dsp::Oversampling<float> oversampling { 2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    // Mixes dry/wet and delays the dry signal to match the oversampling latency.
    // The argument is the max wet latency it can compensate for (in samples).
    juce::dsp::DryWetMixer<float> mixer { 128 };

    // Previous gains, used to ramp smoothly between blocks (no zipper noise).
    float lastDriveGain = 1.0f;
    float lastOutputGain = 1.0f;

    double currentSampleRate = 44100.0;

    // Low / High: boost a band BEFORE the saturation and cut it by the same amount AFTER,
    // so that band gets driven harder without changing the overall tone balance.
    sat::Biquad preLow, preHigh, postLow, postHigh;
    float lowEmphasisDb = 0.0f, highEmphasisDb = 0.0f;     // current (smoothed) amounts

    // The saturation itself. Runs at the oversampled rate, see SaturationEngine.h.
    sat::SaturationEngine engine;

    // Clean-up after the saturation
    float dcX1[2] {}, dcY1[2] {};                           // DC blocker state

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaturatorAudioProcessor)
};

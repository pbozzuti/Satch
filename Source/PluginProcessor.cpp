/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SaturatorAudioProcessor::SaturatorAudioProcessor()
    : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout SaturatorAudioProcessor::createParameterLayout()
{
    using Param = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Drive: how hard the signal is pushed into the saturation curve (dB)
    layout.add (std::make_unique<Param> (juce::ParameterID { "drive", 1 }, "Drive",
                                         Range (0.0f, 36.0f, 0.01f), 6.0f,
                                         juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // Mix: 0 = fully dry, 1 = fully saturated
    layout.add (std::make_unique<Param> (juce::ParameterID { "mix", 1 }, "Mix",
                                         Range (0.0f, 1.0f, 0.001f), 1.0f));

    // Output: level trim after saturation (dB)
    layout.add (std::make_unique<Param> (juce::ParameterID { "output", 1 }, "Output",
                                         Range (-24.0f, 12.0f, 0.01f), 0.0f,
                                         juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // Low / High: how hard the low or high end is driven into the saturation (0..1)
    layout.add (std::make_unique<Param> (juce::ParameterID { "low", 1 },  "Low",
                                         Range (0.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<Param> (juce::ParameterID { "high", 1 }, "High",
                                         Range (0.0f, 1.0f, 0.001f), 0.5f));

    // Softness: 0 = hard, edgy clipping, 1 = soft, gentle saturation
    layout.add (std::make_unique<Param> (juce::ParameterID { "softness", 1 }, "Softness",
                                         Range (0.0f, 1.0f, 0.001f), 0.5f));

    // The ON/OFF switches next to Drive, Low, High and Softness
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "driveOn", 1 },    "Drive On",    true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "lowOn", 1 },      "Low On",      true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "highOn", 1 },     "High On",     true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "softnessOn", 1 }, "Softness On", true));

    // The tabs: which saturation character is used
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "type", 1 }, "Type",
                                                              juce::StringArray { "Tube", "Tape", "Transistor", "Transformer" }, 0));

    return layout;
}

SaturatorAudioProcessor::~SaturatorAudioProcessor()
{
}

//==============================================================================
const juce::String SaturatorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SaturatorAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SaturatorAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SaturatorAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SaturatorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SaturatorAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SaturatorAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SaturatorAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SaturatorAudioProcessor::getProgramName (int index)
{
    return {};
}

void SaturatorAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SaturatorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) getTotalNumOutputChannels() };

    oversampling.initProcessing ((size_t) samplesPerBlock);
    oversampling.reset();

    const auto latency = oversampling.getLatencyInSamples();
    mixer.prepare (spec);
    mixer.setWetLatency (latency);          // keep dry and wet time-aligned
    setLatencySamples ((int) latency);      // tell the host (Logic) about our latency

    lastDriveGain  = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("drive")->load());
    lastOutputGain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load());

    // Start the tone filters flat (0 dB) with a clean state
    currentSampleRate = sampleRate;
    lowEmphasisDb = highEmphasisDb = 0.0f;
    preLow.setLowShelf   (sampleRate, 120.0,  0.0);   postLow.setLowShelf   (sampleRate, 120.0,  0.0);
    preHigh.setHighShelf (sampleRate, 4000.0, 0.0);   postHigh.setHighShelf (sampleRate, 4000.0, 0.0);
    preLow.reset(); preHigh.reset(); postLow.reset(); postHigh.reset();
    for (int ch = 0; ch < 2; ++ch) { dcX1[ch] = dcY1[ch] = 0.0f; }

    // The saturation styles run at the oversampled rate
    engine.prepare (sampleRate * (double) oversampling.getOversamplingFactor());
}

void SaturatorAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SaturatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SaturatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);   // the filters hold state for 2 channels

    auto param = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // ---- read the controls ----
    const int type = juce::jlimit (0, (int) sat::SaturationEngine::NumTypes - 1, (int) param ("type"));

    const bool driveOn    = param ("driveOn")    > 0.5f;
    const bool lowOn      = param ("lowOn")      > 0.5f;
    const bool highOn     = param ("highOn")     > 0.5f;
    const bool softnessOn = param ("softnessOn") > 0.5f;

    // Drive switched off = no extra gain into the curve
    const float driveGain  = driveOn ? juce::Decibels::decibelsToGain (param ("drive")) : 1.0f;
    // Each style has its own natural loudness; the trim evens them out so switching tabs doesn't jump in level
    const float outputGain = juce::Decibels::decibelsToGain (param ("output") + sat::SaturationEngine::trimDb (type));
    mixer.setWetMixProportion (param ("mix"));

    // Softness: knob up = soft and gentle, knob down = hard and gritty. The engine wants "hardness",
    // which is the other way round. Switched off = a neutral middle setting.
    // Each style interprets it in its own way (see SaturationEngine.h).
    engine.setType (type);
    engine.setHardness (softnessOn ? 1.0f - param ("softness") : 0.5f);

    // Drive decides how much distortion there is at all: with Drive OFF or at 0 dB the sound is left clean
    engine.setDrive (param ("drive"), driveOn);

    // Low / High: emphasis in dB (0..+12), eased toward the target so turning the knob doesn't click
    const float targetLow  = lowOn ? param ("low") * 12.0f : 0.0f;
    const float targetHigh = (highOn ? param ("high") * 12.0f : 0.0f);

    auto glide = [] (float& current, float target)
    {
        current += (target - current) * 0.3f;
        if (std::abs (target - current) < 0.01f) current = target;
    };

    const float prevLow = lowEmphasisDb, prevHigh = highEmphasisDb;
    glide (lowEmphasisDb,  targetLow);
    glide (highEmphasisDb, targetHigh);

    if (lowEmphasisDb != prevLow)
    {
        preLow.setLowShelf  (currentSampleRate, 120.0,  (double) lowEmphasisDb);
        postLow.setLowShelf (currentSampleRate, 120.0, -(double) lowEmphasisDb);
    }
    if (highEmphasisDb != prevHigh)
    {
        preHigh.setHighShelf  (currentSampleRate, 4000.0,  (double) highEmphasisDb);
        postHigh.setHighShelf (currentSampleRate, 4000.0, -(double) highEmphasisDb);
    }

    juce::dsp::AudioBlock<float> block (buffer);

    // 1. Remember the clean signal for the Mix knob
    mixer.pushDrySamples (block);

    // 2. Pre-emphasis: boost the low / high bands so they get driven harder
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            x[i] = preHigh.process (ch, preLow.process (ch, x[i]));
    }

    // 3. Drive: push the signal harder into the curve (ramped to avoid clicks)
    buffer.applyGainRamp (0, numSamples, lastDriveGain, driveGain);
    lastDriveGain = driveGain;

    // 4. Saturate at 4x sample rate so the new harmonics don't alias.
    //    The style (Tube / Tape / Transistor / Transformer) is chosen inside the engine.
    auto upBlock = oversampling.processSamplesUp (block);
    for (size_t ch = 0; ch < upBlock.getNumChannels(); ++ch)
    {
        auto* x = upBlock.getChannelPointer (ch);
        for (size_t i = 0; i < upBlock.getNumSamples(); ++i)
            x[i] = engine.process (x[i], (int) ch);
    }
    oversampling.processSamplesDown (block);

    // 5. Clean up: undo the pre-emphasis and remove any DC offset the lopsided curves created
    const float dcR = 1.0f - juce::MathConstants<float>::twoPi * 10.0f / (float) currentSampleRate;   // ~10 Hz high-pass

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float v = postHigh.process (ch, postLow.process (ch, x[i]));

            const float y = v - dcX1[ch] + dcR * dcY1[ch];    // DC blocker
            dcX1[ch] = v;
            dcY1[ch] = y;
            x[i] = y;
        }
    }

    // 6. Blend saturated (wet) with clean (dry)
    mixer.mixWetSamples (block);

    // 7. Output trim
    buffer.applyGainRamp (0, numSamples, lastOutputGain, outputGain);
    lastOutputGain = outputGain;
}

//==============================================================================
bool SaturatorAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SaturatorAudioProcessor::createEditor()
{
    // Web-based editor: shows the files in the project's "ui" folder.
    return new SaturatorAudioProcessorEditor (*this);
}

//==============================================================================
void SaturatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SaturatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SaturatorAudioProcessor();
}

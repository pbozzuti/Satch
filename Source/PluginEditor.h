/*
  ==============================================================================

    Web-based editor: the plugin window is a web view that shows the files in
    the project's "ui" folder (index.html, CSS, JS...).

    - The knobs are connected to the plugin parameters through "relays"
      named "drive", "mix" and "output". In your JavaScript, use
      Juce.getSliderState ("drive") etc. (see ui/juce/index.js).
    - While developing, the ui folder is read straight from disk and the
      view reloads itself whenever you save a file in it.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class SaturatorAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit SaturatorAudioProcessorEditor (SaturatorAudioProcessor&);
    ~SaturatorAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    // Lets the host (e.g. Logic) find which parameter is under the mouse.
    int getControlParameterIndex (juce::Component&) override
    {
        return controlParameterIndexReceiver.getControlParameterIndex();
    }

private:
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);
    juce::Time getNewestUiFileTime() const;

    SaturatorAudioProcessor& audioProcessor;

    // The "ui" folder next to Source/, found from this file's location at compile time.
    juce::File uiFolder { juce::File (__FILE__).getParentDirectory().getParentDirectory().getChildFile ("ui") };
    juce::Time lastUiChange;

    // NOTE: the order of these members matters. Relays first, then the web view
    // (which registers them), then the attachments (which connect them to parameters).
    juce::WebSliderRelay driveRelay  { "drive" };
    juce::WebSliderRelay mixRelay    { "mix" };
    juce::WebSliderRelay outputRelay { "output" };
    juce::WebSliderRelay lowRelay      { "low" };
    juce::WebSliderRelay highRelay     { "high" };
    juce::WebSliderRelay softnessRelay { "softness" };
    juce::WebToggleButtonRelay driveOnRelay    { "driveOn" };
    juce::WebToggleButtonRelay lowOnRelay      { "lowOn" };
    juce::WebToggleButtonRelay highOnRelay     { "highOn" };
    juce::WebToggleButtonRelay softnessOnRelay { "softnessOn" };
    juce::WebComboBoxRelay typeRelay { "type" };          // the tabs: Tube / Tape / Transistor / Transformer
    juce::WebControlParameterIndexReceiver controlParameterIndexReceiver;

    juce::WebBrowserComponent webView { juce::WebBrowserComponent::Options{}
                                            .withNativeIntegrationEnabled()
                                            .withOptionsFrom (driveRelay)
                                            .withOptionsFrom (mixRelay)
                                            .withOptionsFrom (outputRelay)
                                            .withOptionsFrom (lowRelay)
                                            .withOptionsFrom (highRelay)
                                            .withOptionsFrom (softnessRelay)
                                            .withOptionsFrom (driveOnRelay)
                                            .withOptionsFrom (lowOnRelay)
                                            .withOptionsFrom (highOnRelay)
                                            .withOptionsFrom (softnessOnRelay)
                                            .withOptionsFrom (typeRelay)
                                            .withOptionsFrom (controlParameterIndexReceiver)
                                            .withResourceProvider ([this] (const auto& url)
                                                                   {
                                                                       return getResource (url);
                                                                   }) };

    juce::WebSliderParameterAttachment driveAttachment;
    juce::WebSliderParameterAttachment mixAttachment;
    juce::WebSliderParameterAttachment outputAttachment;
    juce::WebSliderParameterAttachment lowAttachment;
    juce::WebSliderParameterAttachment highAttachment;
    juce::WebSliderParameterAttachment softnessAttachment;
    juce::WebToggleButtonParameterAttachment driveOnAttachment;
    juce::WebToggleButtonParameterAttachment lowOnAttachment;
    juce::WebToggleButtonParameterAttachment highOnAttachment;
    juce::WebToggleButtonParameterAttachment softnessOnAttachment;
    juce::WebComboBoxParameterAttachment typeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaturatorAudioProcessorEditor)
};

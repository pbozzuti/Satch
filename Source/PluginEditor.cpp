/*
  ==============================================================================

    Web-based editor (see PluginEditor.h).

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String getMimeType (const juce::String& extension)
    {
        const auto ext = extension.trimCharactersAtStart (".").toLowerCase();

        if (ext == "html" || ext == "htm")  return "text/html";
        if (ext == "css")                   return "text/css";
        if (ext == "js" || ext == "mjs")    return "text/javascript";
        if (ext == "json" || ext == "map")  return "application/json";
        if (ext == "svg")                   return "image/svg+xml";
        if (ext == "png")                   return "image/png";
        if (ext == "jpg" || ext == "jpeg")  return "image/jpeg";
        if (ext == "gif")                   return "image/gif";
        if (ext == "ico")                   return "image/vnd.microsoft.icon";
        if (ext == "woff")                  return "font/woff";
        if (ext == "woff2")                 return "font/woff2";
        if (ext == "ttf")                   return "font/ttf";
        if (ext == "otf")                   return "font/otf";
        if (ext == "txt")                   return "text/plain";

        return "application/octet-stream";
    }

    std::vector<std::byte> toBytes (const void* data, size_t size)
    {
        std::vector<std::byte> bytes (size);

        if (size > 0)
            std::memcpy (bytes.data(), data, size);

        return bytes;
    }
}

//==============================================================================
SaturatorAudioProcessorEditor::SaturatorAudioProcessorEditor (SaturatorAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      driveAttachment  (*p.apvts.getParameter ("drive"),  driveRelay,  nullptr),
      mixAttachment    (*p.apvts.getParameter ("mix"),    mixRelay,    nullptr),
      outputAttachment (*p.apvts.getParameter ("output"), outputRelay, nullptr),
      lowAttachment      (*p.apvts.getParameter ("low"),      lowRelay,      nullptr),
      highAttachment     (*p.apvts.getParameter ("high"),     highRelay,     nullptr),
      softnessAttachment (*p.apvts.getParameter ("softness"), softnessRelay, nullptr),
      driveOnAttachment    (*p.apvts.getParameter ("driveOn"),    driveOnRelay,    nullptr),
      lowOnAttachment      (*p.apvts.getParameter ("lowOn"),      lowOnRelay,      nullptr),
      highOnAttachment     (*p.apvts.getParameter ("highOn"),     highOnRelay,     nullptr),
      softnessOnAttachment (*p.apvts.getParameter ("softnessOn"), softnessOnRelay, nullptr),
      typeAttachment (*p.apvts.getParameter ("type"), typeRelay, nullptr)
{
    addAndMakeVisible (webView);
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    // The design is 1035 x 693, so the window keeps that shape when resized.
    setResizable (true, true);
    setResizeLimits (600, 402, 1550, 1038);
    getConstrainer()->setFixedAspectRatio (1035.0 / 693.0);
    setSize (900, 603);

    // Check twice a second whether a file in the ui folder changed, and reload if so.
    lastUiChange = getNewestUiFileTime();
    startTimer (500);
}

SaturatorAudioProcessorEditor::~SaturatorAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void SaturatorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void SaturatorAudioProcessorEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

//==============================================================================
juce::Time SaturatorAudioProcessorEditor::getNewestUiFileTime() const
{
    juce::Time newest;

    for (const auto& f : uiFolder.findChildFiles (juce::File::findFiles, true))
        newest = juce::jmax (newest, f.getLastModificationTime());

    return newest;
}

void SaturatorAudioProcessorEditor::timerCallback()
{
    const auto newest = getNewestUiFileTime();

    if (newest != lastUiChange)
    {
        lastUiChange = newest;
        webView.refresh();
    }
}

std::optional<juce::WebBrowserComponent::Resource>
SaturatorAudioProcessorEditor::getResource (const juce::String& url)
{
    auto relativePath = url == "/" ? juce::String ("index.html")
                                   : url.fromFirstOccurrenceOf ("/", false, false);

    relativePath = relativePath.upToFirstOccurrenceOf ("?", false, false)
                               .upToFirstOccurrenceOf ("#", false, false);

    if (relativePath.isEmpty())
        relativePath = "index.html";

    // Never allow a page to read files outside the ui folder.
    if (relativePath.contains (".."))
        return std::nullopt;

    const auto file = uiFolder.getChildFile (relativePath);

    if (file.existsAsFile())
    {
        juce::MemoryBlock contents;

        if (file.loadFileAsData (contents))
            return juce::WebBrowserComponent::Resource { toBytes (contents.getData(), contents.getSize()),
                                                         getMimeType (file.getFileExtension()) };
    }

    if (relativePath == "index.html")
    {
        const juce::String message = "No ui/index.html found.\n\nExpected it at:\n"
                                     + uiFolder.getChildFile ("index.html").getFullPathName();
        const auto utf8 = message.toRawUTF8();

        return juce::WebBrowserComponent::Resource { toBytes (utf8, message.getNumBytesAsUTF8()),
                                                     juce::String ("text/plain") };
    }

    return std::nullopt;
}

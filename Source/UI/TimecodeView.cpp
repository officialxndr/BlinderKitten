/*
  ==============================================================================

    TimecodeView.cpp
    Created: 5 Jun 2026
    Author:  BlinderKitten

  ==============================================================================
*/

#include <JuceHeader.h>
#include "TimecodeView.h"
#include "Definitions/Interface/InterfaceIncludes.h"

// Number of timer ticks (at 30 Hz) without a frame change before the source is
// considered "idle" (~0.5 s).
static const int idleThresholdTicks = 15;

//==============================================================================
TimecodeViewUI::TimecodeViewUI(const String& contentName) :
    ShapeShifterContent(TimecodeView::getInstance(), contentName)
{
}

TimecodeViewUI::~TimecodeViewUI()
{
}

//==============================================================================
juce_ImplementSingleton(TimecodeView);

TimecodeView::TimecodeView()
{
    sourceSelector.setTextWhenNoChoicesAvailable("No MIDI interface");
    sourceSelector.addListener(this);
    addAndMakeVisible(sourceSelector);

    tcLabel.setJustificationType(juce::Justification::centred);
    tcLabel.setText("--:--:--:--", juce::dontSendNotification);
    tcLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(tcLabel);

    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setText("No timecode source", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(statusLabel);

    resized();
    startTimerHz(30);
}

TimecodeView::~TimecodeView()
{
    stopTimer();
}

void TimecodeView::paint(juce::Graphics&)
{
}

void TimecodeView::resized()
{
    juce::Rectangle<int> r = getLocalBounds().reduced(2);

    sourceSelector.setBounds(r.removeFromTop(24));
    statusLabel.setBounds(r.removeFromBottom(18));
    tcLabel.setBounds(r);

    // Scale the big readout to its area, mirroring Clock::resized().
    juce::Font f = juce::Font(10, juce::Font::plain);
    float h = f.getHeight();
    float w = jmax(1.0f, (float)f.getStringWidth("00:00:00:00"));

    float sizeH = 10 * tcLabel.getHeight() / h;
    float sizeW = 10 * tcLabel.getWidth() / w;
    tcLabel.setFont(juce::Font(jmin(sizeW, sizeH), juce::Font::plain));

    statusLabel.setFont(juce::Font(12, juce::Font::plain));
}

String TimecodeView::framesToString(int tc)
{
    // Frame count is stored on a 30 fps baseline (see MIDIInterface), so the
    // frame field is displayed on that scale regardless of the true source rate.
    int frame  = tc % 30;
    int second = (tc / 30) % 60;
    int minute = (tc / (30 * 60)) % 60;
    int hour   = tc / (30 * 60 * 60);
    return juce::String::formatted("%02d:%02d:%02d:%02d", hour, minute, second, frame);
}

void TimecodeView::rebuildSourceList(const StringArray& names)
{
    String prevSel = selectedName;

    sourceSelector.clear(juce::dontSendNotification);
    sourceSelector.addItem("Auto", 1);
    int id = 2;
    for (auto& n : names) sourceSelector.addItem(n, id++);

    currentSourceNames = names;

    if (prevSel.isEmpty() || !names.contains(prevSel))
    {
        selectedName = "";
        sourceSelector.setSelectedId(1, juce::dontSendNotification);
    }
    else
    {
        sourceSelector.setSelectedId(2 + names.indexOf(prevSel), juce::dontSendNotification);
    }
}

void TimecodeView::comboBoxChanged(juce::ComboBox* cb)
{
    if (cb != &sourceSelector) return;

    if (sourceSelector.getSelectedId() <= 1) selectedName = "";
    else selectedName = sourceSelector.getText();

    // force idle tracking to reset on the next tick
    lastTargetName = "";
}

void TimecodeView::timerCallback()
{
    juce::Array<MIDIInterface*> midiInterfaces = InterfaceManager::getInterfacesOfType<MIDIInterface>();

    // 1. Keep the dropdown in sync with the available interfaces.
    StringArray names;
    for (auto* mi : midiInterfaces) names.add(mi->niceName);
    if (names != currentSourceNames) rebuildSourceList(names);

    // 2. Resolve which interface to display.
    MIDIInterface* target = nullptr;

    if (selectedName.isNotEmpty())
    {
        for (auto* mi : midiInterfaces)
            if (mi->niceName == selectedName) { target = mi; break; }
    }
    else
    {
        // Auto : prefer whichever interface's frame changed since last tick.
        MIDIInterface* changed = nullptr;
        for (auto* mi : midiInterfaces)
        {
            auto it = lastSeenFrame.find(mi->niceName);
            if (it != lastSeenFrame.end() && it->second != mi->lastFrameSent) changed = mi;
        }

        if (changed != nullptr)
        {
            target = changed;
            autoActiveName = changed->niceName;
        }
        else
        {
            // keep the previously active source if it's still around...
            for (auto* mi : midiInterfaces)
                if (mi->niceName == autoActiveName) { target = mi; break; }

            // ...otherwise lock onto the first that has produced any timecode.
            if (target == nullptr)
                for (auto* mi : midiInterfaces)
                    if (mi->lastFrameSent >= 0) { target = mi; autoActiveName = mi->niceName; break; }
        }
    }

    // remember current frames for next tick's change detection
    for (auto* mi : midiInterfaces) lastSeenFrame[mi->niceName] = mi->lastFrameSent;

    // 3. Reset idle tracking when the resolved source changes.
    String targetName = target != nullptr ? target->niceName : String();
    if (targetName != lastTargetName)
    {
        idleTicks = 0;
        prevFrameForIdle = -1;
        lastTargetName = targetName;
    }

    // 4. Render.
    if (target == nullptr)
    {
        tcLabel.setText("--:--:--:--", juce::dontSendNotification);
        tcLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        statusLabel.setText("No timecode source", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        return;
    }

    int tc = target->lastFrameSent;

    if (tc < 0)
    {
        tcLabel.setText("--:--:--:--", juce::dontSendNotification);
        tcLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        statusLabel.setText(target->niceName + " - waiting", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        return;
    }

    if (tc != prevFrameForIdle) { idleTicks = 0; prevFrameForIdle = tc; }
    else if (idleTicks < 100000) idleTicks++;

    bool running = idleTicks < idleThresholdTicks;

    tcLabel.setText(framesToString(tc), juce::dontSendNotification);
    tcLabel.setColour(juce::Label::textColourId, running ? juce::Colours::white : juce::Colours::grey);

    statusLabel.setText(target->niceName + (running ? " - running" : " - idle"), juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, running ? juce::Colours::white : juce::Colours::grey);
}

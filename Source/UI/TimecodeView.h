/*
  ==============================================================================

    TimecodeView.h
    Created: 5 Jun 2026
    Author:  BlinderKitten

    Panel that displays the live incoming MIDI timecode (HH:MM:SS:FF) with a
    source selector (defaulting to "Auto") and a running/idle status line.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <map>

//==============================================================================
class TimecodeViewUI : public ShapeShifterContent
{
public:
    TimecodeViewUI(const String& contentName);
    ~TimecodeViewUI();

    static TimecodeViewUI* create(const String& name) { return new TimecodeViewUI(name); }
};


class TimecodeView : public juce::Component,
                     public juce::Timer,
                     public juce::ComboBox::Listener
{
public:
    juce_DeclareSingleton(TimecodeView, true);
    TimecodeView();
    ~TimecodeView() override;

    juce::ComboBox sourceSelector;  // item 1 = "Auto", then one per MIDI interface
    juce::Label    tcLabel;         // big HH:MM:SS:FF readout
    juce::Label    statusLabel;     // "<interface name> - running/idle" or "No timecode source"

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void comboBoxChanged(juce::ComboBox*) override;

private:
    // "" means Auto ; otherwise the niceName of the explicitly selected interface
    String selectedName;
    // niceNames currently listed in the dropdown (excluding "Auto"), to detect changes
    StringArray currentSourceNames;
    // per-interface last seen lastFrameSent, used by Auto mode to spot the active source
    std::map<String, int> lastSeenFrame;
    // interface name Auto mode is currently locked onto
    String autoActiveName;
    // name of the target shown last tick (to reset idle tracking on source switch)
    String lastTargetName;

    int prevFrameForIdle = -1;
    int idleTicks = 0;

    static String framesToString(int tc);
    void rebuildSourceList(const StringArray& names);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimecodeView)
};

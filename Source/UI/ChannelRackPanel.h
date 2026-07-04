#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppContext.h"
#include "FableLookAndFeel.h"

namespace fable
{

// Channel Rack: pattern selector on top, one row per channel with mute LED,
// pan/volume knobs, name button and a step grid grouped in beats of four.

class ChannelRackPanel : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit ChannelRackPanel (AppContext& ctx);
    ~ChannelRackPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ChannelRow;
    class Header;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildRows();
    void addChannelMenu();

    AppContext& context;
    std::unique_ptr<Header> header;
    juce::Viewport viewport;
    juce::Component rowHolder;
    std::vector<std::unique_ptr<ChannelRow>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackPanel)
};

} // namespace fable

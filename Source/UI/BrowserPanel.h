#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppContext.h"
#include "FableLookAndFeel.h"

namespace fable
{

// Left dock: discovered VST3 plugins on top, a sample-file browser below.
// Double-click an instrument to add it as a channel; double-click an audio
// file to add a sampler channel, or drag samples into the Channel Rack /
// Playlist.
//
// The sample browser isn't locked to one folder: the root selector lists
// "Home" plus any folders the user adds (e.g. a sample-pack library on
// another drive), persisted across sessions so they only need to add a
// folder once.

class BrowserPanel : public juce::Component,
                     private juce::ChangeListener,
                     private juce::ListBoxModel,
                     private juce::FileBrowserListener
{
public:
    explicit BrowserPanel (AppContext& ctx);
    ~BrowserPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // ListBoxModel (plugins)
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    // FileBrowserListener (samples)
    void selectionChanged() override {}
    void fileClicked (const juce::File&, const juce::MouseEvent&) override;
    void fileDoubleClicked (const juce::File&) override;
    void browserRootChanged (const juce::File&) override {}

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshPlugins();

    void loadSampleRoots();
    void saveSampleRoots();
    void refreshRootBox();
    void addFolderClicked();
    void removeCurrentFolderClicked();
    void setRoot (int rootIndex);
    void stopPreview();

    AppContext& context;
    juce::Label pluginsHeader { {}, "PLUGINS" }, samplesHeader { {}, "FILES" };
    juce::ListBox pluginList;
    juce::Array<juce::PluginDescription> descriptions;

    juce::ComboBox rootBox;
    juce::TextButton addFolderButton { "+" }, removeFolderButton { juce::String::fromUTF8 ("\xc3\x97") };
    juce::Array<juce::File> sampleRoots;   // index 0 is always the home directory
    std::unique_ptr<juce::FileChooser> folderChooser;

    juce::TimeSliceThread scanThread { "file browser" };
    std::unique_ptr<juce::WildcardFileFilter> fileFilter;
    std::unique_ptr<juce::DirectoryContentsList> dirContents;
    std::unique_ptr<juce::FileTreeComponent> fileTree;

    // Click-to-preview goes through the engine's lock-free preview node (see
    // AudioEngine::previewSampleFile) instead of a second AudioSourcePlayer on
    // the device - one audio callback, and swapping the preview node is
    // refcount-safe, which fixes the crashes from rapid re-auditioning.
    juce::String previewFilePath;      // currently-auditioned file (empty = none)
    juce::uint32 previewEndMs = 0;     // when the current preview is expected to finish

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
};

} // namespace fable

#pragma once

#include "Project.h"

namespace fable
{

// JSON (.fable) project persistence.
juce::var    projectToVar   (const Project& p);
juce::String projectToJson  (const Project& p);
bool         projectFromVar (const juce::var& v, Project& out);
bool         projectFromJson (const juce::String& json, Project& out);

bool saveProjectToFile   (const Project& p, const juce::File& file);
bool loadProjectFromFile (const juce::File& file, Project& out);

} // namespace fable

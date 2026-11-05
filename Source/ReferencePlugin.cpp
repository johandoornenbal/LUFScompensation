#include <JuceHeader.h>
#include "ReferenceProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ReferenceProcessor; }

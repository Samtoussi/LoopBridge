#include <JuceHeader.h>
#include <iostream>

int runGmailClientTests();

int main()
{
    const juce::ScopedJuceInitialiser_GUI initialise;
    const int failures = runGmailClientTests();
    std::cout << "Gmail worker tests: " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

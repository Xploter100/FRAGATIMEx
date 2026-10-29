#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// FRAGATIME - motor granular + efectos base.
// Fuente de granos: archivo cargado (.wav/.mp3/...) o, si no hay archivo,
// la entrada en vivo del canal de FL Studio.
class FragatimeProcessor : public juce::AudioProcessor
{
public:
    FragatimeProcessor();
    ~FragatimeProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FRAGATIME"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- archivo de audio (hilo de interfaz) ----
    bool loadAudioFile (const juce::File& file);
    void clearAudioFile();
    juce::String getLoadedFileName() const;
    std::vector<float> getPeaks() const;
    bool hasFile() const { return fileLoaded.load(); }
    float getPlayhead() const { return playhead.load(); }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    struct Grain
    {
        bool active = false;
        bool fromFile = true;
        double pos = 0.0;
        double inc = 1.0;
        int age = 0;
        int len = 1;
        float gainL = 1.f, gainR = 1.f;
    };

    struct Cur // parametros del chunk actual
    {
        double sizeSamples = 4000.0;
        float baseSemis = 0.f;
        float spray = 0.f;
        float position = 0.f;
        float arp = 0.f;
    };

    static constexpr int kMaxGrains = 96;
    static constexpr int kChunk = 512;

    void processChunk (juce::AudioBuffer<float>& buf, int start, int n);
    void spawnGrain (const juce::AudioBuffer<float>* src);

    // parametros (punteros atomicos del APVTS)
    std::atomic<float> *pIntensity, *pFx, *pFxAmt, *pDensity, *pArp, *pSize,
                       *pPitch, *pFeedback, *pGrain, *pPosition;

    // fuente
    juce::AudioFormatManager formatManager;
    juce::SpinLock srcLock;
    std::shared_ptr<juce::AudioBuffer<float>> fileBuf;
    double fileSR = 44100.0;
    std::atomic<bool> fileLoaded { false };
    std::atomic<bool> resetGrains { false };
    std::atomic<float> playhead { 0.f };

    mutable juce::CriticalSection infoLock;
    std::vector<float> peaks;
    juce::String loadedName, loadedPath;

    // motor
    double hostSR = 44100.0;
    std::array<Grain, kMaxGrains> grains;
    Cur cur;
    double spawnCounter = 0.0;
    int arpStep = 0;
    juce::Random rng;

    juce::AudioBuffer<float> live;   // anillo de entrada en vivo
    int liveSize = 1, liveWrite = 0;

    juce::AudioBuffer<float> fbBuf;  // feedback del bus de granos
    int fbSize = 1, fbPos = 0;

    // efectos
    juce::AudioBuffer<float> wet;
    juce::Reverb reverb;
    juce::dsp::Chorus<float> chorus;
    juce::AudioBuffer<float> delayBuf;
    int delaySize = 1, delayPos = 0;
    float holdL = 0.f, holdR = 0.f;
    int crushCount = 0;

    juce::SmoothedValue<float> intensitySm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FragatimeProcessor)
};

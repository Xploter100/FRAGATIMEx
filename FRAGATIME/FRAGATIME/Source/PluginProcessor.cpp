#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Escala pentatonica menor para el arpegiador (semitonos)
    const float kScale[] = { 0.f, 3.f, 5.f, 7.f, 10.f, 12.f, 15.f, 17.f };

    juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String stText  (float v, int) { return juce::String ((v - 50.f) / 50.f * 12.f, 1) + " st"; }

    inline double wrapPos (double p, int n)
    {
        if (p >= n) { p -= n; if (p >= n) p = std::fmod (p, (double) n); }
        if (p < 0.0) p = 0.0;
        return p;
    }

    inline float readLinear (const float* d, int n, double p)
    {
        const int i0 = (int) p;
        int i1 = i0 + 1;
        if (i1 >= n) i1 = 0;
        const float f = (float) (p - i0);
        return d[i0] + f * (d[i1] - d[i0]);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout FragatimeProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto add = [&p] (const String& id, const String& name, float def, bool semis = false)
    {
        auto attr = AudioParameterFloatAttributes().withStringFromValueFunction (semis ? stText : pctText);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                            NormalisableRange<float> (0.f, 100.f, 0.1f),
                                                            def, attr));
    };

    add ("intensity", "Intensidad", 70.f);
    add ("density",   "Density",    50.f);
    add ("size",      "Size",       40.f);
    add ("pitch",     "Pitch",      50.f, true);   // 50 % = sin cambio, +-12 semitonos
    add ("arp",       "Arpegiador",  0.f);
    add ("grain",     "Granulado",  30.f);
    add ("feedback",  "Feedback",   20.f);
    add ("position",  "Posicion",    0.f);
    add ("fxamount",  "FX Amount",  40.f);

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "fx", 1 }, "FX Base",
                 StringArray { "Ninguno", "Reverb", "Delay", "Bitcrusher", "Chorus" }, 0));

    return { p.begin(), p.end() };
}

FragatimeProcessor::FragatimeProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formatManager.registerBasicFormats();

    pIntensity = apvts.getRawParameterValue ("intensity");
    pFx        = apvts.getRawParameterValue ("fx");
    pFxAmt     = apvts.getRawParameterValue ("fxamount");
    pDensity   = apvts.getRawParameterValue ("density");
    pArp       = apvts.getRawParameterValue ("arp");
    pSize      = apvts.getRawParameterValue ("size");
    pPitch     = apvts.getRawParameterValue ("pitch");
    pFeedback  = apvts.getRawParameterValue ("feedback");
    pGrain     = apvts.getRawParameterValue ("grain");
    pPosition  = apvts.getRawParameterValue ("position");
}

bool FragatimeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void FragatimeProcessor::prepareToPlay (double sampleRate, int)
{
    hostSR = sampleRate;

    liveSize = (int) (sampleRate * 8.0);
    live.setSize (2, liveSize);  live.clear();  liveWrite = 0;

    fbSize = (int) (sampleRate * 0.12) + 1;
    fbBuf.setSize (2, fbSize);   fbBuf.clear(); fbPos = 0;

    delaySize = (int) (sampleRate * 1.5);
    delayBuf.setSize (2, delaySize); delayBuf.clear(); delayPos = 0;

    wet.setSize (2, kChunk);     wet.clear();

    reverb.setSampleRate (sampleRate);
    reverb.reset();

    chorus.prepare ({ sampleRate, (juce::uint32) kChunk, 2 });
    chorus.setRate (0.9f);
    chorus.setDepth (0.35f);
    chorus.setCentreDelay (8.f);
    chorus.setFeedback (0.15f);
    chorus.reset();

    for (auto& g : grains) g.active = false;
    spawnCounter = 0.0;
    arpStep = 0;
    holdL = holdR = 0.f;
    crushCount = 0;

    intensitySm.reset (sampleRate, 0.05);
    intensitySm.setCurrentAndTargetValue (pIntensity->load() * 0.01f);
}

void FragatimeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    const int total = buffer.getNumSamples();
    for (int s = 0; s < total; s += kChunk)
        processChunk (buffer, s, juce::jmin (kChunk, total - s));
}

void FragatimeProcessor::spawnGrain (const juce::AudioBuffer<float>* src)
{
    for (auto& g : grains)
    {
        if (g.active) continue;

        const int len = juce::jmax (32, (int) cur.sizeSamples);

        // pitch: base + arpegiador + un poco de azar segun granulado
        float st = cur.baseSemis;
        if (cur.arp > 0.01f)
        {
            const int steps = 2 + (int) (cur.arp * 6.99f);   // 2..8 notas
            st += kScale[arpStep % steps];
            ++arpStep;
        }
        st += (rng.nextFloat() * 2.f - 1.f) * cur.spray * 1.5f;
        st = juce::jlimit (-36.f, 36.f, st);
        const double rate = std::pow (2.0, st / 12.0);

        g.fromFile = (src != nullptr);
        g.len = len;
        g.age = 0;

        if (src != nullptr)
        {
            const int n = src->getNumSamples();
            g.inc = rate * (fileSR / hostSR);
            double start = cur.position * n + (rng.nextFloat() * 2.f - 1.f) * cur.spray * 0.5 * fileSR;
            while (start < 0.0) start += n;
            g.pos = wrapPos (start, n);
            playhead.store ((float) (g.pos / n));
        }
        else
        {
            g.inc = rate;
            double back = len * juce::jmax (1.0, rate)
                        + rng.nextFloat() * (0.02 + cur.spray * 0.8) * hostSR;
            back = juce::jmin (back, (double) (liveSize - 2));
            double start = liveWrite - back;
            while (start < 0.0) start += liveSize;
            g.pos = wrapPos (start, liveSize);
        }

        const float pan = (rng.nextFloat() * 2.f - 1.f) * cur.spray;
        const float angle = (pan + 1.f) * 0.25f * juce::MathConstants<float>::pi;
        g.gainL = std::cos (angle) * 1.4142f;
        g.gainR = std::sin (angle) * 1.4142f;

        g.active = true;
        return;
    }
}

void FragatimeProcessor::processChunk (juce::AudioBuffer<float>& buf, int start, int n)
{
    // ---- parametros ----
    const float intensity = pIntensity->load() * 0.01f;
    const float density   = pDensity->load()   * 0.01f;
    const float size      = pSize->load()      * 0.01f;
    const float pitch     = pPitch->load()     * 0.01f;
    const float arp       = pArp->load()       * 0.01f;
    const float grainAmt  = pGrain->load()     * 0.01f;
    const float fbAmt     = pFeedback->load()  * 0.01f;
    const float position  = pPosition->load()  * 0.01f;
    const float fxAmt     = pFxAmt->load()     * 0.01f;
    const int   fxType    = (int) pFx->load();

    intensitySm.setTargetValue (intensity);

    const float gps       = 0.5f + 60.f * density * density;      // granos por segundo
    const float sizeMs    = 15.f * std::pow (40.f, size);         // 15 ms .. 600 ms
    cur.sizeSamples = sizeMs * 0.001 * hostSR;
    cur.baseSemis   = (pitch - 0.5f) * 24.f;                      // +-12 st
    cur.spray       = grainAmt;
    cur.position    = position;
    cur.arp         = arp;

    const float norm = 1.f / std::sqrt (juce::jmax (1.f, gps * sizeMs * 0.001f));
    const float fb   = fbAmt * 0.92f;

    if (resetGrains.exchange (false))
        for (auto& g : grains) g.active = false;

    float* L  = buf.getWritePointer (0, start);
    float* R  = buf.getWritePointer (1, start);
    float* wl = wet.getWritePointer (0);
    float* wr = wet.getWritePointer (1);
    float* liveL = live.getWritePointer (0);
    float* liveR = live.getWritePointer (1);
    float* fbL = fbBuf.getWritePointer (0);
    float* fbR = fbBuf.getWritePointer (1);

    // el hilo de carga solo toma este bloqueo durante un intercambio de punteros
    juce::SpinLock::ScopedTryLockType lock (srcLock);
    const bool ok = lock.isLocked();
    const juce::AudioBuffer<float>* src =
        (ok && fileBuf != nullptr && fileBuf->getNumSamples() > 0) ? fileBuf.get() : nullptr;

    for (int i = 0; i < n; ++i)
    {
        liveL[liveWrite] = L[i];
        liveR[liveWrite] = R[i];
        if (++liveWrite >= liveSize) liveWrite = 0;

        float gl = 0.f, gr = 0.f;

        if (ok)
        {
            spawnCounter -= 1.0;
            if (spawnCounter <= 0.0)
            {
                spawnGrain (src);
                double interval = hostSR / gps;
                interval *= 1.0 + (rng.nextFloat() * 2.f - 1.f) * grainAmt * 0.8;
                spawnCounter += juce::jmax (1.0, interval);
            }

            for (auto& g : grains)
            {
                if (! g.active) continue;

                const float* dl; const float* dr; int nn;
                if (g.fromFile)
                {
                    if (src == nullptr) { g.active = false; continue; }
                    dl = src->getReadPointer (0);
                    dr = src->getReadPointer (1);
                    nn = src->getNumSamples();
                }
                else { dl = liveL; dr = liveR; nn = liveSize; }

                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                         * (float) g.age / (float) g.len);
                gl += readLinear (dl, nn, g.pos) * w * g.gainL;
                gr += readLinear (dr, nn, g.pos) * w * g.gainR;

                g.pos = wrapPos (g.pos + g.inc, nn);
                if (++g.age >= g.len) g.active = false;
            }
        }

        gl *= norm;
        gr *= norm;

        // feedback del bus de granos (comb corto, saturado para que sea estable)
        const float outL = gl + fbL[fbPos];
        const float outR = gr + fbR[fbPos];
        fbL[fbPos] = std::tanh (outL * fb);
        fbR[fbPos] = std::tanh (outR * fb);
        if (++fbPos >= fbSize) fbPos = 0;

        wl[i] = outL;
        wr[i] = outR;
    }

    // ---- efecto base ----
    if (fxType == 1) // Reverb
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f + 0.65f * fxAmt;
        rp.damping  = 0.4f;
        rp.wetLevel = 0.5f * fxAmt;
        rp.dryLevel = 1.f - 0.5f * fxAmt;
        rp.width    = 1.f;
        reverb.setParameters (rp);
        reverb.processStereo (wl, wr, n);
    }
    else if (fxType == 2) // Delay
    {
        const int dt = (int) (0.32 * hostSR);
        float* dL = delayBuf.getWritePointer (0);
        float* dR = delayBuf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            int rp = delayPos - dt;
            if (rp < 0) rp += delaySize;
            const float xl = wl[i], xr = wr[i];
            const float yl = dL[rp], yr = dR[rp];
            dL[delayPos] = xl + yl * 0.5f;
            dR[delayPos] = xr + yr * 0.5f;
            if (++delayPos >= delaySize) delayPos = 0;
            wl[i] = xl * (1.f - 0.4f * fxAmt) + yl * fxAmt;
            wr[i] = xr * (1.f - 0.4f * fxAmt) + yr * fxAmt;
        }
    }
    else if (fxType == 3) // Bitcrusher
    {
        const float bits = 16.f - 13.f * fxAmt;
        const float q = std::pow (2.f, bits - 1.f);
        const int down = 1 + (int) (fxAmt * 24.f);
        const float mix = juce::jmin (1.f, fxAmt * 4.f);
        for (int i = 0; i < n; ++i)
        {
            if (crushCount <= 0)
            {
                holdL = std::round (wl[i] * q) / q;
                holdR = std::round (wr[i] * q) / q;
                crushCount = down;
            }
            --crushCount;
            wl[i] += (holdL - wl[i]) * mix;
            wr[i] += (holdR - wr[i]) * mix;
        }
    }
    else if (fxType == 4) // Chorus
    {
        chorus.setMix (fxAmt);
        juce::dsp::AudioBlock<float> block (wet);
        auto sub = block.getSubBlock (0, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        chorus.process (ctx);
    }

    // ---- mezcla final (Intensidad) ----
    for (int i = 0; i < n; ++i)
    {
        const float I = intensitySm.getNextValue();
        L[i] = L[i] * (1.f - I) + wl[i] * I;
        R[i] = R[i] * (1.f - I) + wr[i] * I;
    }
}

// ------------------------------------------------------------------ archivo

bool FragatimeProcessor::loadAudioFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    // limite de 5 minutos para cuidar la RAM (8 GB)
    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                   (juce::int64) (reader->sampleRate * 300.0));

    auto nb = std::make_shared<juce::AudioBuffer<float>> (2, len);
    nb->clear();
    reader->read (nb.get(), 0, len, 0, true, true);
    if (reader->numChannels == 1)
        nb->copyFrom (1, 0, *nb, 0, 0, len);

    // miniatura de la onda
    const int bins = 800;
    std::vector<float> pk ((size_t) bins, 0.f);
    const float* d = nb->getReadPointer (0);
    for (int b = 0; b < bins; ++b)
    {
        const juce::int64 s0 = (juce::int64) len * b / bins;
        const juce::int64 s1 = (juce::int64) len * (b + 1) / bins;
        const juce::int64 stride = juce::jmax<juce::int64> (1, (s1 - s0) / 256);
        float m = 0.f;
        for (juce::int64 s = s0; s < s1; s += stride)
            m = juce::jmax (m, std::abs (d[s]));
        pk[(size_t) b] = m;
    }

    {
        juce::SpinLock::ScopedLockType sl (srcLock);
        std::swap (fileBuf, nb);          // el buffer viejo se libera fuera del bloqueo
        fileSR = reader->sampleRate;
    }
    fileLoaded = true;
    resetGrains = true;

    {
        juce::ScopedLock sl (infoLock);
        peaks = std::move (pk);
        loadedName = file.getFileName();
        loadedPath = file.getFullPathName();
    }
    return true;
}

void FragatimeProcessor::clearAudioFile()
{
    std::shared_ptr<juce::AudioBuffer<float>> old;
    {
        juce::SpinLock::ScopedLockType sl (srcLock);
        old.swap (fileBuf);
    }
    fileLoaded = false;
    resetGrains = true;

    juce::ScopedLock sl (infoLock);
    peaks.clear();
    loadedName.clear();
    loadedPath.clear();
}

juce::String FragatimeProcessor::getLoadedFileName() const
{
    juce::ScopedLock sl (infoLock);
    return loadedName;
}

std::vector<float> FragatimeProcessor::getPeaks() const
{
    juce::ScopedLock sl (infoLock);
    return peaks;
}

// ------------------------------------------------------------------ estado

void FragatimeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    {
        juce::ScopedLock sl (infoLock);
        state.setProperty ("filePath", loadedPath, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void FragatimeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (vt);

            const auto path = vt.getProperty ("filePath").toString();
            if (path.isNotEmpty())
            {
                juce::File f (path);
                if (f.existsAsFile()) loadAudioFile (f);
            }
        }
    }
}

juce::AudioProcessorEditor* FragatimeProcessor::createEditor()
{
    return new FragatimeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FragatimeProcessor();
}

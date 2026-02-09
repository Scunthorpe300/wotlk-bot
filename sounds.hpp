#ifndef SOUNDS_HPP
#define SOUNDS_HPP

#include <SFML/Audio.hpp>
#include <vector>
#include <cmath>
#include <cstdint>
#include <memory>

class ChiptuneSound
{
private:
    sf::SoundBuffer soundBuffer;
    std::unique_ptr<sf::Sound> sound;
    std::vector<std::int16_t> samples;

    const unsigned int SAMPLE_RATE = 44100;
    const std::int16_t AMPLITUDE = 30000;
    const float DURATION = 0.2f;

    bool loadBuffer()
    {
        bool result = soundBuffer.loadFromSamples(samples.data(), samples.size(), 1, SAMPLE_RATE,
                                                  {sf::SoundChannel::Mono});

        sound = std::make_unique<sf::Sound>(soundBuffer);
        sound->setVolume(30.0f);

        return result;
    }

public:
    ChiptuneSound()
    {
        generateEnableSound();
    }

    void generateEnableSound()
    {
        unsigned int sampleCount = static_cast<unsigned int>(SAMPLE_RATE * DURATION);
        samples.clear();
        samples.reserve(sampleCount);

        for (unsigned int i = 0; i < sampleCount; ++i)
        {
            float t = static_cast<float>(i) / SAMPLE_RATE;
            float progress = t / DURATION;

            float frequency = 200.0f + (600.0f * progress);

            float period = SAMPLE_RATE / frequency;
            float pulseWidth = 0.3f + 0.4f * progress;
            int waveValue = (std::fmod(static_cast<float>(i), period) < period * pulseWidth) ? 1 : -1;

            float envelope = 1.0f;
            if (t < 0.01f)
            {
                envelope = t / 0.01f;
            }
            else if (t > DURATION - 0.01f)
            {
                envelope = (DURATION - t) / 0.01f;
            }

            samples.push_back(static_cast<std::int16_t>(waveValue * AMPLITUDE * envelope));
        }

        loadBuffer();
    }

    void generateDisableSound()
    {
        unsigned int sampleCount = static_cast<unsigned int>(SAMPLE_RATE * DURATION);
        samples.clear();
        samples.reserve(sampleCount);

        for (unsigned int i = 0; i < sampleCount; ++i)
        {
            float t = static_cast<float>(i) / SAMPLE_RATE;
            float progress = t / DURATION;

            float frequency = 800.0f - (600.0f * progress);

            float period = SAMPLE_RATE / frequency;
            float pulseWidth = 0.7f - 0.4f * progress;
            int waveValue = (std::fmod(static_cast<float>(i), period) < period * pulseWidth) ? 1 : -1;

            float envelope = 1.0f;
            if (t < 0.01f)
            {
                envelope = t / 0.01f;
            }
            else if (t > DURATION - 0.01f)
            {
                envelope = (DURATION - t) / 0.01f;
            }

            samples.push_back(static_cast<std::int16_t>(waveValue * AMPLITUDE * envelope * 0.8f));
        }

        loadBuffer();
    }

    void playEnable()
    {
        generateEnableSound();
        if (sound) sound->play();
    }

    void playDisable()
    {
        generateDisableSound();
        if (sound) sound->play();
    }

    void play()
    {
        if (sound) sound->play();
    }

    void stop()
    {
        if (sound) sound->stop();
    }

    void setVolume(float volume)
    {
        if (sound) sound->setVolume(volume);
    }
};

#endif

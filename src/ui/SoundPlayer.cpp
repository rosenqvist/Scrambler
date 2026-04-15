#include "ui/SoundPlayer.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace scrambler::ui
{

namespace
{

constexpr int kSampleRate = 44100;
constexpr double kInt16Max = 32767.0;

}  // namespace

SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent)
{
}

SoundPlayer::~SoundPlayer()
{
    Cleanup();
}

void SoundPlayer::PlayTone(double frequency_hz, int duration_ms, int volume_percent)
{
    Cleanup();

    QAudioFormat format;
    format.setSampleRate(kSampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    const auto device = QMediaDevices::defaultAudioOutput();
    if (!device.isFormatSupported(format))
    {
        return;
    }

    const int num_samples = (kSampleRate * duration_ms) / 1000;
    samples_.resize(static_cast<qsizetype>(num_samples) * static_cast<qsizetype>(sizeof(int16_t)));
    auto* samples_ptr = reinterpret_cast<int16_t*>(samples_.data());

    const double amplitude = std::clamp(volume_percent, 0, 100) / 100.0;
    for (int i = 0; i < num_samples; ++i)
    {
        const double t = static_cast<double>(i) / kSampleRate;
        const double sample = std::sin(2.0 * std::numbers::pi * frequency_hz * t) * amplitude;
        samples_ptr[i] = static_cast<int16_t>(sample * kInt16Max);
    }

    buffer_ = new QBuffer(&samples_, this);
    buffer_->open(QIODevice::ReadOnly);

    sink_ = new QAudioSink(device, format, this);
    connect(sink_,
            &QAudioSink::stateChanged,
            this,
            [this](QAudio::State state)
    {
        if (state == QAudio::IdleState)
        {
            Cleanup();
        }
    });

    sink_->start(buffer_);
}

void SoundPlayer::Cleanup()
{
    if (sink_)
    {
        sink_->stop();
        sink_->deleteLater();
        sink_ = nullptr;
    }
    if (buffer_)
    {
        buffer_->close();
        buffer_->deleteLater();
        buffer_ = nullptr;
    }
}

}  // namespace scrambler::ui

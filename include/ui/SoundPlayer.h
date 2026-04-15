#pragma once

#include <QByteArray>
#include <QObject>

class QAudioSink;
class QBuffer;

namespace scrambler::ui
{

// Plays short sine tones via Qt's audio stack.
class SoundPlayer : public QObject
{
    Q_OBJECT

public:
    explicit SoundPlayer(QObject* parent = nullptr);
    ~SoundPlayer() override;

    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;
    SoundPlayer(SoundPlayer&&) = delete;
    SoundPlayer& operator=(SoundPlayer&&) = delete;

    void PlayTone(double frequency_hz, int duration_ms, int volume_percent);

private:
    void Cleanup();

    QAudioSink* sink_ = nullptr;
    QBuffer* buffer_ = nullptr;
    QByteArray samples_;
};

}  // namespace scrambler::ui
